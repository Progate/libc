#include <fenv.h>

/*
 * The floating-point exception flags, kept in software because wasm has no
 * status register.  Like the hardware flags on other targets they belong to
 * the thread, and they are sticky: only feclearexcept, fesetexceptflag and
 * fesetenv clear them.
 */
static _Thread_local unsigned short status;

int feclearexcept(int mask)
{
	status &= ~(mask & FE_ALL_EXCEPT);
	return 0;
}

int feraiseexcept(int mask)
{
	status |= mask & FE_ALL_EXCEPT;
	return 0;
}

int fetestexcept(int mask)
{
	return status & mask & FE_ALL_EXCEPT;
}

int fegetround(void)
{
	return FE_TONEAREST;
}

int __fesetround(int r)
{
	return 0;
}

int fegetenv(fenv_t *envp)
{
	envp->__status = status;
	return 0;
}

int fesetenv(const fenv_t *envp)
{
	status = envp == FE_DFL_ENV ? 0 : envp->__status & FE_ALL_EXCEPT;
	return 0;
}
