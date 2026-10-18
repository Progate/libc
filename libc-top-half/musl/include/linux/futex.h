#ifndef _LINUX_FUTEX_H
#define _LINUX_FUTEX_H

/*
 * futex(2) の syscall 引数の定数。musl は Linux カーネルの UAPI ヘッダーを持たない
 * （実在の musl 配布では linux-headers パッケージが別に入る）が、BrowserOS の
 * カーネルは futex の取り決めを Linux と同じ形で持つので、libc++ などの
 * `<linux/futex.h>` を読むプログラムのために使う分だけここに置く。
 *
 * 番号は本物の Linux の UAPI（include/uapi/linux/futex.h）と同じ。
 * BrowserOS のカーネルが捌くのは WAIT / WAKE / WAIT_BITSET / WAKE_BITSET /
 * REQUEUE / CMP_REQUEUE で、PRIVATE と CLOCK_REALTIME のフラグは
 * ゲスト内で完結するのでカーネル側では無視される
 */

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_FD 2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_WAIT_REQUEUE_PI 11
#define FUTEX_CMP_REQUEUE_PI 12
#define FUTEX_LOCK_PI2 13
#define FUTEX_WAIT_BITSET_PI 14
#define FUTEX_LOCK_PI3 15
#define FUTEX_WAKE_PI 16

#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256

#define FUTEX_WAIT_PRIVATE (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#define FUTEX_REQUEUE_PRIVATE (FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG)
#define FUTEX_CMP_REQUEUE_PRIVATE (FUTEX_CMP_REQUEUE | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_OP_PRIVATE (FUTEX_WAKE_OP | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAIT_BITSET_PRIVATE (FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_BITSET_PRIVATE (FUTEX_WAKE_BITSET | FUTEX_PRIVATE_FLAG)

#define FUTEX_BITSET_MATCH_ANY 0xffffffff

#endif
