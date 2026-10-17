#include <fenv.h>

/*
 * The floating-point exception flags, kept in software because wasm has no
 * status register.  They live in a mutable wasm global: each thread
 * instantiates the module and so has its own, which makes the flags belong
 * to the thread like the hardware ones, and code reads and sets them without
 * touching memory.  They are sticky: only feclearexcept, fesetexceptflag and
 * fesetenv clear them.
 *
 * Code compiled for wasm32 raises them after each floating-point operation
 * (LLVM's WebAssemblyFPExceptionFlags pass), by global.get and global.set of
 * __fe_status and by calling the __fe_* functions of softfloat/fenv-raise.c.
 * Dynamically loaded modules import the global from the program, so they all
 * share the flags of the thread.
 */
__asm__(
	".globaltype __fe_status, i32\n"
	".globl __fe_status\n"
	"__fe_status:\n");

static unsigned get_status(void)
{
	unsigned status;
	__asm__ volatile("global.get __fe_status\n\tlocal.set %0" : "=r"(status));
	return status;
}

static void set_status(unsigned status)
{
	__asm__ volatile("local.get %0\n\tglobal.set __fe_status" :: "r"(status));
}

int feclearexcept(int mask)
{
	set_status(get_status() & ~(mask & FE_ALL_EXCEPT));
	return 0;
}

int feraiseexcept(int mask)
{
	set_status(get_status() | (mask & FE_ALL_EXCEPT));
	return 0;
}

int fetestexcept(int mask)
{
	return get_status() & mask & FE_ALL_EXCEPT;
}

int fegetround(void)
{
	return FE_TONEAREST;
}

/* wasm rounds to nearest only, so the other modes cannot be selected. */
int __fesetround(int r)
{
	return r == FE_TONEAREST ? 0 : -1;
}

/* The environment is the flags, as the riscv32 fcsr holds the flags and the
   rounding mode (always to nearest here). */
int fegetenv(fenv_t *envp)
{
	*envp = get_status();
	return 0;
}

int fesetenv(const fenv_t *envp)
{
	set_status(envp == FE_DFL_ENV ? 0 : *envp & FE_ALL_EXCEPT);
	return 0;
}
