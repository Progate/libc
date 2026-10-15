#include <semaphore.h>
#ifdef __wasilibc_browseros
// 名前つきセマフォ（sem_open）の値はカーネルにある（→ browseros_sem.h）
#include "browseros_sem.h"
#endif
#include <limits.h>
#include "pthread_impl.h"

int sem_trywait(sem_t *sem)
{
#ifdef __wasilibc_browseros
	if (__browseros_sem_is_named(sem)) return __browseros_sem_trywait(sem);
#endif
	int val;
	while ((val=sem->__val[0]) & SEM_VALUE_MAX) {
		if (a_cas(sem->__val, val, val-1)==val) return 0;
	}
	errno = EAGAIN;
	return -1;
}
