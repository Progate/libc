typedef unsigned long __jmp_buf[8];

/* wasm32 では setjmp / longjmp はコンパイラが wasm の例外へ落とす（-mllvm -wasm-enable-sjlj、
 * 実体は -lsetjmp の __wasm_setjmp / __wasm_longjmp）。sigsetjmp / siglongjmp はその変換の
 * 対象ではなく、呼び出し側で setjmp / longjmp に写し替えて初めて組める。関数としては
 * 定義できない（戻ってきたとき呼び出しの枠はもう無い）のでここでマクロにする。
 * シグナルマスクは sjlj では保存・復元できないので savemask は捨てる（呼び出しても
 * sigprocmask は変わらない＝ savemask 0 と同じ振る舞い） */
#define sigsetjmp(env, savemask) setjmp(env)
#define siglongjmp(env, val) longjmp(env, val)
