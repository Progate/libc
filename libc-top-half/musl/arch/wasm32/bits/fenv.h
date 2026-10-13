/*
 * wasm has no floating-point status register: its instructions never record
 * IEEE 754 exceptions.  The flags below are kept in software instead (see
 * src/fenv/wasm32/fenv.c), so feraiseexcept, fetestexcept and the other flag
 * functions behave as on other targets, and code that reports errors through
 * them (numpy's linear algebra, for one) works.  The values are the x86 ones.
 *
 * wasm rounds to nearest only, so FE_TONEAREST is the only rounding mode.
 */
#define FE_INVALID    1
#define FE_DIVBYZERO  4
#define FE_OVERFLOW   8
#define FE_UNDERFLOW  16
#define FE_INEXACT    32

#define FE_ALL_EXCEPT 61

#define FE_TONEAREST  0

typedef unsigned short fexcept_t;

typedef struct {
	unsigned short __status;
} fenv_t;

#define FE_DFL_ENV      ((const fenv_t *) -1)
