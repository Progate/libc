/* BrowserOS: スレッドローカルの領域とスレッドポインタ（musl の src/env/__init_tls.c と同じ口）。
 *
 * wasm のスレッドローカル変数は、__tls_base が指す領域にコンパイラが置く（ELF の PT_TLS は無い）。
 * スレッドの struct pthread も、その領域の中の __browseros_pthread_self に置き、スレッドポインタは
 * その番地にする（→ arch/wasm32/pthread_arch.h）。新しいスレッドの領域は __wasm_init_tls が初めの
 * 値で埋める。1 本の版ではスレッドローカル変数はただの大域変数で、スレッドは作れない。 */

#include <limits.h>
#include <string.h>
#include <stddef.h>
#include "pthread_impl.h"
#include "libc.h"
#include "atomic.h"
#include "syscall.h"

/* スレッド一覧のロックは pthread_impl.h で extern hidden 宣言の内部名。このファイルは
 * そのヘッダを読まないので、ここで同じく hidden にしておく */
__attribute__((__visibility__("hidden")))
volatile int __thread_list_lock;

/* libc の中だけの TLS（clone.c が新しいスレッドに置く先にも使う）。外に見せると
 * 動的な TLS（GOT 経由）の解決が要るので hidden にしてモジュール内の local-exec に留める */
__attribute__((__visibility__("hidden")))
_Thread_local struct pthread __browseros_pthread_self;

/* 動的に読むスレッドローカル（__tls_get_addr）は使わない。dtv は数だけ持つ */
static uintptr_t dtv[1];

int __init_tp(void *p)
{
	pthread_t td = p;
	td->self = td;
	int r = __set_thread_area(TP_ADJ(p));
	if (r < 0) return -1;
#ifdef _REENTRANT
	if (!r) libc.can_do_threads = 1;
#endif
	td->detach_state = DT_JOINABLE;
	td->tid = __syscall(SYS_set_tid_address, &__thread_list_lock);
	td->locale = &libc.global_locale;
	td->robust_list.head = &td->robust_list.head;
	td->sysinfo = __sysinfo;
	td->next = td->prev = td;
	return 0;
}

#ifdef _REENTRANT
void __wasm_init_tls(void *);

/* mem から libc.tls_size バイトに新しいスレッドの領域を作り、その struct pthread を返す。
 * .tdata の中身の写しと TLS の番地を差す GOT.data エントリの再配置（__wasm_init_tls がやる
 * 仕事）は、新しいスレッドの側の wasi_thread_start がそのスレッドのインスタンスで走らせる
 * （→ src/thread/wasm32/wasi_thread_start.s）。こちらのインスタンスで走らせると GOT.data が
 * 子の区画を差したまま戻ってこないので、ここでは番地の計算と dtv の口合わせだけをする */
void *__copy_tls(unsigned char *mem)
{
	/* 動的な道では libc.tls_align はローダーの static_tls が答えた全区画の揃え（→ static_tls_dyld.c） */
	size_t align = libc.tls_align;
	void *current = __builtin_wasm_tls_base();
	size_t offset = (char *)&__browseros_pthread_self - (char *)current;
	mem += -(uintptr_t)mem & (align - 1);
	pthread_t td = (pthread_t)(mem + offset);
	td->dtv = dtv;
	return td;
}
#else
void *__copy_tls(unsigned char *mem)
{
	pthread_t td = &__browseros_pthread_self;
	td->dtv = dtv;
	return td;
}
#endif

#ifdef _REENTRANT
/* 動的な道では静的 TLS の区画（読み込み時点の全モジュールぶん）の大きさと揃えをローダーが決める。
 * 共有の libc.so では src/env/wasm32/static_tls_dyld.c の強い定義が browser_dyld.static_tls に
 * 聞く。静的な libc.a ではこちらが使われ、自分のモジュールの TLS だけになる */
__attribute__((__weak__)) __attribute__((__visibility__("hidden")))
void __browseros_static_tls(size_t *size, size_t *align)
{
	*align = __builtin_wasm_tls_align();
	*size = __builtin_wasm_tls_size() + *align;
}
#endif

static void static_init_tls(size_t *aux)
{
#ifdef _REENTRANT
	__browseros_static_tls(&libc.tls_size, &libc.tls_align);
	/* start 関数の __wasm_init_memory は主スレッドの __tls_base と .tdata/.tbss を用意するが、
	 * TLS の番地を差す GOT.data エントリ（__tls_base + offset への再配置）は __wasm_init_tls
	 * の中にだけある。主スレッドの区画にも 1 度走らせないと、GOT 経由で TLS を読むコード
	 * （__pthread_self() など）が 0 番地を指す。動的な道ではローダーが各モジュールの
	 * __wasm_init_tls を呼ぶので、ここで当て直すのは静的な道だけで意味を持つ */
	__wasm_init_tls(__builtin_wasm_tls_base());
#endif
	libc.tls_cnt = 1;
	pthread_t td = &__browseros_pthread_self;
	td->dtv = dtv;
	/* スレッドポインタを置けないのは致命的 */
	if (__init_tp(td) < 0)
		a_crash();
}

weak_alias(static_init_tls, __init_tls);
