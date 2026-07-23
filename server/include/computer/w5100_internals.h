/* w5100.c: Wiznet W5100 emulation - internal routines
   
   Emulates a minimal subset of the Wiznet W5100 TCP/IP controller.

   Copyright (c) 2011-2015 Philip Kendall
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
   
   Author contact information:
   
   E-mail: philip-fuse@shadowmagic.org.uk
 
*/

#ifndef FUSE_W5100_INTERNALS_H
#define FUSE_W5100_INTERNALS_H

#include <signal.h>
#include <stdint.h>

#ifndef WIN32
#include <sys/select.h>
#include "compat.h"

#endif

typedef enum w5100_socket_mode {
  W5100_SOCKET_MODE_CLOSED = 0x00,
  W5100_SOCKET_MODE_TCP,
  W5100_SOCKET_MODE_UDP,
  W5100_SOCKET_MODE_IPRAW,
  W5100_SOCKET_MODE_MACRAW,
  W5100_SOCKET_MODE_PPPOE,
} w5100_socket_mode;

typedef enum w5100_socket_state {
  W5100_SOCKET_STATE_CLOSED = 0x00,

  W5100_SOCKET_STATE_INIT = 0x13,
  W5100_SOCKET_STATE_LISTEN,
  W5100_SOCKET_STATE_ESTABLISHED = 0x17,
  W5100_SOCKET_STATE_CLOSE_WAIT = 0x1c,

  W5100_SOCKET_STATE_UDP = 0x22,
} w5100_socket_state;

enum w5100_socket_registers {
  W5100_SOCKET_MR = 0x00,
  W5100_SOCKET_CR,
  W5100_SOCKET_IR,
  W5100_SOCKET_SR,
  
  W5100_SOCKET_PORT0,
  W5100_SOCKET_PORT1,

  W5100_SOCKET_DIPR0 = 0x0c,
  W5100_SOCKET_DIPR1,
  W5100_SOCKET_DIPR2,
  W5100_SOCKET_DIPR3,

  W5100_SOCKET_DPORT0,
  W5100_SOCKET_DPORT1,

  W5100_SOCKET_TX_FSR0 = 0x20,
  W5100_SOCKET_TX_FSR1,
  W5100_SOCKET_TX_RR0,
  W5100_SOCKET_TX_RR1,
  W5100_SOCKET_TX_WR0,
  W5100_SOCKET_TX_WR1,

  W5100_SOCKET_RX_RSR0,
  W5100_SOCKET_RX_RSR1,
  W5100_SOCKET_RX_RD0,
  W5100_SOCKET_RX_RD1,
};

typedef enum w5100_socket_type {
  W5100_SOCKET_TYPE_INET = 0,
  W5100_SOCKET_TYPE_UNIX
} w5100_socket_type;

struct network_device_t;

typedef struct nic_w5100_socket_t {

  int id; /* For debug use only */

  /* W5100 properties */

  w5100_socket_mode mode;
  uint8_t flags;
  w5100_socket_type type;

  w5100_socket_state state;
  uint8_t listening;

  uint8_t ir;      /* Interrupt register */

  union {
      uint8_t port[2]; /* Source port */
      uint16_t port_uint16;
  };

  union {
      uint8_t dip[4];  /* Destination IP address */
      uint32_t dip_uint32;
  };
  union {
      uint8_t dport[2];/* Destination port */
      uint16_t dport_uint16;
  };

  uint16_t tx_rr;   /* Transmit read pointer */
  uint16_t tx_wr;   /* Transmit write pointer */

  uint16_t rx_rsr;  /* Received size */
  uint16_t rx_rd;   /* Received read pointer */

  uint16_t old_rx_rd; /* Used in RECV command processing */

  uint8_t tx_buffer[0x800];  /* Transmit buffer */
  uint8_t rx_buffer[0x800];  /* Received buffer */

  /* Host properties */

  compat_socket_t fd;       /* Socket file descriptor */
  int bind_count;           /* Number of writes to the Sn_PORTx registers we've received */
  int socket_bound;         /* True once we've bound the socket to a port */
  int write_pending;        /* True if we're waiting to write data on this socket */

  int last_send;            /* The value of Sn_TX_WR when the SEND command was last sent */
  int datagram_lengths[0x20]; /* The lengths of datagrams to be sent */
  int datagram_count;

  /* Flag used to indicate that a socket has been closed since we started
     waiting for it in a select() call and therefore the socket should no
     longer be used */
  int ok_for_io;

  pthread_mutex_t lock;     /* Mutex for this socket */

  struct network_device_t* device;

} nic_w5100_socket_t;

struct nic_w5100_t {
  uint8_t gw[4];   /* Gateway IP address */
  uint8_t sub[4];  /* Our subnet mask */
  uint8_t sha[6];  /* MAC address */
  uint8_t sip[4];  /* Our IP address */

  nic_w5100_socket_t socket[4];

  pthread_t thread;         /* Thread for doing I/O */
  sig_atomic_t stop_io_thread; /* Flag to stop I/O thread */
  struct compat_socket_selfpipe_t selfpipe; /* Device for waking I/O thread */
};

void nic_w5100_socket_init( nic_w5100_socket_t *socket, int which, struct network_device_t* device );
void nic_w5100_socket_end( nic_w5100_socket_t *socket );

void nic_w5100_socket_reset( nic_w5100_socket_t *socket );

uint8_t nic_w5100_socket_read( struct nic_w5100_t *self, uint16_t reg );
void nic_w5100_socket_write( struct nic_w5100_t *self, uint16_t reg, uint8_t b );

uint8_t nic_w5100_socket_read_rx_buffer( struct nic_w5100_t *self, uint16_t reg );
void nic_w5100_socket_write_tx_buffer( struct nic_w5100_t *self, uint16_t reg, uint8_t b );

void nic_w5100_socket_add_to_sets( nic_w5100_socket_t *socket, fd_set *readfds,
  fd_set *writefds, int *max_fd );
void nic_w5100_socket_process_io( struct nic_w5100_t *self, nic_w5100_socket_t *socket, fd_set readfds,
  fd_set writefds );

#endif                          /* #ifndef FUSE_W5100_H */
