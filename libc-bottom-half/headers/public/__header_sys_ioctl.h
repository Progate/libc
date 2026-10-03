#ifndef __wasilibc___header_sys_ioctl_h
#define __wasilibc___header_sys_ioctl_h

#define FIONREAD 1
#define FIONBIO 2

#include <features.h>
#ifdef __wasilibc_browseros
/* BrowserOS の端末の大きさ（→ libc-bottom-half/sources/browseros/termios.c）。番号は Linux と同じ */
#define __NEED_struct_winsize
#include <bits/alltypes.h>
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#endif

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int, int, ...);

#ifdef __cplusplus
}
#endif

#endif
