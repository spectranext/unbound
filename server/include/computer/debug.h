#ifndef DEBUG_H
#define DEBUG_H

#include <inttypes.h>


/* Define this to spew debugging info to stdout */
#define W5100_DEBUG 1
#define W5100_ERROR 1
#define W5100_VERBOSE 0

#if W5100_DEBUG
#include <stdio.h>
#define nic_w5100_debug printf
#else
#define nic_w5100_debug(...)i
#endif

#if W5100_VERBOSE
#define nic_w5100_verbose printf
#else
#define nic_w5100_verbose(...)
#endif

#if W5100_ERROR
#define nic_w5100_error printf
#else
#define nic_w5100_error(...)
#endif



#endif