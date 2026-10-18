/* BrowserOS: 動的な道の初期化・終了処理は、読み込んだモジュールの順を知っているローダーが
 * 受け持つ（→ docs/foundation/spec.md の 5.5）。libc.so の __libc_start_init はローダーの
 * init_modules を呼び、ローダーが依存されるものから順に各モジュールの __wasm_call_ctors を
 * 走らせる（musl の動的リンクで ld.so が __libc_start_init を受け持つのと同じ形）。exit の
 * ときは fini_modules を呼び、逆の順に終了処理を走らせる。
 *
 * このファイルは共有の libc.so にだけ入る。静的な libc.a には入らず、静的な道では
 * crt1.o（crt_start.h）の __libc_start_init が __wasm_call_ctors を呼ぶ */

#include <stdint.h>

__attribute__((import_module("browser_dyld"), import_name("init_modules")))
extern void __browser_dyld_init_modules(void);
__attribute__((import_module("browser_dyld"), import_name("fini_modules")))
extern void __browser_dyld_fini_modules(void);

/* musl ではこの 2 つは libc.h で hidden 宣言の内部名（crt と libc の間の受け渡し用）。
 * .so の中だけで解決されればよいので同じく hidden にしておく */
__attribute__((__visibility__("hidden")))
void __libc_start_init(void)
{
	__browser_dyld_init_modules();
}

__attribute__((__visibility__("hidden")))
void __libc_exit_fini(void)
{
	__browser_dyld_fini_modules();
}
