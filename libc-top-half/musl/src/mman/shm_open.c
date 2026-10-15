#ifndef __wasilibc_unmodified_upstream
/* shm_open does not map memory; it only needs the declarations, which wasi-libc
 * keeps behind the mmap emulation switch. */
#define _WASI_EMULATED_MMAN
#endif
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
#include <pthread.h>
#endif

char *__shm_mapname(const char *name, char *buf)
{
	char *p;
	while (*name == '/') name++;
	if (*(p = __strchrnul(name, '/')) || p==name ||
	    (p-name <= 2 && name[0]=='.' && p[-1]=='.')) {
		errno = EINVAL;
		return 0;
	}
	if (p-name > NAME_MAX) {
		errno = ENAMETOOLONG;
		return 0;
	}
	memcpy(buf, "/dev/shm/", 9);
	memcpy(buf+9, name, p-name+1);
	return buf;
}

int shm_open(const char *name, int flag, mode_t mode)
{
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
	int cs;
#endif
	char buf[NAME_MAX+10];
	if (!(name = __shm_mapname(name, buf))) return -1;
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &cs);
#endif
	int fd = open(name, flag|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK, mode);
#if defined(__wasilibc_unmodified_upstream) || defined(_REENTRANT)
	pthread_setcancelstate(cs, 0);
#endif
	return fd;
}

int shm_unlink(const char *name)
{
	char buf[NAME_MAX+10];
	if (!(name = __shm_mapname(name, buf))) return -1;
	return unlink(name);
}
