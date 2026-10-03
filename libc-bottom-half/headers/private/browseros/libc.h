// BrowserOS の libc の中だけで使う関数（ホスト関数ではないもの。→ host.h）。

#ifndef __BROWSEROS_LIBC_H
#define __BROWSEROS_LIBC_H

#include <sys/types.h>

/// open(2) / mkdir(2) が新しく作るものに付ける権限（`mode & ~umask`）。→ process.c
mode_t __browseros_creation_mode(mode_t mode);

/// 読む端の無いパイプへ書いて EPIPE になったときに呼ぶ。SIGPIPE を起こし、errno は EPIPE のまま戻す。→ signal.c
void __browseros_broken_pipe(void);

#endif
