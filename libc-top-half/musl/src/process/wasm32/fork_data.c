/* BrowserOS: fork(2) の asyncify の書き出し先（→ src/internal/wasm32/syscall.c）。
 * 関数のフレームとローカル変数は wasm の外から読めないので、fork を使うプログラムは asyncify
 * （wasm-opt --asyncify --pass-arg=asyncify-imports@browser_os_linux.syscall）で組んでおく。カーネルは
 * clone(SIGCHLD) を受けたら、積み重なりをここへ書き出して _start まで巻き戻し、線形メモリを写してから、
 * 親と子の両方を clone の呼び出しへ巻き直す。fork を libc 本体に入れると、configure が fork を見つけ、
 * asyncify をかけずに組んだプログラムまで fork + exec の道を選ぶので、旧い道では fork と _Fork は
 * このライブラリ（-lbrowseros-fork）にある。
 * 新しい道（wasm32-linux-musl）では libc.a・libc.so のどちらにも入る。カーネルはこの番地を
 * asyncify_start_unwind(data) にそのまま渡すので、ここの私設の領域が書き出し先になる。
 * asyncify をかけずに組んだプログラムの判別は、asyncify の export が無い実体をカーネル側で
 * ENOSYS にする（→ browser-os の wasi/fork.ts）。binaryen が足す __asyncify_* の global は
 * wasm-opt 後にしか存在しないので、ここから C で名前を指定して読むことはできない */

/* 深く入れ子になった呼び出しから fork しても足りる大きさ */
#define ASYNCIFY_DATA_BYTES (64 * 1024)

/* Binaryen の決まり: 頭はいまの位置と終わり。その後ろに積み重なりが書かれる */
static struct {
	void *current;
	void *end;
	unsigned char stack[ASYNCIFY_DATA_BYTES];
} data;

/* asyncify の書き出し先は clone の入口（syscall.c）と _Fork.c だけが使う libc 内部のもの。
 * .so から外に見せると GOT 経由の解決になるので hidden に留める */
__attribute__((__visibility__("hidden")))
void *__browseros_fork_data(void)
{
	data.current = data.stack;
	data.end = data.stack + sizeof data.stack;
	return &data;
}
