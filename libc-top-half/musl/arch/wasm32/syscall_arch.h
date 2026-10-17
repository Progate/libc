/* BrowserOS: syscall は import の browser_os_linux.syscall(nr, a0..a5) -> i64 を 1 つ通す。番号・引数・
 * 戻り値は riscv32 の Linux のもの（失敗は -errno）。64 ビットの引数は riscv32 と同じく 2 つの long に分ける。 */

#define __SYSCALL_LL_E(x) \
((union { long long ll; long l[2]; }){ .ll = x }).l[0], \
((union { long long ll; long l[2]; }){ .ll = x }).l[1]
#define __SYSCALL_LL_O(x) __SYSCALL_LL_E((x))

__attribute__((__visibility__("hidden")))
long __browseros_syscall(long, long, long, long, long, long, long);

static inline long __syscall0(long n)
{
	return __browseros_syscall(n, 0, 0, 0, 0, 0, 0);
}

static inline long __syscall1(long n, long a)
{
	return __browseros_syscall(n, a, 0, 0, 0, 0, 0);
}

static inline long __syscall2(long n, long a, long b)
{
	return __browseros_syscall(n, a, b, 0, 0, 0, 0);
}

static inline long __syscall3(long n, long a, long b, long c)
{
	return __browseros_syscall(n, a, b, c, 0, 0, 0);
}

static inline long __syscall4(long n, long a, long b, long c, long d)
{
	return __browseros_syscall(n, a, b, c, d, 0, 0);
}

static inline long __syscall5(long n, long a, long b, long c, long d, long e)
{
	return __browseros_syscall(n, a, b, c, d, e, 0);
}

static inline long __syscall6(long n, long a, long b, long c, long d, long e, long f)
{
	return __browseros_syscall(n, a, b, c, d, e, f);
}

#define IPC_64 0
