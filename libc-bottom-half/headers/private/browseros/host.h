// BrowserOS のカーネルが渡すホスト関数（wasip1 の ABI に無い syscall）。
//
// BrowserOS（@progate/browser-os）は、WASI preview1 に無い fork / exec / pipe / dup / flock / chmod などを
// `browser_os_fs` / `browser_os_process` の名前空間のホスト関数として渡す。この libc はそれを使って
// POSIX の関数（posix_spawn・pipe・dup2・waitpid・flock・chmod・stat の権限のビット・SIGPIPE）を本物の
// 意味論で持つ。戻り値はどれも WASI の errno で、wasi-libc の errno の値は WASI の値そのものなので、
// そのまま errno に置ける。
//
// ホスト関数の中身と意味論は browser-os の src/wasi/fs.ts と src/process/wasi-spawn.ts にある。

#ifndef __BROWSEROS_HOST_H
#define __BROWSEROS_HOST_H

#include <stdint.h>
#include <wasi/api.h>

#define __BROWSEROS_IMPORT(module, name) \
  __attribute__((import_module(module), import_name(name)))

// browser_os_fs

__BROWSEROS_IMPORT("browser_os_fs", "dup")
int32_t __browseros_dup(int32_t fd, int32_t lowest, int32_t *newfd);
__BROWSEROS_IMPORT("browser_os_fs", "dup2")
int32_t __browseros_dup2(int32_t fd, int32_t newfd);
__BROWSEROS_IMPORT("browser_os_fs", "flock")
int32_t __browseros_flock(int32_t fd, int32_t operation);
// fstat / fstatat。WASI の filestat と、filestat に無い st_mode（種類と権限のビット）を 1 度の問い合わせで返す
__BROWSEROS_IMPORT("browser_os_fs", "fstat")
int32_t __browseros_fstat(int32_t fd, __wasi_filestat_t *filestat, uint32_t *mode);
__BROWSEROS_IMPORT("browser_os_fs", "stat_at")
int32_t __browseros_stat_at(int32_t dirfd, const char *path, uint32_t path_len, __wasi_filestat_t *filestat,
                            uint32_t *mode);
__BROWSEROS_IMPORT("browser_os_fs", "fchmod")
int32_t __browseros_fchmod(int32_t fd, uint32_t mode);
__BROWSEROS_IMPORT("browser_os_fs", "chmod_at")
int32_t __browseros_chmod_at(int32_t dirfd, const char *path, uint32_t path_len, uint32_t mode);

// browser_os_tty（端末。state は ICANON を落としているか・ECHO・ISIG の i32 3 つ）

__BROWSEROS_IMPORT("browser_os_tty", "tcgetattr")
int32_t __browseros_tcgetattr(int32_t fd, int32_t *state);
__BROWSEROS_IMPORT("browser_os_tty", "tcsetattr")
int32_t __browseros_tcsetattr(int32_t fd, int32_t actions, const int32_t *state);
__BROWSEROS_IMPORT("browser_os_tty", "ioctl")
int32_t __browseros_tty_ioctl(int32_t fd, int32_t request, void *arg);

// browser_os_process

/// pipe2(2)。flags は O_NONBLOCK だけ
__BROWSEROS_IMPORT("browser_os_process", "pipe2")
int32_t __browseros_pipe2(int32_t *fds, int32_t flags);
/// posix_spawnp(3) と同じ形。file actions と attr はバイト列で渡す（→ spawn.c）。argv と envp は NUL 区切りのバイト列。
/// `file` に `/` があればそのパスを起こす（この libc は PATH を自分で引き、いつも `/` のあるパスで呼ぶ）
__BROWSEROS_IMPORT("browser_os_process", "posix_spawnp")
int32_t __browseros_posix_spawnp(int32_t *pid, const char *file, uint32_t file_len,
                                 const void *actions, uint32_t actions_len, const void *attr,
                                 uint32_t attr_len, const char *argv, uint32_t argv_len,
                                 const char *envp, uint32_t envp_len);
__BROWSEROS_IMPORT("browser_os_process", "wait")
int32_t __browseros_wait(int32_t pid, int32_t options, int32_t *status, int32_t *child);
__BROWSEROS_IMPORT("browser_os_process", "kill")
int32_t __browseros_kill(int32_t pid, int32_t signal);
// このプロセスの利用者とグループの番号（BrowserOS の id と同じ決め方）
__BROWSEROS_IMPORT("browser_os_process", "credentials")
int32_t __browseros_credentials(uint32_t *uid, uint32_t *gid);
__BROWSEROS_IMPORT("browser_os_process", "getpid")
int32_t __browseros_getpid(int32_t *pid);
__BROWSEROS_IMPORT("browser_os_process", "getppid")
int32_t __browseros_getppid(int32_t *pid);
/// いま無視しているシグナルと塞いでいるシグナル（シグナル n はビット n-1）を知らせる。posix_spawn の子へ継ぐ扱いはここから決まる
__BROWSEROS_IMPORT("browser_os_process", "set_signals")
int32_t __browseros_set_signals(uint64_t ignored, uint64_t blocked);
/// このプロセスが始めるときに無視するシグナルと塞ぐシグナル（親の扱いと posix_spawn の attr からカーネルが決めたもの）
__BROWSEROS_IMPORT("browser_os_process", "initial_signals")
int32_t __browseros_initial_signals(uint64_t *ignored, uint64_t *blocked);
/// 戻らない（シグナルの既定の動作で終わる）。ホストが渡していなければ ENOSYS で戻る
__BROWSEROS_IMPORT("browser_os_process", "exit_signal")
int32_t __browseros_exit_signal(int32_t signal);

/// posix_spawnp の file actions の種類（バイト列の op。browser-os の process/wasi-spawn.ts と同じ番号）
#define __BROWSEROS_SPAWN_OPEN 1
#define __BROWSEROS_SPAWN_CLOSE 2
#define __BROWSEROS_SPAWN_DUP2 3
#define __BROWSEROS_SPAWN_CHDIR 4
#define __BROWSEROS_SPAWN_FCHDIR 5

#endif
