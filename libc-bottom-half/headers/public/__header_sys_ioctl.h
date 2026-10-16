#ifndef __wasilibc___header_sys_ioctl_h
#define __wasilibc___header_sys_ioctl_h

/* BrowserOS: ioctl(2) の要求は Linux（asm-generic/ioctls.h）の値 */
#define FIONREAD 0x541B
#define FIONBIO 0x5421

#include <features.h>
#ifdef __wasilibc_browseros
/* BrowserOS の端末の要求（カーネルの ioctl(2) が受ける。→ libc-bottom-half/sources/browseros/termios.c）。番号は Linux と同じ */
#define __NEED_struct_winsize
#include <bits/alltypes.h>
#define TCGETS 0x5401
#define TCSETS 0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404
#define TCSBRK 0x5409
#define TCXONC 0x540A
#define TCFLSH 0x540B
#define TIOCSCTTY 0x540E
#define TIOCGPGRP 0x540F
#define TIOCSPGRP 0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define TIOCNOTTY 0x5422
#define TIOCGSID 0x5429
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#endif

#ifdef __cplusplus
extern "C" {
#endif

int ioctl(int, int, ...);

#ifdef __cplusplus
}
#endif

#endif
