#include <sys/utsname.h>
#ifdef __wasilibc_unmodified_upstream // Implement uname with placeholders
#include "syscall.h"
#elif defined(__wasilibc_browseros)
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#else
#include <string.h>
#endif

#if defined(__wasilibc_browseros) && !defined(__wasilibc_unmodified_upstream)
// BrowserOS のカーネルは自分の名乗り（`uname` コマンドと同じ値）を /proc/sys/kernel に出している。Linux の
// uname(2) と同じ値を返さないと、OS の名前で振る舞いを決めるプログラム（CMake の CMAKE_HOST_SYSTEM_NAME、
// Python の platform.system()）が `uname -s` と食い違う。/proc の無い世界では Linux の既定の値に倣う

// /proc の 1 行のファイルを読み、末尾の改行を落として field に入れる。読めなければ fallback
static void read_field(const char *path, char *field, size_t size, const char *fallback)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t length = fd < 0 ? -1 : read(fd, field, size - 1);
	if (fd >= 0)
		close(fd);
	if (length <= 0) {
		strncpy(field, fallback, size - 1);
		field[size - 1] = '\0';
		return;
	}
	field[length] = '\0';
	char *newline = strchr(field, '\n');
	if (newline)
		*newline = '\0';
}

int uname(struct utsname *uts)
{
	read_field("/proc/sys/kernel/ostype", uts->sysname, sizeof uts->sysname, "BrowserOS");
	read_field("/proc/sys/kernel/hostname", uts->nodename, sizeof uts->nodename, "(none)");
	read_field("/proc/sys/kernel/osrelease", uts->release, sizeof uts->release, "0.0.0");
	read_field("/proc/sys/kernel/version", uts->version, sizeof uts->version, uts->release);
	strcpy(uts->machine, "wasm32");
#ifdef _GNU_SOURCE
	strcpy(uts->domainname, "(none)");
#else
	strcpy(uts->__domainname, "(none)");
#endif
	return 0;
}
#else

int uname(struct utsname *uts)
{
#ifdef __wasilibc_unmodified_upstream // Implement uname with placeholders
	return syscall(SYS_uname, uts);
#else
	// Just fill in the fields with placeholder values.
	strcpy(uts->sysname, "wasi");
	strcpy(uts->nodename, "(none)");
	strcpy(uts->release, "0.0.0");
	strcpy(uts->version, "0.0.0");
#if defined(__wasm32__)
	strcpy(uts->machine, "wasm32");
#elif defined(__wasm64__)
	strcpy(uts->machine, "wasm64");
#else
	strcpy(uts->machine, "unknown");
#endif
#ifdef _GNU_SOURCE
	strcpy(uts->domainname, "(none)");
#else
	strcpy(uts->__domainname, "(none)");
#endif
	return 0;
#endif
}
#endif
