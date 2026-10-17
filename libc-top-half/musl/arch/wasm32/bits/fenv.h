/*
 * wasm has no floating-point status register: its instructions never record
 * IEEE 754 exceptions.  The flags below are kept in software instead (see
 * src/fenv/wasm32/fenv.c), so feraiseexcept, fetestexcept and the other flag
 * functions behave as on other targets, and code that reports errors through
 * them (numpy's linear algebra, for one) works.  The values, the rounding
 * modes and the types are those of riscv32 Linux (arch/riscv32/bits/fenv.h),
 * the ABI that BrowserOS follows.
 *
 * wasm rounds to nearest only.  The other rounding modes are named as on
 * riscv32, and fesetround reports that it cannot select them.
 */
#define FE_INVALID      16
#define FE_DIVBYZERO    8
#define FE_OVERFLOW     4
#define FE_UNDERFLOW    2
#define FE_INEXACT      1

#define FE_ALL_EXCEPT   31

#define FE_TONEAREST    0
#define FE_DOWNWARD     2
#define FE_UPWARD       3
#define FE_TOWARDZERO   1

typedef unsigned int fexcept_t;
typedef unsigned int fenv_t;

#define FE_DFL_ENV      ((const fenv_t *) -1)
