/* BrowserOS: すべての syscall はここから、カーネルの Linux の syscall の入口 browser_os_linux.syscall を呼ぶ
 * （→ @progate/browser-os の src/wasi/linux/）。番号・引数・戻り値は riscv32 の Linux のもの。
 *
 * wasm の作りのために、ここで 2 つだけ受け渡しを整える。
 *
 * - rt_sigaction: wasm の関数ポインタは関数の表の番号で、表の 1 番は SIG_IGN（1）と重なりうる。カーネルへは
 *   ハンドラの番号に最上位のビットを立てて渡し、受け取った古い動作からは外す。
 * - fork の形の clone（CLONE_VM なし）: wasm は自分のスタックを写せないので、カーネルは asyncify で書き出した
 *   呼び出しの積み重なりから親と子を巻き直す。書き出し先は使わない tls の引数で渡す（-lbrowseros-fork が持つ。
 *   無ければ ENOSYS）。巻き直しでは関数の頭が飛ばされ、子の __stack_pointer は初めの値になるので、呼ぶ前の
 *   値へ書き戻す。 */

#include <stdint.h>
#include <signal.h>
#include <string.h>
#include "syscall.h"
#include "ksigaction.h"

__attribute__((__import_module__("browser_os_linux"), __import_name__("syscall")))
int64_t __browseros_linux_syscall(int32_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

#ifdef __wasi__
/* シグナルの受け口（→ src/signal/wasm32/__browseros_signal.c）。ランタイムが export を呼ぶので、どのプログラムにも残す。
 * libc.so の道ではシグナルは syscall の戻り値の上の 32 ビットで届く（→ spec.md の 9）のでこの入り口は持たない */
int __browseros_signal(int);
__attribute__((__used__)) static int (*const keep_signal_entry)(int) = __browseros_signal;
#else
/* 積まれたシグナルの配り手（→ src/signal/wasm32/__browseros_signal.c） */
__attribute__((__visibility__("hidden"))) int __browseros_deliver_signals(int);
#endif

/* -lbrowseros-fork が持つ asyncify の書き出し先（→ src/process/wasm32/fork_data.c） */
extern __attribute__((__weak__)) void *__browseros_fork_data(void);

#define HANDLER_BIT 0x80000000UL
#define CLONE_VM 0x00000100

static unsigned long encode(unsigned long handler)
{
	return handler <= 1 ? handler : handler | HANDLER_BIT;
}

static unsigned long decode(unsigned long handler)
{
	return handler <= 1 ? handler : handler & ~HANDLER_BIT;
}

static inline uintptr_t stack_pointer(void)
{
	uintptr_t sp;
	__asm__ volatile(".globaltype __stack_pointer, i32\n"
	                 "global.get __stack_pointer\n"
	                 "local.set %0\n" : "=r"(sp));
	return sp;
}

static inline void set_stack_pointer(uintptr_t sp)
{
	__asm__ volatile(".globaltype __stack_pointer, i32\n"
	                 "local.get %0\n"
	                 "global.set __stack_pointer\n" : : "r"(sp));
}

static long call(long n, long a, long b, long c, long d, long e, long f)
{
	/* 数はどれも 32 ビットの long。カーネルは下の 32 ビットを読む */
	int64_t r = __browseros_linux_syscall(n, a, b, c, d, e, f);
#ifndef __wasi__
	/* シグナルが積まれているときは上の 32 ビットが立つ（→ spec.md の 9）。下の 32 ビットを結果として
	 * 返す前にハンドラを走らせる。syscall を呼ばずに回り続ける間は届かないので、既定の動作と
	 * SIGKILL・SIGSTOP はカーネルが届ける。
	 *
	 * 配る側（`__browseros_deliver_signals`）が呼ぶ browseros_sigdeliver と rt_sigreturn はこの検査を
	 * 外す。配るものがまだ残っているとこれらの戻り値にも上の 32 ビットが立つので、検査すると
	 * `__browseros_deliver_signals` が自分の syscall で再帰的に呼ばれ続け、ブロックされた保留がある間は
	 * 終わらない。 */
	if (n != 1024 /* SYS_browseros_sigdeliver */ && n != SYS_rt_sigreturn &&
	    (uint64_t)r >> 32) __browseros_deliver_signals(0);
#endif
	return (long)r;
}

long __browseros_syscall(long n, long a, long b, long c, long d, long e, long f)
{
	if (n == SYS_rt_sigaction) {
		struct k_sigaction ksa, *old = (void *)c;
		if (b) {
			memcpy(&ksa, (void *)b, sizeof ksa);
			ksa.handler = (void (*)(int))encode((unsigned long)ksa.handler);
			b = (long)&ksa;
		}
		long r = call(n, a, b, c, d, e, f);
		if (r == 0 && old) old->handler = (void (*)(int))decode((unsigned long)old->handler);
		return r;
	}
	if (n == SYS_clone && !(a & CLONE_VM)) {
		void *data = __browseros_fork_data ? __browseros_fork_data() : 0;
		if (!data) return -ENOSYS;
		uintptr_t sp = stack_pointer();
		long r = call(n, a, b, c, (long)data, e, f);
		if (r == 0) set_stack_pointer(sp);
		return r;
	}
	return call(n, a, b, c, d, e, f);
}
