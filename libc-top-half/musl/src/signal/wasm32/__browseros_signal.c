/* BrowserOS: シグナルの受け口。Linux のカーネルは syscall から戻るところでハンドラのフレームを積んで飛ばすが、
 * wasm の積み重なりはカーネルから触れない。そこで syscall から戻るところでランタイムがこの export を呼び
 * （→ @progate/browser-wasi の signals.ts）、カーネルの browseros_sigdeliver（この OS が足した 1024 番）で配る
 * シグナルとハンドラのあいだのマスクを受け取ってハンドラを呼び、rt_sigreturn(2) でマスクを戻す。
 * 既定の動作（終わる・止まる）と無視はカーネルが済ませる。 */

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "syscall.h"

#define SYS_browseros_sigdeliver 1024
#define HANDLER_BIT 0x80000000U

/* browseros_sigdeliver が書くフレーム（→ browser-os の src/wasi/linux/signals.ts） */
struct frame {
	int32_t sig;
	uint32_t handler;
	int32_t flags;
	int32_t code;
	int32_t pid;
	uint32_t uid;
	uint64_t oldmask;
};

/* 戻り値は、EINTR で終わった syscall をやり直してよいか（走らせたハンドラがすべて SA_RESTART を持っていたか） */
__attribute__((__export_name__("__browseros_signal")))
int __browseros_signal(int hint)
{
	int saved = errno;
	int restart = 1;
	struct frame frame;
	while (__syscall(SYS_browseros_sigdeliver, hint, &frame) > 0) {
		hint = 0;
		uintptr_t handler = frame.handler & ~HANDLER_BIT;
		if (frame.flags & SA_SIGINFO) {
			siginfo_t info;
			ucontext_t uc;
			memset(&info, 0, sizeof info);
			memset(&uc, 0, sizeof uc);
			info.si_signo = frame.sig;
			info.si_code = frame.code;
			info.si_pid = frame.pid;
			info.si_uid = frame.uid;
			memcpy(&uc.uc_sigmask, &frame.oldmask, sizeof frame.oldmask);
			((void (*)(int, siginfo_t *, void *))handler)(frame.sig, &info, &uc);
		} else {
			((void (*)(int))handler)(frame.sig);
		}
		if (!(frame.flags & SA_RESTART)) restart = 0;
		__syscall(SYS_rt_sigreturn, &frame);
	}
	errno = saved;
	return restart;
}
