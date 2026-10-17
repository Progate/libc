/* BrowserOS: wasm のスレッドポインタは __tls_base（新しいスレッドは wasi_thread_start.s が置く）。
 * 置き直すものは無く、スレッドは作れる（1 本の版は clone(CLONE_THREAD) が EINVAL で断る） */
#include "pthread_impl.h"

int __set_thread_area(void *p)
{
	return 0;
}
