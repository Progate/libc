#include <unistd.h>
#include <signal.h>
#include "syscall.h"
#include "libc.h"
#include "lock.h"
#include "pthread_impl.h"
#include "aio_impl.h"
#include "fork_impl.h"

static void dummy(int x) { }
weak_alias(dummy, __aio_atfork);

void __post_Fork(int ret)
{
	if (!ret) {
		pthread_t self = __pthread_self();
		self->tid = __syscall(SYS_set_tid_address, &__thread_list_lock);
		self->robust_list.off = 0;
		self->robust_list.pending = 0;
		self->next = self->prev = self;
		__thread_list_lock = 0;
		libc.threads_minus_1 = 0;
		if (libc.need_locks) libc.need_locks = -1;
	}
	UNLOCK(__abort_lock);
	if (!ret) __aio_atfork(1);
}

/* clone の fork の形が使う asyncify の書き出し先（同じライブラリの wasm32/fork_data.c）。syscall.c 側の参照は
 * 弱いのでリンクには残らず、ここ（fork(2)・_Fork(2) が必ず通る）から強く参照して取り込む。取り込めなければ
 * fork の形の clone は ENOSYS を返す（→ src/internal/wasm32/syscall.c） */
void *__browseros_fork_data(void);
__attribute__((__used__)) static void *(*const keep_fork_data)(void) = __browseros_fork_data;

pid_t _Fork(void)
{
	pid_t ret;
	sigset_t set;
	__block_all_sigs(&set);
	LOCK(__abort_lock);
#ifdef SYS_fork
	ret = __syscall(SYS_fork);
#else
	ret = __syscall(SYS_clone, SIGCHLD, 0);
#endif
	__post_Fork(ret);
	__restore_sigs(&set);
	return __syscall_ret(ret);
}
