/* BrowserOS: 起動（musl の crt/crt1.c と同じ役）。入口のアセンブリ（arch/wasm32/crt_arch.h）が、カーネルの積んだ起動のスタック
 * （argc・argv・NULL・envp・NULL・auxv）の頭を START_c へ渡す。
 * コンストラクタは .init_array ではなく wasm-ld が作る __wasm_call_ctors にある。 */

#include <features.h>
#include "libc.h"

void __wasm_call_ctors(void);
/* wasm-ld が PIE に作る data 再配置（ローダーを通らない静的な PIE は自分で呼ぶ） */
void __wasm_apply_data_relocs(void);
int __libc_start_main(int (*)(int, char **, char **), int, char **, void (*)(), void (*)(), void (*)());
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
