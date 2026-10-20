/* BrowserOS: __clone(fn, stack, flags, arg, ptid, tls, ctid)（musl の src/thread/<arch>/clone.s と同じ口）。
 * wasm は新しい実行の流れを自分で作れないので、カーネルの clone(2) がゲストの export へ入り直させる。
 *
 * - CLONE_VM|CLONE_VFORK（posix_spawn の子）: カーネルは呼んだスレッドの上で同じインスタンスの
 *   __browseros_clone_child へ入り直す（→ @progate/browser-wasi の clone.ts）。子が execve(2) を通すか exit(2) を
 *   呼ぶと、カーネルは子の段の wasm のフレームを巻き戻して、親の clone から子の pid を返す。巻き戻したフレームの
 *   ぶん __stack_pointer が下がったままになるので、呼ぶ前の値に戻す。子は親のスタックの続きで走る。
 * - CLONE_THREAD（pthread_create）: カーネルは新しいワーカーで同じモジュールを共有メモリに立て、export の
 *   wasi_thread_start(tid, start) を呼ぶ（→ wasi_thread_start.s）。start は新しいスタックの頭に置く。 */

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
	/* __wasm_init_tls が struct pthread のある TLS 区画ごと .tdata の初めの値で潰すので、親が組んだ
	 * 中身をここに写して持ち越し、子が __browseros_thread_entry で戻す。一覧への登録（prev/next）は
	 * 親が clone のあとでやるが、潰しが終わってからでないと載せた繋がりが消えるので link_state で順序を取る
	 * （0 = 子がまだ潰し中・戻し中、1 = 戻し済み（親が登録してよい）、2 = 登録済み（子が走ってよい）） */
	volatile int link_state;
	struct pthread pt;
};

/* 親（__pthread_create）が子の thread_start までたどるための口。__clone がスタックの頭に置く場所と
 * 同じ計算をする（start は子のスタックにあり、共有メモリなので親からも読み書きできる）。
 * _REENTRANT ではない版でも __pthread_create から参照されるので外に出しておく */
__attribute__((__visibility__("hidden")))
volatile int *__browseros_thread_link_state(void *stack)
{
	uintptr_t top = (uintptr_t)stack & -16;
	struct thread_start *start = (void *)(top - sizeof *start);
	start = (void *)((uintptr_t)start & -16);
	return &start->link_state;
}

#ifdef _REENTRANT
void wasi_thread_start(int, struct thread_start *);
__attribute__((__used__)) static void *const keep_thread_start = wasi_thread_start;

__attribute__((__visibility__("hidden")))
void __browseros_thread_entry(int wasi_tid, struct thread_start *start)
{
	/* wasi_thread_start の第 1 引数は wasi-threads の番号（プロセスごとに 1 から振るワーカーの番号）で、Linux の tid
	 * ではない。mutex の持ち主・tkill(2)・robust list はカーネルの gettid(2) の番号で照らすので、self->tid にこれを
	 * 置くと主スレッドの tid（= pid）と重なったスレッドがある。そのスレッドが主スレッドの持つ ERRORCHECK の mutex を
	 * 取ると、自分が持っているとみなされて EDEADLK になり、libuv の uv_mutex_lock が abort する */
	(void)wasi_tid;
	int tid = __syscall(SYS_gettid);
	/* 親の clone が ptid に書くより先に子が走ることがあるので、子も書く（Linux はカーネルが子を走らせる前に書く） */
	if (start->ptid) a_store((volatile int *)start->ptid, tid);
	pthread_t self = __pthread_self();
	/* 直前に __wasm_init_tls が走って、struct pthread（__browseros_pthread_self。TLS 区画の中）を
	 * 初めの値（0 埋め）に戻した。親が clone 前に組んでおいた中身（map・stack・detach_state・
	 * robust_list・locale・canary・tsd など）を start->pt から戻す */
	struct pthread pt = start->pt;
	pt.self = self;
	pt.tid = tid;
	memcpy(self, &pt, sizeof pt);
	/* 潰しが終わったこと（= 一覧へ載せてよいこと）を親へ知らせ、親が登録を終えるまで待つ。
	 * 先に走って __pthread_exit へ入ると、prev/next が 0 のままの外す処理が 0 番地近辺へ書き込み、
	 * 自分が一覧から外れない（他スレッドが一覧を回るときに永久ループになる） */
	a_store((volatile int *)&start->link_state, 1);
	__wake((volatile int *)&start->link_state, 1, 1);
	for (;;) {
		int state = start->link_state;
		if (state == 2) break;
		__wait((volatile int *)&start->link_state, 0, state, 1);
	}
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
		/* tls は新しいスレッドの struct pthread（→ __init_tls.c の __copy_tls）。TLS の領域の頭はその手前 */
		start->tls_base = (char *)tls - ((char *)&__browseros_pthread_self - (char *)__builtin_wasm_tls_base());
		start->fn = fn;
		start->arg = arg;
		start->ptid = (flags & CLONE_PARENT_SETTID) ? ptid : 0;
		start->link_state = 0;
		/* 子は __wasm_init_tls が TLS 区画（struct pthread ごと）を潰してから走り始めるので、
		 * ここまで pthread_create が組んだ new の中身を持ち越す */
		memcpy(&start->pt, tls, sizeof start->pt);
		return __syscall(SYS_clone, flags, start, ptid, tls, ctid);
	}
#endif
	return -EINVAL;
}
