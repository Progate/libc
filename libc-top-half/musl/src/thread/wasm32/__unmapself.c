/* BrowserOS: 切り離したスレッドが自分のスタックを返して終わる。wasm はスタックを切り替えられないので、
 * munmap(2) のあとは syscall を呼ぶだけでスタックに触れずに exit(2) する（カーネルがスレッドを終わらせる） */
#include "pthread_impl.h"
#include "syscall.h"

void __unmapself(void *base, size_t size)
{
	__syscall(SYS_munmap, base, size);
	for (;;) __syscall(SYS_exit, 0);
}
