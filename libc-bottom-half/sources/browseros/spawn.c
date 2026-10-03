// posix_spawn(3) / posix_spawnp(3) と file actions。
//
// BrowserOS には fork も exec も無いので、子を起こすのはカーネルの posix_spawnp（browser_os_process.posix_spawnp）
// である。file actions と attr はバイト列にして渡し、カーネルが子の fd 表に順に当てる（子の fd は親の fd と同じ
// 開いたファイルの記述を指す）。並べ方は browser-os の process/wasi-spawn.ts と README「本物と同じ形で起こす」にある。
//
// libc が持つものはカーネルが知らないので、ここで足す。
//
// - 作業ディレクトリ: cwd の持ち主は libc なので、先頭に addchdir(getcwd()) を置く（相対パスの addopen・addchdir と
//   起こすもののパスは、そこから引かれる）
// - PATH: posix_spawnp は呼んだプロセスのいまの PATH を引いて、`/` のあるパスで 1 つずつ試す（musl・execvp(3) と同じ）
// - umask: addopen が作るファイルの権限は `mode & ~umask` にして渡す
//
// 子へ継がせられるのは fd 0 / 1 / 2 だけである（WASI の fd 3 からは preopen が並ぶ）。3 以上の fd を開いたまま子に
// 残す file actions は、カーネルが ENOTSUP で断る。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

enum { FDOP_CLOSE = 1, FDOP_DUP2, FDOP_OPEN, FDOP_CHDIR, FDOP_FCHDIR };

/// file actions の 1 つ。`posix_spawn_file_actions_t.__actions` は最後の 1 つを指し、`prev` で前へ辿る（musl と同じ形）
struct fdop {
  struct fdop *next, *prev;
  int cmd, fd, srcfd, oflag;
  mode_t mode;
  char path[];
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa) {
  fa->__actions = 0;
  return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa) {
  struct fdop *op = fa->__actions, *next;
  while (op) {
    next = op->prev;
    free(op);
    op = next;
  }
  return 0;
}

static void append(posix_spawn_file_actions_t *fa, struct fdop *op) {
  op->next = 0;
  if ((op->prev = fa->__actions))
    op->prev->next = op;
  fa->__actions = op;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd) {
  if (fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_CLOSE;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int srcfd, int fd) {
  if (srcfd < 0 || fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_DUP2;
  op->srcfd = srcfd;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *restrict fa, int fd,
                                     const char *restrict path, int flags, mode_t mode) {
  if (fd < 0)
    return EBADF;
  size_t len = strlen(path) + 1;
  struct fdop *op = malloc(sizeof *op + len);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_OPEN;
  op->fd = fd;
  op->oflag = flags;
  op->mode = mode;
  memcpy(op->path, path, len);
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *restrict fa,
                                         const char *restrict path) {
  size_t len = strlen(path) + 1;
  struct fdop *op = malloc(sizeof *op + len);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_CHDIR;
  op->fd = -1;
  memcpy(op->path, path, len);
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *fa, int fd) {
  if (fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_FCHDIR;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *attr) {
  *attr = (posix_spawnattr_t){0};
  return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr) {
  (void)attr;
  return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags) {
  const unsigned all = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
                       POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSCHEDPARAM |
                       POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_USEVFORK | POSIX_SPAWN_SETSID;
  if ((unsigned)flags & ~all)
    return EINVAL;
  attr->__flags = flags;
  return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *restrict attr, short *restrict flags) {
  *flags = attr->__flags;
  return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgrp) {
  attr->__pgrp = pgrp;
  return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict attr, pid_t *restrict pgrp) {
  *pgrp = attr->__pgrp;
  return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict attr, const sigset_t *restrict mask) {
  attr->__mask = *mask;
  return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict attr, sigset_t *restrict mask) {
  *mask = attr->__mask;
  return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict attr, const sigset_t *restrict def) {
  attr->__def = *def;
  return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict attr, sigset_t *restrict def) {
  *def = attr->__def;
  return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict attr,
                                  const struct sched_param *restrict param) {
  (void)attr;
  (void)param;
  return 0;
}

int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict attr,
                                  struct sched_param *restrict param) {
  (void)attr;
  (void)param;
  return 0;
}

int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy) {
  attr->__pol = policy;
  return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict attr, int *restrict policy) {
  *policy = attr->__pol;
  return 0;
}

/// NUL 区切りのバイト列にする（argv / env の渡し方。execve の形と同じ）
static char *pack(char *const strings[], size_t *len) {
  size_t total = 0;
  for (size_t i = 0; strings && strings[i]; i++)
    total += strlen(strings[i]) + 1;
  char *out = malloc(total ? total : 1);
  if (!out)
    return 0;
  size_t at = 0;
  for (size_t i = 0; strings && strings[i]; i++) {
    size_t n = strlen(strings[i]) + 1;
    memcpy(out + at, strings[i], n);
    at += n;
  }
  *len = total;
  return out;
}

/// file actions のバイト列を伸ばしながら書く先
struct buffer {
  unsigned char *data;
  size_t len, cap;
  int failed;
};

static void put(struct buffer *out, const void *bytes, size_t len) {
  if (out->failed)
    return;
  if (out->len + len > out->cap) {
    size_t cap = out->cap ? out->cap * 2 : 256;
    while (cap < out->len + len)
      cap *= 2;
    unsigned char *data = realloc(out->data, cap);
    if (!data) {
      out->failed = 1;
      return;
    }
    out->data = data;
    out->cap = cap;
  }
  memcpy(out->data + out->len, bytes, len);
  out->len += len;
}

/// file actions 1 件（op, fd, newfd, oflag, mode, path の長さ。すべて 32 ビット）と path を書く。次の件は 4 バイト境界から
static void put_action(struct buffer *out, uint32_t op, int32_t fd, int32_t newfd, uint32_t oflag,
                       uint32_t mode, const char *path) {
  uint32_t path_len = path ? strlen(path) : 0;
  uint32_t head[6] = {op, (uint32_t)fd, (uint32_t)newfd, oflag, mode, path_len};
  put(out, head, sizeof head);
  if (path_len) {
    static const unsigned char zeros[3];
    put(out, path, path_len);
    put(out, zeros, (4 - path_len % 4) % 4);
  }
}

/// file actions をバイト列にする。先頭に addchdir(cwd) を置き、子の作業ディレクトリを libc の cwd から始める
static int encode_actions(const posix_spawn_file_actions_t *fa, const char *cwd, struct buffer *out) {
  put_action(out, __BROWSEROS_SPAWN_CHDIR, -1, -1, 0, 0, cwd);
  struct fdop *op = fa ? fa->__actions : 0;
  // 最後の 1 つを指しているので、最初の 1 つへ戻ってから順に書く
  while (op && op->prev)
    op = op->prev;
  for (; op; op = op->next) {
    switch (op->cmd) {
    case FDOP_CLOSE:
      put_action(out, __BROWSEROS_SPAWN_CLOSE, op->fd, -1, 0, 0, 0);
      break;
    case FDOP_DUP2:
      put_action(out, __BROWSEROS_SPAWN_DUP2, op->srcfd, op->fd, 0, 0, 0);
      break;
    case FDOP_OPEN:
      put_action(out, __BROWSEROS_SPAWN_OPEN, op->fd, -1, op->oflag, __browseros_creation_mode(op->mode),
                 op->path);
      break;
    case FDOP_CHDIR:
      put_action(out, __BROWSEROS_SPAWN_CHDIR, -1, -1, 0, 0, op->path);
      break;
    case FDOP_FCHDIR:
      put_action(out, __BROWSEROS_SPAWN_FCHDIR, op->fd, -1, 0, 0, 0);
      break;
    default:
      return EINVAL;
    }
  }
  return out->failed ? ENOMEM : 0;
}

/// sigset_t を u64 にする（シグナル n はビット n-1。カーネルの attr の並び）
static uint64_t mask_of_set(const sigset_t *set) {
  uint64_t mask = 0;
  for (int sig = 1; sig < _NSIG && sig <= 64; sig++)
    if (sigismember(set, sig))
      mask |= (uint64_t)1 << (sig - 1);
  return mask;
}

/// カーネルへ渡す attr（flags・sigdefault・sigmask）
struct encoded_attr {
  uint32_t flags, pad;
  uint64_t sigdefault, sigmask;
};

/// 1 つのパスで起こす。file actions と attr はもうバイト列になっている
static int spawn_path(pid_t *pid, const char *path, const struct buffer *actions,
                      const struct encoded_attr *attr, const char *argv, size_t argv_len,
                      const char *env, size_t env_len) {
  int32_t child = 0;
  int error = __browseros_posix_spawnp(&child, path, strlen(path), actions->data, actions->len, attr,
                                       sizeof *attr, argv, argv_len, env, env_len);
  if (error)
    return error;
  if (pid)
    *pid = child;
  return 0;
}

/**
 * `dir` と `file` を繋いだパスで起こす。カーネルは `/` の無い名前を PATH から探すので、`dir` が空（PATH の空の要素・
 * posix_spawn に渡された相対の名前）なら `./` を付けて、子の作業ディレクトリからの相対パスにする
 */
static int spawn_in(pid_t *pid, const char *dir, size_t dir_len, const char *file,
                    const struct buffer *actions, const struct encoded_attr *attr, const char *argv,
                    size_t argv_len, const char *env, size_t env_len) {
  size_t file_len = strlen(file);
  char *path = malloc(dir_len + file_len + 3);
  if (!path)
    return ENOMEM;
  if (dir_len) {
    memcpy(path, dir, dir_len);
    path[dir_len] = '/';
    memcpy(path + dir_len + 1, file, file_len + 1);
  } else {
    memcpy(path, "./", 2);
    memcpy(path + 2, file, file_len + 1);
  }
  int error = spawn_path(pid, path, actions, attr, argv, argv_len, env, env_len);
  free(path);
  return error;
}

/**
 * 起こす。`search` なら `file` に `/` が無いとき、呼んだプロセスの PATH から 1 つずつ試す。
 * 見つからない（ENOENT・ENOTDIR）なら次へ進み、どれでも見つからなければ ENOENT、権限で断られたものがあれば EACCES
 * （musl の posix_spawnp・execvp(3) と同じ）
 */
static int spawn(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                 char *const argv[restrict], char *const envp[restrict], int search) {
  if (!*file)
    return ENOENT;
  if (strnlen(file, PATH_MAX) >= PATH_MAX)
    return ENAMETOOLONG;
  char *cwd = getcwd(0, 0);
  if (!cwd)
    return errno;
  struct buffer actions = {0};
  int error = encode_actions(fa, cwd, &actions);
  free(cwd);
  struct encoded_attr encoded = {0};
  if (attr) {
    encoded.flags = (uint16_t)attr->__flags;
    encoded.sigdefault = mask_of_set(&attr->__def);
    encoded.sigmask = mask_of_set(&attr->__mask);
  }
  size_t argv_len = 0, env_len = 0;
  char *packed_argv = error ? 0 : pack(argv, &argv_len);
  char *packed_env = error ? 0 : pack(envp ? envp : environ, &env_len);
  if (!error && (!packed_argv || !packed_env))
    error = ENOMEM;

  if (error) {
    // 組み立てに失敗した。何も起こさない
  } else if (strchr(file, '/')) {
    error = spawn_path(pid, file, &actions, &encoded, packed_argv, argv_len, packed_env, env_len);
  } else if (!search) {
    // posix_spawn の `path` は PATH を引かない。`/` の無い名前も作業ディレクトリからの相対パスである
    error = spawn_in(pid, "", 0, file, &actions, &encoded, packed_argv, argv_len, packed_env, env_len);
  } else {
    const char *path = getenv("PATH");
    if (!path)
      path = "/usr/local/bin:/bin:/usr/bin";
    int seen_eacces = 0;
    error = ENOENT;
    for (const char *p = path;; p++) {
      const char *z = strchrnul(p, ':');
      int tried = spawn_in(pid, p, z - p, file, &actions, &encoded, packed_argv, argv_len,
                           packed_env, env_len);
      if (tried == EACCES) {
        seen_eacces = 1;
      } else if (tried != ENOENT && tried != ENOTDIR) {
        error = tried;
        break;
      }
      if (!*z)
        break;
      p = z;
    }
    if (error == ENOENT && seen_eacces)
      error = EACCES;
  }
  free(packed_argv);
  free(packed_env);
  free(actions.data);
  return error;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                char *const argv[restrict], char *const envp[restrict]) {
  return spawn(pid, path, fa, attr, argv, envp, 0);
}

int posix_spawnp(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                 char *const argv[restrict], char *const envp[restrict]) {
  return spawn(pid, file, fa, attr, argv, envp, 1);
}
