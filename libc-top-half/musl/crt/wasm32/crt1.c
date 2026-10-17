#include "crt_start.h"

static int call_main(int argc, char **argv, char **envp)
{
	if (__main_void) return __main_void();
	return __main_argc_argv(argc, argv);
}

__attribute__((__export_name__("_start")))
void _start(void)
{
	int argc;
	char **argv = build_start_block(&argc);
	__libc_start_main(call_main, argc, argv, 0, 0, 0);
}
