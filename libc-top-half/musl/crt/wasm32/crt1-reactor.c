/* reactor（_start を持たず、export を呼ばれるモジュール）は、_initialize で libc とコンストラクタを用意する */
#include "crt_start.h"

__attribute__((__export_name__("_initialize")))
void _initialize(void)
{
	int argc;
	char **argv = build_start_block(&argc);
	__browseros_init_libc(argv + argc + 1, argv[0]);
	__libc_start_init();
}
