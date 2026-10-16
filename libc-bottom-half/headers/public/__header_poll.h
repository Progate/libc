#ifndef __wasilibc___header_poll_h
#define __wasilibc___header_poll_h

#include <__struct_pollfd.h>
#include <__typedef_nfds_t.h>

/*
 * BrowserOS: poll(2) の出来事は Linux（musl の include/poll.h と arch/generic/bits/poll.h）の値にする。カーネルの
 * ppoll(2) へそのまま渡せる
 */
#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200
#define POLLMSG    0x400
#define POLLRDHUP  0x2000

#ifdef __cplusplus
extern "C" {
#endif

int poll(struct pollfd[], nfds_t, int);

#ifdef __cplusplus
}
#endif

#endif
