/* BrowserOS: .so の側の crt1-reactor.o は _initialize から __init_libc を呼ぶが、musl は
 * __init_libc を internal（hidden、src/internal/libc.h）にしているので libc.so の export
 * には載らず、-lc を引く .so（拡張モジュール）のリンクが undefined symbol になる。静的リンク
 * （crt1.o + libc.a）では同じリンク単位なので問題ないが、動的リンクでは import が名前で
 * 解決されるため libc.so の export にこの名前が要る。ここでは中身に呼び出すだけのものを作り、
 * export の名前だけ __init_libc に合わせる（→ src/env/__libc_start_main.c） */
void __init_libc(char **, char *);
__attribute__((__export_name__("__init_libc")))
void __browseros_init_libc(char **envp, char *pn)
{
	__init_libc(envp, pn);
}
