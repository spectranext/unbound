/* socket.c: Socket-related compatibility routines
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

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "computer/compat.h"
#include "computer/debug.h"

const compat_socket_t compat_socket_invalid = -1;
const int compat_socket_EBADF = EBADF;

int
compat_socket_blocking_mode( compat_socket_t fd, int blocking )
{
  int flags = fcntl( fd, F_GETFL, 0 );
  if( flags == -1 )
    return -1;
  flags = blocking ? ( flags | O_NONBLOCK ) : ( flags & ~O_NONBLOCK );
  return ( fcntl( fd, F_SETFL, flags ) != 0 );
}

int
compat_socket_close( compat_socket_t fd )
{
  return close( fd );
}

int compat_socket_get_error( void )
{
  return errno;
}

const char *
compat_socket_get_strerror( void )
{
  return strerror( errno );
}

void compat_socket_selfpipe_init( struct compat_socket_selfpipe_t* self )
{
  int error;
  int pipefd[2];

  error = pipe( pipefd );
  if( error ) {
      nic_w5100_error( "%s: %d: error %d creating pipe", __FILE__, __LINE__, error );
  }

  self->read_fd = pipefd[0];
  self->write_fd = pipefd[1];
}

void compat_socket_selfpipe_destroy( struct compat_socket_selfpipe_t *self )
{
  close( self->read_fd );
  close( self->write_fd );
}

compat_socket_t compat_socket_selfpipe_get_read_fd( struct compat_socket_selfpipe_t *self )
{
  return self->read_fd;
}

void compat_socket_selfpipe_wake( struct compat_socket_selfpipe_t *self )
{
  const char dummy = 0;
  ssize_t unused = write( self->write_fd, &dummy, 1 );
  (void) unused;
}

void compat_socket_selfpipe_discard_data( struct compat_socket_selfpipe_t *self )
{
  char bitbucket;
  ssize_t bytes_read;

  do {
    bytes_read = read( self->read_fd, &bitbucket, 1 );
    if( bytes_read == -1 && errno != EINTR ) {
      nic_w5100_error( "%s: %d: unexpected error %d (%s) reading from pipe", __FILE__,
                       __LINE__, errno, strerror(errno) );
    }
  } while( bytes_read < 0 );
}
