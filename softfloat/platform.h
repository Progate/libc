/*
 * Berkeley SoftFloat's platform configuration for wasm32 (compare
 * build/Linux-x86_64-GCC/platform.h upstream).
 */

#ifndef platform_h
#define platform_h 1

#define LITTLEENDIAN 1

/*
 * Every primitive is local to the file that uses it. With C99's plain inline,
 * a call that the compiler does not inline needs an external definition,
 * which the upstream build gets from the s_*.c files that this subset leaves
 * out.
 */
#define INLINE static inline

#define SOFTFLOAT_BUILTIN_CLZ 1
#define SOFTFLOAT_INTRINSIC_INT128 1
#include "opts-GCC.h"

/* The exception flags of an operation belong to the thread that did it. */
#define THREAD_LOCAL _Thread_local

#endif
