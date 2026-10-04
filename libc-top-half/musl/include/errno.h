#ifndef	_ERRNO_H
#define _ERRNO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <features.h>

#ifdef __wasilibc_unmodified_upstream /* Use alternate WASI libc headers */
#include <bits/errno.h>
#else
#include <__errno_values.h>
#endif

#ifdef __GNUC__
__attribute__((const))
#endif
int *__errno_location(void);

// Cross-module TLS does not work in WASIp3, so declaring an `extern` to some
// data doesn't work for errno. On other platforms though that's been the
// historical default so that's left in place.
//
// BrowserOS: atomics を持つ構成（wasm32-wasip1-threads）でもスレッドローカル変数は本物になり、
// 共有ライブラリ（browser-dyld のサイドモジュール）からメインの errno を指せなくなる
// （wasm-ld が「未定義の名への R_WASM_MEMORY_ADDR_TLS_SLEB」で断る）。glibc と同じく関数で読む
#if (!defined(__wasip1__) && !defined(__wasip2__)) || defined(__wasm_atomics__)
#define errno (*__errno_location())
#else
extern _Thread_local int errno;
#define errno errno
#endif

#ifdef _GNU_SOURCE
extern char *program_invocation_short_name, *program_invocation_name;
#endif

#ifdef __cplusplus
}
#endif

#endif

