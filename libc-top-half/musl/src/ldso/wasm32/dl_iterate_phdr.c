// dl_iterate_phdr(3)。動的ローダーを持たないプログラムで読み込まれているのはプログラム自身だけなので、
// それを 1 つだけ答える（Linux で静的にリンクしたプログラムと同じ）。dlopen を持つプログラム（python.wasm など）では、
// 動的ローダーのゲスト側（@progate/browser-dyld の guest/dlfcn.c）が強い定義で置き換え、読み込んだ共有ライブラリも並べる。
// 定義が無いと、読み込まれたライブラリをこの関数で探す道具（threadpoolctl など）が「libc に無い」と諦め、
// OpenMP や BLAS のスレッド数を制御できない。
// wasm にはプログラムヘッダーが無いので dlpi_phdr は NULL・dlpi_phnum は 0、プログラムは番地 0 から置かれる

#include <link.h>
#include <stddef.h>

__attribute__((__weak__))
int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
	struct dl_phdr_info info = {
		.dlpi_addr = 0,
		// glibc と同じく、プログラム自身の名前は空の文字列
		.dlpi_name = "",
		.dlpi_phdr = NULL,
		.dlpi_phnum = 0,
		.dlpi_adds = 1,
		.dlpi_subs = 0,
	};
#ifdef _REENTRANT
	// スレッド版のスレッドローカル変数は __tls_base から始まるスレッドごとの領域にある。1 本の版では
	// ただの大域変数になり、スレッドローカルの領域を持たない
	if (__builtin_wasm_tls_size() > 0) {
		info.dlpi_tls_modid = 1;
		info.dlpi_tls_data = __builtin_wasm_tls_base();
	}
#endif
	return callback(&info, sizeof info, data);
}
