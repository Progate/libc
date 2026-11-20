/* reactor（_start を持たず、export を呼ばれるモジュール）は、_initialize で libc とコンストラクタを用意する。
 * 入口は _start と同じく、カーネルの積んだ起動のスタックの頭を受け取る */
#define START "_initialize"

#include "crt_arch.h"
#include "crt_start.h"

hidden void _initialize_c(long *p)
{
	int argc = p[0];
	char **argv = (void *)(p+1);
	__browseros_init_libc(argv + argc + 1, argv[0]);
	__libc_start_init();
}
