/* -lbrowseros-fork を引かないプログラムのための fork 系の stub。
 *
 * fork が要るプログラムは -lbrowseros-fork をリンクして asyncify で組む（→ libc-top-half/musl/src/process/wasm32/fork_data.c
 * の説明）。そうでないプログラムでも、fork の名前が残るコード（configure が置いた分岐やライブラリの
 * 使われない部）のためにリンクだけは通したいことがある。libc.a には入れられない（configure が fork を
 * 見つけて、asyncify なしに組んだプログラムまで fork を選ぶので）ので、引きたいものだけが引く別の
 * ライブラリにする。daemon・forkpty・wordexp のような libc.a の中で fork を呼ぶ関数も、このライブラリを
 * 引けばリンクが通る（呼べば ENOSYS を返す）。
 * 全部弱い定義なので、本物の -lbrowseros-fork と一緒に引いてもそちらが勝つ
 */
#include <errno.h>
#include <unistd.h>

static int nofork(void)
{
	errno = ENOSYS;
	return -1;
}

__attribute__((__weak__)) pid_t fork(void) { return nofork(); }
__attribute__((__weak__)) pid_t vfork(void) { return nofork(); }
__attribute__((__weak__)) pid_t _Fork(void) { return nofork(); }
