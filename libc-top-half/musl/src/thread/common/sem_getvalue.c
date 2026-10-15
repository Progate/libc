#include <semaphore.h>
#ifdef __wasilibc_browseros
// 名前つきセマフォ（sem_open）の値はカーネルにある（→ browseros_sem.h）
#include "browseros_sem.h"
#endif
#include <limits.h>

int sem_getvalue(sem_t *restrict sem, int *restrict valp)
{
#ifdef __wasilibc_browseros
	if (__browseros_sem_is_named(sem)) return __browseros_sem_getvalue(sem, valp);
#endif
	int val = sem->__val[0];
	*valp = val & SEM_VALUE_MAX;
	return 0;
}
