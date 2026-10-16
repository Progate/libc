// BrowserOS のカーネルの Linux の syscall の入口。
//
// wasip1 に無い syscall は、カーネルが `browser_os_linux.syscall(nr, a0..a5)` の 1 つの関数で受ける。番号・引数・
// 戻り値・フラグ・構造体は riscv32 の Linux のもの（rv32 の ILP32、time_t は 64 ビット。番号は asm-generic/unistd.h）で、
// 失敗は `-errno`（Linux の値）で返る。カーネルが知らない番号は `-ENOSYS` で返る。
//
// この libc のヘッダーの errno は WASI の値なので、`__browseros_syscall` は Linux の errno をこの libc の値に直して
// 返す（musl の `__syscall` と同じく、成功は 0 以上、失敗は `-errno`）。フラグの値が Linux と違うもの（`O_*`・`AT_*`）は、
// 呼ぶ側が Linux の値（下の `__BOS_*`）に直して渡す。
//
// カーネルの側は browser-os の src/wasi/linux/ にある。

#ifndef __BROWSEROS_LINUX_H
#define __BROWSEROS_LINUX_H

#include <stdint.h>

__attribute__((import_module("browser_os_linux"), import_name("syscall")))
int64_t __browseros_linux_syscall(int32_t nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4,
                                  int64_t a5);

/// syscall を呼ぶ。成功なら 0 以上、失敗なら `-errno`（この libc の値）
long __browseros_syscall(int nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5);

/// `__browseros_syscall` の戻り値を C の関数の戻り値にする（失敗なら errno を置いて -1）
long __browseros_syscall_ret(long r);

#define __bos_syscall0(nr) __browseros_syscall(nr, 0, 0, 0, 0, 0, 0)
#define __bos_syscall1(nr, a) __browseros_syscall(nr, (int64_t)(a), 0, 0, 0, 0, 0)
#define __bos_syscall2(nr, a, b) __browseros_syscall(nr, (int64_t)(a), (int64_t)(b), 0, 0, 0, 0)
#define __bos_syscall3(nr, a, b, c) __browseros_syscall(nr, (int64_t)(a), (int64_t)(b), (int64_t)(c), 0, 0, 0)
#define __bos_syscall4(nr, a, b, c, d) \
  __browseros_syscall(nr, (int64_t)(a), (int64_t)(b), (int64_t)(c), (int64_t)(d), 0, 0)
#define __bos_syscall5(nr, a, b, c, d, e) \
  __browseros_syscall(nr, (int64_t)(a), (int64_t)(b), (int64_t)(c), (int64_t)(d), (int64_t)(e), 0)

/// ポインターを引数にする（wasm32 の番地は符号なしの 32 ビット）
#define __bos_ptr(p) ((int64_t)(uintptr_t)(p))

// syscall の番号（riscv32 の Linux。musl の arch/riscv32/bits/syscall.h.in と同じ）
#define __BOS_SYS_dup 23
#define __BOS_SYS_dup3 24
#define __BOS_SYS_fcntl64 25
#define __BOS_SYS_ioctl 29
#define __BOS_SYS_flock 32
#define __BOS_SYS_fchmod 52
#define __BOS_SYS_fchmodat 53
#define __BOS_SYS_pipe2 59
#define __BOS_SYS_waitid 95
#define __BOS_SYS_kill 129
#define __BOS_SYS_tkill 130
#define __BOS_SYS_rt_sigsuspend 133
#define __BOS_SYS_rt_sigaction 134
#define __BOS_SYS_rt_sigprocmask 135
#define __BOS_SYS_rt_sigpending 136
#define __BOS_SYS_rt_sigreturn 139
#define __BOS_SYS_setpgid 154
#define __BOS_SYS_getpgid 155
#define __BOS_SYS_getsid 156
#define __BOS_SYS_setsid 157
#define __BOS_SYS_getgroups 158
#define __BOS_SYS_setgroups 159
#define __BOS_SYS_getpid 172
#define __BOS_SYS_getppid 173
#define __BOS_SYS_getuid 174
#define __BOS_SYS_geteuid 175
#define __BOS_SYS_getgid 176
#define __BOS_SYS_getegid 177
#define __BOS_SYS_gettid 178
#define __BOS_SYS_umask 166
#define __BOS_SYS_statx 291
#define __BOS_SYS_rt_sigtimedwait_time64 421

// この OS が足した syscall（Linux が使わない 1024 から。→ browser-os の src/wasi/linux/）
/// 配るシグナルを 1 つ受け取る（Linux ではカーネルがハンドラのフレームを積むところ。→ signal.c）
#define __BOS_SYS_browseros_sigdeliver 1024

// 値が Linux とこの libc で違う定数の、Linux の値
#define __BOS_O_NONBLOCK 04000
#define __BOS_O_CLOEXEC 02000000
#define __BOS_AT_FDCWD (-100)
#define __BOS_AT_SYMLINK_NOFOLLOW 0x100
#define __BOS_AT_EMPTY_PATH 0x1000
#define __BOS_F_DUPFD 0
#define __BOS_F_GETFD 1
#define __BOS_F_SETFD 2
#define __BOS_F_DUPFD_CLOEXEC 1030
#define __BOS_STATX_BASIC_STATS 0x7ffu
#define __BOS_TCGETS 0x5401
#define __BOS_TCSETS 0x5402
#define __BOS_TCSBRK 0x5409
#define __BOS_TCXONC 0x540a
#define __BOS_TCFLSH 0x540b
#define __BOS_TIOCGWINSZ 0x5413

/// struct statx（Linux の形。どのアーキテクチャでも同じ）
struct __bos_statx_timestamp {
  int64_t tv_sec;
  uint32_t tv_nsec;
  int32_t __reserved;
};

struct __bos_statx {
  uint32_t stx_mask;
  uint32_t stx_blksize;
  uint64_t stx_attributes;
  uint32_t stx_nlink;
  uint32_t stx_uid;
  uint32_t stx_gid;
  uint16_t stx_mode;
  uint16_t __spare0;
  uint64_t stx_ino;
  uint64_t stx_size;
  uint64_t stx_blocks;
  uint64_t stx_attributes_mask;
  struct __bos_statx_timestamp stx_atime, stx_btime, stx_ctime, stx_mtime;
  uint32_t stx_rdev_major, stx_rdev_minor, stx_dev_major, stx_dev_minor;
  uint64_t __spare2[14];
};

_Static_assert(sizeof(struct __bos_statx) == 0x100, "struct statx is 256 bytes");

struct stat;

/// statx(2) で引き、この libc の struct stat に直す。`flags` は Linux の値（`__BOS_AT_*`）。成功なら 0、失敗なら -1（errno）
int __browseros_statx(int dirfd, const char *path, int flags, struct stat *buf);

#endif
