#include "pthread_impl.h"

#if !defined(__wasilibc_unmodified_upstream) && !defined(__wasilibc_browseros)
#include <common/clock.h>
#endif

int pthread_condattr_setclock(pthread_condattr_t *a, clockid_t clk)
{
#if defined(__wasilibc_unmodified_upstream) || defined(__wasilibc_browseros) /* BrowserOS: clockid_t は Linux と同じ番号 */
	if (clk < 0 || clk-2U < 2) return EINVAL;
#else
	if (clk->id < 0 || clk->id-2U < 2) return EINVAL;
#endif
	a->__attr &= 0x80000000;
#if defined(__wasilibc_unmodified_upstream) || defined(__wasilibc_browseros) /* BrowserOS: clockid_t は Linux と同じ番号 */
	a->__attr |= clk;
#else
	a->__attr |= clk->id;
#endif
	return 0;
}
