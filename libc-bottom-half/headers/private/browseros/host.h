// BrowserOS のカーネルが前に渡していたホスト関数（`browser_os_fs` / `browser_os_process` の名前空間）の宣言。
//
// この libc はもうこれらを呼ばない。カーネルを呼ぶのは Linux の syscall の入口だけである（→ browseros/linux.h）。
// カーネルは publish 済みのプログラムのためにこれらを凍結して残している。戻り値は WASI の errno で、この libc の
// errno（Linux の値）とは違うので、呼ぶなら errno に置く前に直すこと。ここに残るのは、file actions のバイト列などの
// 並べ方を表す定数と型を共有するためである。

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
// fcntl(2) の F_GETFD / F_SETFD。fd の印（FD_CLOEXEC）は fd 表の持ち主のカーネルが fd の番号ごとに持ち、execve が
// 通ったときに印の立った fd を閉じる。dup・dup2 で作った番号には立たない。この口を持たないカーネルでは ENOSYS
__BROWSEROS_IMPORT("browser_os_fs", "getfd")
int32_t __browseros_getfd(int32_t fd, int32_t *flags);
__BROWSEROS_IMPORT("browser_os_fs", "setfd")
int32_t __browseros_setfd(int32_t fd, int32_t flags);
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
// fchdir(2) のカーネル側。fd が指すディレクトリのいまの絶対パスを返す（作業ディレクトリの持ち主は libc なので、
// 移るのは libc の chdir）。名前を失ったディレクトリは ENOENT
__BROWSEROS_IMPORT("browser_os_fs", "fchdir")
int32_t __browseros_fchdir(int32_t fd, char *path, uint32_t size, uint32_t *length);

// browser_os_tty（端末。state は ICANON を落としているか・ECHO・ISIG の i32 3 つ）

__BROWSEROS_IMPORT("browser_os_tty", "tcgetattr")
int32_t __browseros_tcgetattr(int32_t fd, int32_t *state);
__BROWSEROS_IMPORT("browser_os_tty", "tcsetattr")
int32_t __browseros_tcsetattr(int32_t fd, int32_t actions, const int32_t *state);
__BROWSEROS_IMPORT("browser_os_tty", "ioctl")
int32_t __browseros_tty_ioctl(int32_t fd, int32_t request, void *arg);
/// 出力の改行変換（OPOST かつ ONLCR。書いた `\n` を端末が `\r\n` にするか）。ICANON とは別の設定なので、
/// state の 3 つとは別の口にしてある。この口を持たないカーネルでは ENOSYS が返り、変換は ICANON に連動したままになる
__BROWSEROS_IMPORT("browser_os_tty", "set_onlcr")
int32_t __browseros_tty_set_onlcr(int32_t fd, int32_t enabled);
__BROWSEROS_IMPORT("browser_os_tty", "get_onlcr")
int32_t __browseros_tty_get_onlcr(int32_t fd, int32_t *enabled);

// browser_os_process

/// pipe2(2)。flags は O_NONBLOCK と O_CLOEXEC（この libc は O_NONBLOCK だけを渡し、O_CLOEXEC は setfd で立てる）
__BROWSEROS_IMPORT("browser_os_process", "pipe2")
int32_t __browseros_pipe2(int32_t *fds, int32_t flags);
/// posix_spawnp(3) と同じ形。file actions と attr はバイト列で渡す（→ spawn.c）。argv と envp は NUL 区切りのバイト列。
/// `file` に `/` があればそのパスを起こす（この libc は PATH を自分で引き、いつも `/` のあるパスで呼ぶ）
__BROWSEROS_IMPORT("browser_os_process", "posix_spawnp")
int32_t __browseros_posix_spawnp(int32_t *pid, const char *file, uint32_t file_len,
                                 const void *actions, uint32_t actions_len, const void *attr,
                                 uint32_t attr_len, const char *argv, uint32_t argv_len,
                                 const char *envp, uint32_t envp_len);
/// execve(2)。呼んだプロセスの実行ファイルを入れ替える。**通れば戻らない**（カーネルが呼び出しの積み重なりごと捨てる）。
/// fd 表はそのまま残り、close-on-exec の立った fd だけが閉じる。通らなければ fd は閉じない。
/// argv と envp は NUL 区切りのバイト列、`cwd` は libc が持っている作業ディレクトリ。`file` に `/` が無ければカーネルは
/// PATH から探すので、この libc はいつも `/` のあるパスで呼ぶ（→ exec.c）。戻れば errno
__BROWSEROS_IMPORT("browser_os_process", "execve")
int32_t __browseros_execve(const char *file, uint32_t file_len, const char *argv, uint32_t argv_len,
                           const char *envp, uint32_t envp_len, const char *cwd, uint32_t cwd_len);
/// setitimer(2)。時間はマイクロ秒。`old` には置き換える前の残りと間隔（u64 の 2 つ）を書く。満了したらカーネルが
/// SIGALRM を送る。受け付けるのは ITIMER_REAL だけで、ほかは EINVAL（→ itimer.c）
__BROWSEROS_IMPORT("browser_os_process", "setitimer")
int32_t __browseros_setitimer(int32_t which, uint64_t value, uint64_t interval, uint64_t *old);
/// getitimer(2)。いまの残りと間隔（マイクロ秒の u64 の 2 つ）を `current` に書く
__BROWSEROS_IMPORT("browser_os_process", "getitimer")
int32_t __browseros_getitimer(int32_t which, uint64_t *current);
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
/// プロセスグループ（getpgid(2)。pid 0 は自分）
__BROWSEROS_IMPORT("browser_os_process", "getpgid")
int32_t __browseros_getpgid(int32_t pid, int32_t *pgid);
/// プロセスグループを変える（setpgid(2)）
__BROWSEROS_IMPORT("browser_os_process", "setpgid")
int32_t __browseros_setpgid(int32_t pid, int32_t pgid);
/// ハンドラを置いているシグナル（シグナル n はビット n-1）を知らせる。カーネルは送るときにこれを見て、置いてあるシグナルは
/// 既定の動作の代わりに保留へ積み、止まっている syscall を EINTR で起こす。syscall から戻るところでランタイムが
/// __browseros_signal を呼んでハンドラを走らせる（→ signal.c）
__BROWSEROS_IMPORT("browser_os_process", "set_handlers")
int32_t __browseros_set_handlers(uint64_t caught);
/// 戻らない（シグナルの既定の動作で終わる）。ホストが渡していなければ ENOSYS で戻る
__BROWSEROS_IMPORT("browser_os_process", "exit_signal")
int32_t __browseros_exit_signal(int32_t signal);

/// fork(2)。呼んだプロセスを写して子を作り、親には子の pid、子には 0 を `pid` に書く。積み重なりを写すために、
/// プログラムは asyncify（`wasm-opt --asyncify`）で組んである必要がある（組んでいなければ ENOSYS）。`asyncify_data` は
/// asyncify が積み重なりを書き出す場所で、先頭の 2 語が「いまの位置」と「終わり」（→ fork/fork.c）
__BROWSEROS_IMPORT("browser_os_process", "fork")
int32_t __browseros_fork(void *asyncify_data, int32_t *pid);

/// posix_spawnp の file actions の種類（バイト列の op。browser-os の process/wasi-spawn.ts と同じ番号）
#define __BROWSEROS_SPAWN_OPEN 1
#define __BROWSEROS_SPAWN_CLOSE 2
#define __BROWSEROS_SPAWN_DUP2 3
#define __BROWSEROS_SPAWN_CHDIR 4
#define __BROWSEROS_SPAWN_FCHDIR 5

// browser_os_sem（名前つきセマフォ。値と待ち手はカーネルの表にある。→ browser-os の src/process/semaphores.ts）
//
// open の oflag は 1 が O_CREAT、2 が O_EXCL（libc の O_* の値ではない）。名前は先頭の `/` を外したもの。timedwait の deadline は CLOCK_REALTIME のナノ秒（sem_timedwait の abstime）で、
// 負なら期限なし。待っているあいだにハンドラのあるシグナルが届けば EINTR で戻り、SA_RESTART でもやり直さない

__BROWSEROS_IMPORT("browser_os_sem", "open")
int32_t __browseros_ksem_open(const char *name, uint32_t name_len, int32_t oflag, uint32_t value, uint32_t *handle,
                              uint32_t *id);
__BROWSEROS_IMPORT("browser_os_sem", "close")
int32_t __browseros_ksem_close(uint32_t handle);
__BROWSEROS_IMPORT("browser_os_sem", "unlink")
int32_t __browseros_ksem_unlink(const char *name, uint32_t name_len);
__BROWSEROS_IMPORT("browser_os_sem", "timedwait")
int32_t __browseros_ksem_timedwait(uint32_t handle, int64_t deadline);
__BROWSEROS_IMPORT("browser_os_sem", "trywait")
int32_t __browseros_ksem_trywait(uint32_t handle);
__BROWSEROS_IMPORT("browser_os_sem", "post")
int32_t __browseros_ksem_post(uint32_t handle);
__BROWSEROS_IMPORT("browser_os_sem", "getvalue")
int32_t __browseros_ksem_getvalue(uint32_t handle, int32_t *value);

#endif
