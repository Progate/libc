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
#define SYS_prctl 167
#define SYS_getuid 174
#define SYS_geteuid 175
#define SYS_getgid 176
#define SYS_getegid 177
#define SYS_mmap 222
#define SYS_getrandom 278
#define PROT_RW 3
#define MAP_PRIVATE_ANONYMOUS 0x22
/* Linux 6.4 の prctl(2) の命令（include/uapi/linux/prctl.h） */
#define PR_GET_AUXV 0x41555856
/* カーネルの mm->saved_auxv の語の数（Linux 6.1 の riscv の AT_VECTOR_SIZE） */
#define KERNEL_AUXV_WORDS 60

void __wasm_call_ctors(void);
/* wasm-ld が PIE に作る data 再配置（ローダーを通らない静的な PIE は自分で呼ぶ） */
void __wasm_apply_data_relocs(void);
int __libc_start_main(int (*)(int, char **, char **), int, char **, void (*)(), void (*)(), void (*)());
void __init_libc(char **, char *);
/* __init_libc は hidden なので .so からは呼べない。外から呼べる名前を libc に別名で置いてある
 * （→ src/env/wasm32/init_libc_export.c） */
void __browseros_init_libc(char **, char *);

#ifdef __wasi__
/* C の main は、引数を取るなら __main_argc_argv、取らないなら __main_void という名前になる（clang の wasm の決まり）。
 * WASI の的では clang が引数を取らない main に __main_argc_argv の入口を作らないので、どちらか一方しか無い。
 * 強く参照すると `int main(void)` のプログラムがリンクで失敗するので、wasi-libc と同じく両方を弱く参照する */
int __main_argc_argv(int, char **) __attribute__((__weak__));
int __main_void(void) __attribute__((__weak__));
#else
/* C の main。引数を取る main は __main_argc_argv という名前になり、取らない main にも clang が同じ名前の入口を作る
 * （wasm32-unknown-linux-musl。Progate/llvm-project#48）。ELF の crt1 が main を強く参照するのと同じく強く参照する:
 * アーカイブの中の main（libgtest_main.a）も取り込まれ、main の無いプログラムは起動して止まるのではなくリンクで失敗する */
int __main_argc_argv(int, char **);
#endif

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

/* カーネルが execve(2) で決めた auxv（AT_SECURE・exec の時点の AT_UID など）を kaux へ写し、AT_NULL の前までの組の数を返す。
 * Linux はこれを起動のスタックに積むが、wasm には起動のスタックで値を渡す仕組みが無いので、BrowserOS のカーネルは prctl(2) の
 * PR_GET_AUXV で渡す。AT_SECURE を 0 と決めつけると、ファイルの capability で権を得たプログラム（uid と euid が同じ）が
 * secure_getenv や LD_* を信じてしまう。PR_GET_AUXV を持たないカーネル（EINVAL）では、いまの資格の、権を引き上げていない実行の
 * ものにする */
static size_t kernel_auxv(size_t *kaux)
{
	long r = sys(SYS_prctl, PR_GET_AUXV, (long)kaux, KERNEL_AUXV_WORDS * sizeof(size_t), 0, 0, 0);
	size_t n = 0;
	if (r > 0) {
		while (n < KERNEL_AUXV_WORDS / 2 - 1 && kaux[2 * n] != AT_NULL) n++;
		return n;
	}
	kaux[n * 2] = AT_PAGESZ; kaux[n++ * 2 + 1] = 4096;
	kaux[n * 2] = AT_UID; kaux[n++ * 2 + 1] = (size_t)sys(SYS_getuid, 0, 0, 0, 0, 0, 0);
	kaux[n * 2] = AT_EUID; kaux[n++ * 2 + 1] = (size_t)sys(SYS_geteuid, 0, 0, 0, 0, 0, 0);
	kaux[n * 2] = AT_GID; kaux[n++ * 2 + 1] = (size_t)sys(SYS_getgid, 0, 0, 0, 0, 0, 0);
	kaux[n * 2] = AT_EGID; kaux[n++ * 2 + 1] = (size_t)sys(SYS_getegid, 0, 0, 0, 0, 0, 0);
	kaux[n * 2] = AT_SECURE; kaux[n++ * 2 + 1] = 0;
	return n;
}

/* argc・argv・NULL・envp・NULL・auxv を 1 つの領域に並べ、argv の先頭を返す */
static char **build_start_block(int *argc_out)
{
	size_t argc, argv_size, envc, env_size;
	if (__browseros_args_sizes_get(&argc, &argv_size) || __browseros_environ_sizes_get(&envc, &env_size))
		die();
	size_t kaux[KERNEL_AUXV_WORDS];
	size_t kauxc = kernel_auxv(kaux);
	/* カーネルの組に、AT_RANDOM と終わりの AT_NULL を足す */
	size_t auxc = kauxc + 2;
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
	for (; i < 2 * kauxc; i++) auxv[i] = kaux[i];
	/* AT_RANDOM の 16 バイトは起動のスタックの代わりにこの領域に置く（カーネルはゲストの番地を持たないので auxv に載せない） */
	auxv[i++] = AT_RANDOM; auxv[i++] = (size_t)random;
	auxv[i++] = AT_NULL; auxv[i++] = 0;
	*argc_out = (int)argc;
	return argv;
}
