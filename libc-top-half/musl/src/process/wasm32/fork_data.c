/* BrowserOS: fork(2) の asyncify の書き出し先（-lbrowseros-fork が持つ。→ src/internal/wasm32/syscall.c）。
 * 関数のフレームとローカル変数は wasm の外から読めないので、fork を使うプログラムは asyncify
 * （wasm-opt --asyncify --pass-arg=asyncify-imports@browser_os_linux.syscall）で組んでおく。カーネルは
 * clone(SIGCHLD) を受けたら、積み重なりをここへ書き出して _start まで巻き戻し、線形メモリを写してから、
 * 親と子の両方を clone の呼び出しへ巻き直す。fork を libc 本体に入れると、configure が fork を見つけ、
 * asyncify をかけずに組んだプログラムまで fork + exec の道を選ぶので、fork と _Fork はこのライブラリにある。 */

/* 深く入れ子になった呼び出しから fork しても足りる大きさ */
#define ASYNCIFY_DATA_BYTES (64 * 1024)

/* Binaryen の決まり: 頭はいまの位置と終わり。その後ろに積み重なりが書かれる */
static struct {
	void *current;
	void *end;
	unsigned char stack[ASYNCIFY_DATA_BYTES];
} data;

void *__browseros_fork_data(void)
{
	data.current = data.stack;
	data.end = data.stack + sizeof data.stack;
	return &data;
}
