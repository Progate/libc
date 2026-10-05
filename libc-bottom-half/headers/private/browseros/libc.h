// BrowserOS の libc の中だけで使う関数（ホスト関数ではないもの。→ host.h）。

#ifndef __BROWSEROS_LIBC_H
#define __BROWSEROS_LIBC_H

#include <sys/types.h>

/// open(2) / mkdir(2) が新しく作るものに付ける権限（`mode & ~umask`）。→ process.c
mode_t __browseros_creation_mode(mode_t mode);

/// preopen からの相対パスを `mode & ~umask` の権限で開く・作る（open(2) の mode を活かす）。→ create.c
int __browseros_nocwd_openat(int dirfd, const char *path, int oflag, mode_t mode);

/// preopen からの相対パスに `mode & ~umask` の権限でディレクトリを作る。→ create.c
int __browseros_nocwd_mkdirat(int dirfd, const char *path, mode_t mode);

/// いま無視しているシグナルと、呼んだスレッドが塞いでいるシグナルをカーネルへ知らせる（posix_spawn の子へ継ぐ）。→ signal.c
void __browseros_report_signals(void);

/// このスレッドでハンドラを走らせた回数。ppoll・sigsuspend が「待つあいだに配られたか」を見る。→ signal.c
unsigned __browseros_handled_count(void);

/// 読む端の無いパイプへ書いて EPIPE になったときに呼ぶ。SIGPIPE を起こし、errno は EPIPE のまま戻す。→ signal.c
void __browseros_broken_pipe(void);

#endif
