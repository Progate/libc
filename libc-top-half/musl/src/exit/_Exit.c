#include <stdlib.h>
#include "syscall.h"

#ifdef __wasilibc_browseros
// MAP_SHARED の写しに書いたものを、プロセスが終わる前にファイルへ出す（→ libc-bottom-half/sources/browseros/memory_sync.c）
void __browseros_memory_sync(void);
#endif

_Noreturn void _Exit(int ec)
{
#ifdef __wasilibc_browseros
	__browseros_memory_sync();
#endif
	__syscall(SYS_exit_group, ec);
	for (;;) __syscall(SYS_exit, ec);
}
