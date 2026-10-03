// open(2) の O_CREAT と mkdir(2) が新しく作るものの権限。
//
// WASI の path_open / path_create_directory は権限を受け取らないので、作ったものはカーネルの既定
// （ファイル 0644・ディレクトリ 0755）になる。Unix では `mode & ~umask` になるので、それと違うときだけ
// 作った直後にカーネルの chmod で付け替える（mkstemp の 0600 や、umask 077 のシェルが作るファイル）。
//
// O_CREAT で開いたとき、作ったのか既にあったのかは path_open からは分からない。既にあったファイルの権限を
// 変えてはいけないので、まず O_EXCL を付けて開き、作れたときだけ付け替える。

#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <wasi/libc-nocwd.h>

/// カーネルが新しいファイル・ディレクトリに付ける権限（browser-os の DEFAULT_FILE_MODE / DEFAULT_DIRECTORY_MODE）
#define KERNEL_FILE_MODE 0644
#define KERNEL_DIRECTORY_MODE 0755

static int created(int fd, mode_t wanted) {
  if (fd >= 0)
    __browseros_fchmod(fd, wanted);
  return fd;
}

int __browseros_nocwd_openat(int dirfd, const char *path, int oflag, mode_t mode) {
  if (!(oflag & O_CREAT))
    return __wasilibc_nocwd_openat_nomode(dirfd, path, oflag);
  mode_t wanted = __browseros_creation_mode(mode);
  if (wanted == KERNEL_FILE_MODE)
    return __wasilibc_nocwd_openat_nomode(dirfd, path, oflag);
  if (oflag & O_EXCL)
    return created(__wasilibc_nocwd_openat_nomode(dirfd, path, oflag), wanted);

  int fd = __wasilibc_nocwd_openat_nomode(dirfd, path, oflag | O_EXCL);
  if (fd >= 0 || errno != EEXIST)
    return created(fd, wanted);
  // 既にあった。権限はそのままで開く
  fd = __wasilibc_nocwd_openat_nomode(dirfd, path, oflag & ~O_CREAT);
  if (fd >= 0 || errno != ENOENT)
    return fd;
  /*
   * 名前はあるのに開けば無い。行き先の無いシンボリックリンク（O_EXCL はリンク自体があると EEXIST）か、
   * 間に消された。どちらも O_CREAT で開けば今作ることになるので、作ったものとして付け替える
   */
  return created(__wasilibc_nocwd_openat_nomode(dirfd, path, oflag), wanted);
}

int __browseros_nocwd_mkdirat(int dirfd, const char *path, mode_t mode) {
  int result = __wasilibc_nocwd_mkdirat_nomode(dirfd, path);
  mode_t wanted = __browseros_creation_mode(mode);
  if (result == 0 && wanted != KERNEL_DIRECTORY_MODE)
    __browseros_chmod_at(dirfd, path, strlen(path), wanted);
  return result;
}
