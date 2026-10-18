/* BrowserOS: シグナルの配り手。Linux のカーネルは syscall から戻るところでハンドラのフレームを積んで飛ばすが、
 * wasm の積み重なりはカーネルから触れない。そこで積まれたシグナルがあると分かったところで
 * （→ src/internal/wasm32/syscall.c の戻り値の上の 32 ビット）、カーネルの browseros_sigdeliver
 * （この OS が足した 1024 番）で配るシグナルとハンドラのあいだのマスクを受け取ってハンドラを呼び、
 * rt_sigreturn(2) でマスクを戻す。既定の動作（終わる・止まる）と無視はカーネルが済ませる。
 * 古いプログラムの道では、ランタイムが syscall から戻るところで __browseros_signal の export を呼ぶ
 * （→ @progate/browser-wasi の signals.ts）。その呼び出し口は __wasi__ の組にだけ残す */

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include "syscall.h"

#define SYS_browseros_sigdeliver 1024
#define HANDLER_BIT 0x80000000U

/* browseros_sigdeliver が書くフレーム（→ browser-os の src/wasi/linux/signals.ts）。3 つ目の引数にフレームの
 * 大きさを渡したときだけ、カーネルは `status`（sigchld の si_status）まで書く。小さいフレームを渡す古い libc の
 * 後ろを上書きしないためである */
struct frame {
	int32_t sig;
	uint32_t handler;
	int32_t flags;
	int32_t code;
	int32_t pid;
	uint32_t uid;
	uint64_t oldmask;
	int32_t status;
	int32_t pad;
};

/* 戻り値は、EINTR で終わった syscall をやり直してよいか（走らせたハンドラがすべて SA_RESTART を持っていたか）。
 * libc.so の道ではカーネルがハンドラの動作を持っているので、この返り値は古い道だけが読む */
__attribute__((__visibility__("hidden")))
int __browseros_deliver_signals(int hint)
{
	int saved = errno;
	int restart = 1;
	struct frame frame;
	while (__syscall(SYS_browseros_sigdeliver, hint, &frame, sizeof frame) > 0) {
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
			info.si_status = frame.status;
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

#ifdef __wasi__
/* 古いプログラムの道の受け口（→ 頭のコメント）。どのプログラムにも残す */
__attribute__((__export_name__("__browseros_signal")))
int __browseros_signal(int hint)
{
	return __browseros_deliver_signals(hint);
}
#endif
