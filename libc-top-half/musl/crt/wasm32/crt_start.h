/* BrowserOS: 起動（musl の crt/crt1.c と同じ役）。wasm にはスタックで argv を渡す仕組みが無いので、
 * argv と環境変数は wasip1 の args_get・environ_get で受け取り、Linux の起動と同じ並び
 * （argc・argv・NULL・envp・NULL・auxv）にして __libc_start_main へ渡す。
 * コンストラクタは .init_array ではなく wasm-ld が作る __wasm_call_ctors にある。 */

#include <stdint.h>
#include <stddef.h>
#include <elf.h>

typedef uint16_t wasi_errno_t;

__attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__("args_sizes_get")))
wasi_errno_t __browseros_args_sizes_get(size_t *, size_t *);
__attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__("args_get")))
wasi_errno_t __browseros_args_get(char **, char *);
__attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__("environ_sizes_get")))
wasi_errno_t __browseros_environ_sizes_get(size_t *, size_t *);
__attribute__((__import_module__("wasi_snapshot_preview1"), __import_name__("environ_get")))
wasi_errno_t __browseros_environ_get(char **, char *);
__attribute__((__import_module__("browser_os_linux"), __import_name__("syscall")))
int64_t __browseros_linux_syscall(int32_t, int64_t, int64_t, int64_t, int64_t, int64_t, int64_t);

#define SYS_exit_group 94
#define SYS_getuid 174
#define SYS_geteuid 175
#define SYS_getgid 176
#define SYS_getegid 177
#define SYS_mmap 222
#define SYS_getrandom 278
#define PROT_RW 3
#define MAP_PRIVATE_ANONYMOUS 0x22

void __wasm_call_ctors(void);
/* wasm-ld が PIE に作る data 再配置（ローダーを通らない静的な PIE は自分で呼ぶ） */
void __wasm_apply_data_relocs(void);
int __libc_start_main(int (*)(int, char **, char **), int, char **, void (*)(), void (*)(), void (*)());
void __init_libc(char **, char *);
/* __init_libc は hidden なので .so からは呼べない。外から呼べる名前を libc に別名で置いてある
 * （→ src/env/wasm32/init_libc_export.c） */
void __browseros_init_libc(char **, char *);

/* C の main は、引数を取るなら __main_argc_argv、取らないなら __main_void という名前になる（clang の wasm の決まり） */
int __main_argc_argv(int, char **) __attribute__((__weak__));
int __main_void(void) __attribute__((__weak__));

/* musl の __libc_start_init（弱い別名）の代わり。__init_libc のあとにコンストラクタを走らせる。
 * 共有の libc.so の道では、読み込んだモジュールの順を知っているローダーがコンストラクタを
 * 走らせるので、libc.so 側の __libc_start_init（→ src/env/wasm32/init_modules.c）が勝つ。
 * こちらは静的な道（crt1.o・rcrt1.o + libc.a）のためのもの */
#ifndef CRT_SHARED
void __libc_start_init(void)
{
	__wasm_call_ctors();
}
#endif

static long sys(long n, long a, long b, long c, long d, long e, long f)
{
	return (long)__browseros_linux_syscall(n, a, b, c, d, e, f);
}

static _Noreturn void die(void)
{
	for (;;) sys(SYS_exit_group, 127, 0, 0, 0, 0, 0);
}

/* argc・argv・NULL・envp・NULL・auxv を 1 つの領域に並べ、argv の先頭を返す */
static char **build_start_block(int *argc_out)
{
	size_t argc, argv_size, envc, env_size;
	if (__browseros_args_sizes_get(&argc, &argv_size) || __browseros_environ_sizes_get(&envc, &env_size))
		die();
	size_t auxc = 8;
	size_t words = 1 + argc + 1 + envc + 1 + 2 * auxc;
	size_t size = words * sizeof(size_t) + 16 + argv_size + env_size;
	long p = sys(SYS_mmap, 0, (long)size, PROT_RW, MAP_PRIVATE_ANONYMOUS, -1, 0);
	if (p < 0 && p > -4096) die();
	size_t *block = (size_t *)p;
	char **argv = (char **)(block + 1);
	char **envp = argv + argc + 1;
	size_t *auxv = (size_t *)(envp + envc + 1);
	unsigned char *random = (unsigned char *)(auxv + 2 * auxc);
	char *argbuf = (char *)random + 16;
	char *envbuf = argbuf + argv_size;
	if (__browseros_args_get(argv, argbuf) || __browseros_environ_get(envp, envbuf)) die();
	argv[argc] = 0;
	envp[envc] = 0;
	block[0] = argc;
	sys(SYS_getrandom, (long)random, 16, 0, 0, 0, 0);
	size_t i = 0;
	auxv[i++] = AT_PAGESZ; auxv[i++] = 4096;
	auxv[i++] = AT_RANDOM; auxv[i++] = (size_t)random;
	auxv[i++] = AT_UID; auxv[i++] = (size_t)sys(SYS_getuid, 0, 0, 0, 0, 0, 0);
	auxv[i++] = AT_EUID; auxv[i++] = (size_t)sys(SYS_geteuid, 0, 0, 0, 0, 0, 0);
	auxv[i++] = AT_GID; auxv[i++] = (size_t)sys(SYS_getgid, 0, 0, 0, 0, 0, 0);
	auxv[i++] = AT_EGID; auxv[i++] = (size_t)sys(SYS_getegid, 0, 0, 0, 0, 0, 0);
	auxv[i++] = AT_SECURE; auxv[i++] = 0;
	auxv[i++] = AT_NULL; auxv[i++] = 0;
	*argc_out = (int)argc;
	return argv;
}
