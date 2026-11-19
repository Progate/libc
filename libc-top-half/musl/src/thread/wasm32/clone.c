/* BrowserOS: __clone(fn, stack, flags, arg, ptid, tls, ctid)（musl の src/thread/<arch>/clone.s と同じ口）。
 * wasm は新しい実行の流れを自分で作れないので、カーネルの clone(2) がゲストの export へ入り直させる。
 *
 * - CLONE_VM|CLONE_VFORK（posix_spawn の子）: カーネルは呼んだスレッドの上で同じインスタンスの
 *   __browseros_clone_child へ入り直す（→ @progate/browser-wasi の clone.ts）。子が execve(2) を通すか exit(2) を
 *   呼ぶと、カーネルは子の段の wasm のフレームを巻き戻して、親の clone から子の pid を返す。巻き戻したフレームの
 *   ぶん __stack_pointer が下がったままになるので、呼ぶ前の値に戻す。子は親のスタックの続きで走る。
 * - CLONE_THREAD（pthread_create）: カーネルは新しいワーカーで同じモジュールを共有メモリに立て、export の
 *   wasi_thread_start(tid, start) を呼ぶ（→ wasi_thread_start.s）。start は新しいスタックの頭に置く。
 *   親は子が走り始めるのを待たずに戻る（Linux の clone(2) と同じ）。 */

#define _GNU_SOURCE
#include <stdint.h>
#include <stdarg.h>
#include <sched.h>
#include <string.h>
#include "pthread_impl.h"
#include "syscall.h"

static _Thread_local int (*child_fn)(void *);
static _Thread_local void *child_arg;

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

/* clone の子の入口（カーネルが、clone を呼んだスレッドの上で呼ぶ）。fn(arg) の戻り値で子を終える */
__attribute__((__export_name__("__browseros_clone_child")))
void __browseros_clone_child(void)
{
	__syscall(SYS_exit, child_fn(child_arg));
}

/* 新しいスレッドの始まり（→ wasi_thread_start.s）。オフセット 0 と 4 は wasi_thread_start が読む */
struct thread_start {
	void *stack;
	void *tls_base;
	int (*fn)(void *);
	void *arg;
	volatile int *ptid;
	/* 子の struct pthread（pthread_create が TLS の雛形の外に組んだもの。→ src/env/wasm32/__init_tls.c） */
	struct pthread *self;
};

#ifdef _REENTRANT
void wasi_thread_start(int, struct thread_start *);
__attribute__((__used__)) static void *const keep_thread_start = wasi_thread_start;

extern _Thread_local struct pthread *__browseros_pthread_self;

__attribute__((__visibility__("hidden")))
void __browseros_thread_entry(int wasi_tid, struct thread_start *start)
{
	/* 直前に __wasm_init_tls がこのスレッドの区画を雛形の初めの値で埋めたので、スレッドポインタは主スレッドを
	 * 指している。libc を呼ぶ前に、親が組んだこのスレッドの struct pthread へ置き直す */
	__browseros_pthread_self = start->self;
	/* wasi_thread_start の第 1 引数は wasi-threads の番号（プロセスごとに 1 から振るワーカーの番号）で、Linux の tid
	 * ではない。mutex の持ち主・tkill(2)・robust list はカーネルの gettid(2) の番号で照らすので、self->tid にこれを
	 * 置くと主スレッドの tid（= pid）と重なったスレッドがある。そのスレッドが主スレッドの持つ ERRORCHECK の mutex を
	 * 取ると、自分が持っているとみなされて EDEADLK になり、libuv の uv_mutex_lock が abort する */
	(void)wasi_tid;
	int tid = __syscall(SYS_gettid);
	/* 親の clone が ptid に書くより先に子が走ることがあるので、子も書く（Linux はカーネルが子を走らせる前に書く） */
	if (start->ptid) a_store((volatile int *)start->ptid, tid);
	__syscall(SYS_exit, start->fn(start->arg));
}
#endif

int __clone(int (*fn)(void *), void *stack, int flags, void *arg, ...)
{
	void *ptid = 0, *tls = 0, *ctid = 0;
	va_list ap;
	va_start(ap, arg);
	ptid = va_arg(ap, void *);
	tls = va_arg(ap, void *);
	ctid = va_arg(ap, void *);
	va_end(ap);

	if ((flags & (CLONE_VM | CLONE_VFORK)) == (CLONE_VM | CLONE_VFORK)) {
		/* 子の段の中でもう 1 度 clone することがあるので、前の fn と arg を控えて戻す */
		int (*outer_fn)(void *) = child_fn;
		void *outer_arg = child_arg;
		child_fn = fn;
		child_arg = arg;
		uintptr_t sp = stack_pointer();
		long r = __syscall(SYS_clone, flags, stack, ptid, tls, ctid);
		set_stack_pointer(sp);
		child_fn = outer_fn;
		child_arg = outer_arg;
		return r;
	}

#ifdef _REENTRANT
	if (flags & CLONE_THREAD) {
		uintptr_t top = (uintptr_t)stack & -16;
		struct thread_start *start = (void *)(top - sizeof *start);
		start = (void *)((uintptr_t)start & -16);
		start->stack = start;
		/* tls は新しいスレッドの struct pthread（→ __init_tls.c の __copy_tls）。静的 TLS の区画はその手前で、
		 * カーネルへはその頭を渡す（ローダーは各モジュールの TLS をそこから並べる） */
		start->tls_base = __browseros_tls_block(tls);
		start->fn = fn;
		start->arg = arg;
		start->ptid = (flags & CLONE_PARENT_SETTID) ? ptid : 0;
		start->self = tls;
		return __syscall(SYS_clone, flags, start, ptid, start->tls_base, ctid);
	}
#endif
	return -EINVAL;
}
