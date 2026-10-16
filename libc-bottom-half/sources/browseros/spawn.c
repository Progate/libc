// posix_spawn(3) / posix_spawnp(3) と file actions。
//
// 子を起こすのはカーネルの browseros_posix_spawn（この OS が足した syscall。wasm は自分を写せないので Linux の
// clone(2) + execve(2) の形にできず、macOS の XNU が syscall として持つ posix_spawn を前例にする）である。file actions と
// attr はバイト列にして渡し、カーネルが子の fd 表に順に当てる（子の fd は親の fd と同じ開いたファイルの記述を指す）。
// 並べ方は browser-os の process/wasi-spawn.ts と README「本物と同じ形で起こす」にある。argv・envp は `char *` の並びの
// まま渡す。
//
// - 作業ディレクトリ: カーネルが持つ。子は親のいまの作業ディレクトリで始まり、相対パスの addopen・addchdir と起こす
//   もののパスはそこから引かれる
// - PATH: posix_spawnp は呼んだプロセスのいまの PATH を引いて、`/` のあるパスで 1 つずつ試す（musl・execvp(3) と同じ）
// - umask: addopen が作るファイルの権限は `mode & ~umask` にして渡す
//
// 子へ継がせられるのは fd 0 / 1 / 2 だけである（WASI の fd 3 からは preopen が並ぶ）。3 以上の fd を開いたまま子に
// 残す file actions は、カーネルが ENOTSUP で断る。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/linux.h>
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

/// file actions をバイト列にする（子の作業ディレクトリは、カーネルが持っている親の作業ディレクトリから始まる）
static int encode_actions(const posix_spawn_file_actions_t *fa, struct buffer *out) {
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

/// カーネルへ渡す attr（flags・sigdefault・sigmask・pgroup）
struct encoded_attr {
  uint32_t flags, pad;
  uint64_t sigdefault, sigmask;
  int32_t pgroup, pad2;
};

/// カーネルの browseros_posix_spawn への頼み（→ browser-os の src/wasi/linux/process.ts）
struct spawn_request {
  uint32_t path, actions, actions_len, attr, attr_len, argv, envp;
};

/// 1 つのパスで起こす。file actions と attr はもうバイト列になっている
static int spawn_path(pid_t *pid, const char *path, const struct buffer *actions, const struct encoded_attr *attr,
                      char *const argv[], char *const envp[]) {
  int32_t child = 0;
  struct spawn_request request = {
      .path = (uintptr_t)path,
      .actions = (uintptr_t)actions->data,
      .actions_len = actions->len,
      .attr = (uintptr_t)attr,
      .attr_len = sizeof *attr,
      .argv = (uintptr_t)argv,
      .envp = (uintptr_t)envp,
  };
  long r = __bos_syscall2(__BOS_SYS_browseros_posix_spawn, __bos_ptr(&child), __bos_ptr(&request));
  if (r < 0)
    return -r;
  if (pid)
    *pid = child;
  return 0;
}

/**
 * `dir` と `file` を繋いだパスで起こす。カーネルは `/` の無い名前を PATH から探すので、`dir` が空（PATH の空の要素・
 * posix_spawn に渡された相対の名前）なら `./` を付けて、子の作業ディレクトリからの相対パスにする
 */
static int spawn_in(pid_t *pid, const char *dir, size_t dir_len, const char *file,
                    const struct buffer *actions, const struct encoded_attr *attr, char *const argv[],
                    char *const envp[]) {
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
  int error = spawn_path(pid, path, actions, attr, argv, envp);
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
  struct buffer actions = {0};
  int error = encode_actions(fa, &actions);
  // 子へ継ぐシグナルの扱いはカーネルが親の扱い（カーネルが持っている）と attr から決める
  struct encoded_attr encoded = {0};
  if (attr) {
    encoded.flags = (uint16_t)attr->__flags;
    encoded.sigdefault = mask_of_set(&attr->__def);
    encoded.sigmask = mask_of_set(&attr->__mask);
    encoded.pgroup = attr->__pgrp;
  }
  char *const *env = envp ? envp : environ;

  if (error) {
    // 組み立てに失敗した。何も起こさない
  } else if (strchr(file, '/')) {
    error = spawn_path(pid, file, &actions, &encoded, argv, env);
  } else if (!search) {
    // posix_spawn の `path` は PATH を引かない。`/` の無い名前も作業ディレクトリからの相対パスである
    error = spawn_in(pid, "", 0, file, &actions, &encoded, argv, env);
  } else {
    const char *path = getenv("PATH");
    if (!path)
      path = "/usr/local/bin:/bin:/usr/bin";
    int seen_eacces = 0;
    error = ENOENT;
    for (const char *p = path;; p++) {
      const char *z = strchrnul(p, ':');
      int tried = spawn_in(pid, p, z - p, file, &actions, &encoded, argv, env);
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
  free(actions.data);
  return error;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                char *const argv[restrict], char *const envp[restrict]) {
  // 子が始まる前に、MAP_SHARED の写しに書いたものを出しておく（子は写しをファイルから読む。→ memory_sync.c）
  __browseros_memory_sync();
  return spawn(pid, path, fa, attr, argv, envp, 0);
}

int posix_spawnp(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *fa, const posix_spawnattr_t *restrict attr,
                 char *const argv[restrict], char *const envp[restrict]) {
  __browseros_memory_sync();
  return spawn(pid, file, fa, attr, argv, envp, 1);
}
