// execve(2) と exec(3) の仲間（execv・execvp・execvpe・execl・execle・execlp・fexecve）。
//
// 実行ファイルを入れ替えるのはカーネルの execve（browser_os_process.execve）である。同じ pid のまま、fd・
// プロセスグループ・親・子・無視しているシグナル・塞いでいるシグナル・保留・インターバルタイマーを新しいプログラムへ
// 渡し、ハンドラを置いていたシグナルは既定の動作に戻す。fd は close-on-exec（FD_CLOEXEC。O_CLOEXEC・SOCK_CLOEXEC・
// F_SETFD で立てる）の立ったものだけを閉じる。通ればカーネルが呼び出しの積み重なりごと捨てるので、ここへは戻らない。
// 入れ替えられなければ execve(2) と同じ errno（ENOENT・ENOTDIR・EACCES・ENOEXEC・ELOOP）を返し、fd を閉じずに
// 元のプログラムが続く。
//
// libc が持ち、カーネルが知らないものはここで渡す（posix_spawn と同じ。→ spawn.c）。
//
// - 作業ディレクトリ: cwd の持ち主は libc なので、getcwd() を渡す（相対パスの実行ファイルはそこから引かれる）
// - シグナル: 無視しているものと塞いでいるものを、呼ぶ前に __browseros_report_signals で知らせる
// - PATH: execvp・execvpe・execlp は呼んだプロセスの PATH を引いて、`/` のあるパスで 1 つずつ試す（musl と同じ）
//
// カーネルの execve は `/` の無い名前を PATH から探すので、execve(2) に `/` の無い名前が来たら `./` を付けて、
// 作業ディレクトリからの相対パスにする（execve は PATH を引かない）。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;

int execve(const char *path, char *const argv[], char *const envp[]) {
  if (!path || !*path) {
    errno = ENOENT;
    return -1;
  }
  if (strnlen(path, PATH_MAX) >= PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }
  // Linux と同じく、argv が空（NULL か argv[0] が NULL）でも起こす。新しいプログラムの argv[0] は空の文字列になる
  static char *const empty_argv[] = {"", 0};
  if (!argv || !argv[0])
    argv = empty_argv;
  // envp が NULL なら空の環境（Linux と同じ）
  static char *const empty_envp[] = {0};
  if (!envp)
    envp = empty_envp;

  char *cwd = getcwd(0, 0);
  if (!cwd)
    return -1;
  size_t argv_len = 0, env_len = 0;
  char *packed_argv = __browseros_pack_strings(argv, &argv_len);
  char *packed_env = __browseros_pack_strings(envp, &env_len);
  int bare = !strchr(path, '/');
  char *relative = 0;
  const char *file = path;
  if (bare) {
    size_t len = strlen(path);
    relative = malloc(len + 3);
    if (relative) {
      memcpy(relative, "./", 2);
      memcpy(relative + 2, path, len + 1);
      file = relative;
    }
  }
  int error = ENOMEM;
  if (packed_argv && packed_env && (!bare || relative)) {
    __browseros_report_signals();
    // 入れ替わると写しは消えるので、MAP_SHARED の写しに書いたものを先にファイルへ出す（→ memory_sync.c）
    __browseros_memory_sync();
    error = __browseros_execve(file, strlen(file), packed_argv, argv_len, packed_env, env_len, cwd,
                               strlen(cwd));
  }
  // ここへ来たのは入れ替えられなかったとき
  free(relative);
  free(packed_argv);
  free(packed_env);
  free(cwd);
  errno = error ? error : ENOEXEC;
  return -1;
}

int execv(const char *path, char *const argv[]) { return execve(path, argv, environ); }

int execvpe(const char *file, char *const argv[], char *const envp[]) {
  errno = ENOENT;
  if (!*file)
    return -1;
  if (strchr(file, '/'))
    return execve(file, argv, envp);
  const char *path = getenv("PATH");
  if (!path)
    path = "/usr/local/bin:/bin:/usr/bin";
  size_t k = strnlen(file, NAME_MAX + 1);
  if (k > NAME_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }
  size_t l = strnlen(path, PATH_MAX - 1) + 1;
  int seen_eacces = 0;
  for (const char *p = path, *z;; p = z) {
    char b[l + k + 1];
    z = strchrnul(p, ':');
    if ((size_t)(z - p) >= l) {
      if (!*z++)
        break;
      continue;
    }
    // PATH の空の要素は作業ディレクトリ（execve が `./` を付ける）
    memcpy(b, p, z - p);
    b[z - p] = '/';
    memcpy(b + (z - p) + (z > p), file, k + 1);
    execve(b, argv, envp);
    // 見つからない（ENOENT・ENOTDIR）なら次へ進み、権限で断られたものがあれば最後に EACCES（execvp(3)）
    switch (errno) {
    case EACCES:
      seen_eacces = 1;
      break;
    case ENOENT:
    case ENOTDIR:
      break;
    default:
      return -1;
    }
    if (!*z++)
      break;
  }
  errno = seen_eacces ? EACCES : ENOENT;
  return -1;
}

int execvp(const char *file, char *const argv[]) { return execvpe(file, argv, environ); }

/// 可変長の引数を argv にして呼ぶ（execl・execlp・execle）。`envp` を読むのは execle だけ
#define WITH_ARGV(argv0, envp_out, call)                                                          \
  do {                                                                                            \
    int argc;                                                                                     \
    va_list ap;                                                                                   \
    va_start(ap, argv0);                                                                          \
    for (argc = 1; va_arg(ap, const char *); argc++)                                              \
      ;                                                                                           \
    va_end(ap);                                                                                   \
    char *argv[argc + 1];                                                                         \
    va_start(ap, argv0);                                                                          \
    argv[0] = (char *)argv0;                                                                      \
    for (int i = 1; i <= argc; i++)                                                               \
      argv[i] = va_arg(ap, char *);                                                               \
    envp_out;                                                                                     \
    va_end(ap);                                                                                   \
    return call;                                                                                  \
  } while (0)

int execl(const char *path, const char *argv0, ...) {
  WITH_ARGV(argv0, (void)0, execv(path, argv));
}

int execlp(const char *file, const char *argv0, ...) {
  WITH_ARGV(argv0, (void)0, execvp(file, argv));
}

int execle(const char *path, const char *argv0, ...) {
  char **envp;
  WITH_ARGV(argv0, envp = va_arg(ap, char **), execve(path, argv, envp));
}

/// fd が指す実行ファイルで入れ替える。カーネルは fd から実行ファイルを起こす口（execveat(2) の AT_EMPTY_PATH）も
/// /proc/self/fd も持たないので、開いている fd には ENOSYS を返す（fexecve(3)）
int fexecve(int fd, char *const argv[], char *const envp[]) {
  (void)argv;
  (void)envp;
  if (fcntl(fd, F_GETFD) < 0)
    return -1;
  errno = ENOSYS;
  return -1;
}
