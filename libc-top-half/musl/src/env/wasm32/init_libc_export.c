/* BrowserOS: .so の側（拡張モジュール）の crt1-reactor.o は _initialize から
 * __browseros_init_libc を呼ぶ。__init_libc そのものではないのは、musl がそれを
 * internal（hidden、src/internal/libc.h）にしていて libc.so の export に載らない
 * からである。静的リンク（crt1.o + libc.a）では同じリンク単位なので問題ないが、
 * -lc を引く .so のリンクは undefined symbol で落ちる。外から呼べる名前が要るので、
 * 中身に呼び出すだけのものをここに置く（→ src/env/__libc_start_main.c）。
 * __export_name__ で __init_libc の名前を被せることはしない。主モジュールが
 * --export-all で本物の __init_libc も export すると名前が重なってしまう */
void __init_libc(char **, char *);
void __browseros_init_libc(char **envp, char *pn)
{
	__init_libc(envp, pn);
}
