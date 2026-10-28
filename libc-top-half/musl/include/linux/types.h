#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

/*
 * Linux カーネルの UAPI ヘッダーが使う型（include/uapi/linux/types.h・asm-generic/int-ll64.h）。musl は Linux の UAPI
 * ヘッダーを持たない（実在の musl 配布では linux-headers パッケージが別に入る）が、BrowserOS のカーネルは ALSA の
 * 取り決めを Linux と同じ形で持つので、<sound/asound.h> が読むぶんだけここに置く。
 *
 * 型の幅は Linux の asm-generic と同じ（__u64 は unsigned long long）。__bitwise は sparse の印で、
 * コンパイラーには空になる
 */

#include <asm/bitsperlong.h>

typedef signed char __s8;
typedef unsigned char __u8;
typedef signed short __s16;
typedef unsigned short __u16;
typedef signed int __s32;
typedef unsigned int __u32;
typedef signed long long __s64;
typedef unsigned long long __u64;

#ifndef __bitwise
#define __bitwise
#endif

typedef __u16 __bitwise __le16;
typedef __u16 __bitwise __be16;
typedef __u32 __bitwise __le32;
typedef __u32 __bitwise __be32;
typedef __u64 __bitwise __le64;
typedef __u64 __bitwise __be64;
typedef __u16 __bitwise __sum16;
typedef __u32 __bitwise __wsum;

#define __aligned_u64 __u64 __attribute__((aligned(8)))
#define __aligned_be64 __be64 __attribute__((aligned(8)))
#define __aligned_le64 __le64 __attribute__((aligned(8)))

typedef unsigned __bitwise __poll_t;

typedef long __kernel_long_t;
typedef unsigned long __kernel_ulong_t;
typedef __kernel_long_t __kernel_off_t;
typedef long long __kernel_loff_t;
typedef long long __kernel_time64_t;
typedef int __kernel_pid_t;

#endif
