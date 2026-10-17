/* BrowserOS: スレッドの struct pthread は、そのスレッドの TLS の中に置く（wasm の TLS は __tls_base が指す）。
 * 1 本の版では TLS は普通の大域変数になる。 */
extern _Thread_local struct __pthread __browseros_pthread_self;

static inline uintptr_t __get_tp()
{
	return (uintptr_t)&__browseros_pthread_self;
}

#define MC_PC __gregs[0]
