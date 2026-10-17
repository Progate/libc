/* BrowserOS: 取り消しの点になる syscall（musl の src/thread/<arch>/syscall_cp.s と同じ口）。取り消しを
 * 頼まれていれば syscall の前に __cancel へ進む。wasm にはプログラムカウンタが無いので、非同期に届いた
 * SIGCANCEL は次の取り消しの点で拾う（__cp_begin・__cp_end は pthread_cancel.c が番地を比べるための印） */
#include "pthread_impl.h"
#include "syscall.h"

hidden long __cancel(void);

hidden const char __cp_begin[1], __cp_end[1], __cp_cancel[1];

hidden long __syscall_cp_asm(volatile void *cancel, syscall_arg_t nr,
                             syscall_arg_t u, syscall_arg_t v, syscall_arg_t w,
                             syscall_arg_t x, syscall_arg_t y, syscall_arg_t z)
{
	if (*(volatile int *)cancel) return __cancel();
	return __syscall(nr, u, v, w, x, y, z);
}
