/*
 * 静的 TLS の区画の大きさと揃えをローダーに聞く（spec.md の 7.2）。
 *
 * Linux と同じく、スレッドを作った時点で読み込まれている全モジュールの TLS は 1 つの区画に
 * 並ぶ（libc.so は位置 0）。区画の大きさと揃えはローダーだけが知るので、browser_dyld の
 * static_tls に聞く。このファイルは共有の libc.so にだけ入る。静的な libc.a では
 * __init_tls.c の弱い定義が使われ、自分のモジュールの TLS だけを返す
 */
#include <stddef.h>
#include <stdint.h>

#ifdef _REENTRANT
/* static_tls が書く区画の情報（大きさと揃え） */
struct browseros_static_tls {
	uint32_t size;
	uint32_t align;
};

__attribute__((import_module("browser_dyld"), import_name("static_tls")))
extern void __browser_dyld_static_tls(struct browseros_static_tls *out);

__attribute__((__visibility__("hidden")))
void __browseros_static_tls(size_t *size, size_t *align)
{
	struct browseros_static_tls tls;
	__browser_dyld_static_tls(&tls);
	/* libc.tls_size は musl と同じく揃えのずれのぶんを含めて持つ */
	*size = tls.size + tls.align;
	*align = tls.align;
}
#endif
