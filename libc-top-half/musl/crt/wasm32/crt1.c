#include "crt_start.h"

#ifdef __wasi__
/* .so（拡張モジュール）の側の _initialize は __browseros_init_libc を import で呼ぶので、
 * その相手になるものを主モジュールの export に入れる（→ src/env/wasm32/init_libc_export.c）。
 * 参照だけでは libc.a から取り込まれないので、used のアンカーで強く引く。
 * libc.so の道（wasm32-linux-musl）では init_libc_export.c は入らないのでアンカーも要らない */
__attribute__((__used__))
static void (*const __browseros_init_libc_anchor)(char **, char *) = __browseros_init_libc;
#endif

static int call_main(int argc, char **argv, char **envp)
{
#ifdef __wasi__
	if (__main_void) return __main_void();
#endif
	return __main_argc_argv(argc, argv);
}

__attribute__((__export_name__("_start")))
void _start(void)
{
#ifdef CRT_STATIC_PIE
	/* 静的な PIE（rcrt1.o）はローダーを通らないので、data の再配置を自分で当てる */
	__wasm_apply_data_relocs();
#endif
	int argc;
	char **argv = build_start_block(&argc);
	__libc_start_main(call_main, argc, argv, 0, 0, 0);
}
