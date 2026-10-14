/*
 * The exceptions of floating-point operations, computed exactly.
 *
 * wasm has no floating-point status register, so code compiled for wasm32
 * raises the IEEE 754 exceptions itself (LLVM's WebAssemblyFPExceptionFlags
 * pass). It decides the common cases inline, and calls these functions for
 * the rest: results that are zero, subnormal, infinite or NaN, operands at the
 * ends of the exponent range, and the formats that wasm has no instructions
 * for (half, bfloat16, binary128). Each function takes the operands as their
 * bits, redoes the operation in Berkeley SoftFloat, and raises the exceptions
 * it signals. The result is not needed: the caller has it from the operation
 * itself.
 *
 * SoftFloat is configured as on x86 with SSE (the 8086-SSE specialization):
 * tininess is detected after rounding, and underflow is raised when the result
 * is tiny and inexact. Rounding is to nearest, the only mode wasm has.
 */

#include <fenv.h>
#include <stdbool.h>
#include <stdint.h>

#include "platform.h"
#include "internals.h"
#include "softfloat.h"
#include "specialize.h"

/* Raises the exceptions that SoftFloat recorded since it was last cleared. */
static void raise_recorded(void)
{
	uint_fast8_t flags = softfloat_exceptionFlags;
	int excepts = 0;
	if (flags & softfloat_flag_invalid) excepts |= FE_INVALID;
	if (flags & softfloat_flag_infinite) excepts |= FE_DIVBYZERO;
	if (flags & softfloat_flag_overflow) excepts |= FE_OVERFLOW;
	if (flags & softfloat_flag_underflow) excepts |= FE_UNDERFLOW;
	if (flags & softfloat_flag_inexact) excepts |= FE_INEXACT;
	if (excepts) feraiseexcept(excepts);
}

static float16_t to_f16(uint32_t bits) { return (float16_t){(uint16_t)bits}; }
static bfloat16_t to_bf16(uint32_t bits) { return (bfloat16_t){(uint16_t)bits}; }
static float32_t to_f32(uint32_t bits) { return (float32_t){bits}; }
static float64_t to_f64(uint64_t bits) { return (float64_t){bits}; }
static float128_t to_f128(unsigned __int128 bits)
{
	union ui128_f128 u;
	u.ui.v0 = (uint64_t)bits;
	u.ui.v64 = (uint64_t)(bits >> 64);
	return u.f;
}

#define BINARY(op, fmt, type) \
	void __fe_##op##_##fmt(type a, type b) \
	{ \
		softfloat_exceptionFlags = 0; \
		fmt##_##op(to_##fmt(a), to_##fmt(b)); \
		raise_recorded(); \
	}

#define UNARY(op, fmt, type) \
	void __fe_##op##_##fmt(type a) \
	{ \
		softfloat_exceptionFlags = 0; \
		fmt##_##op(to_##fmt(a)); \
		raise_recorded(); \
	}

#define TRUNC(from, to, type) \
	void __fe_trunc_##from##_##to(type a) \
	{ \
		softfloat_exceptionFlags = 0; \
		from##_to_##to(to_##from(a)); \
		raise_recorded(); \
	}

#define FORMAT(fmt, type) \
	BINARY(add, fmt, type) \
	BINARY(sub, fmt, type) \
	BINARY(mul, fmt, type) \
	BINARY(div, fmt, type) \
	UNARY(sqrt, fmt, type)

FORMAT(f16, uint32_t)
FORMAT(f32, uint32_t)
FORMAT(f64, uint64_t)
FORMAT(f128, unsigned __int128)

TRUNC(f32, f16, uint32_t)
TRUNC(f64, f16, uint64_t)
TRUNC(f128, f16, unsigned __int128)
TRUNC(f64, f32, uint64_t)
TRUNC(f128, f32, unsigned __int128)
TRUNC(f128, f64, unsigned __int128)
TRUNC(f32, bf16, uint32_t)

/*
 * Narrowing to bfloat16 from double and binary128. SoftFloat has only the
 * conversion from float, and going through float would round twice. The
 * significand is shifted so that its leading one is at bit 14, with the bits
 * below jammed into the lowest, which is what softfloat_roundPackToBF16 takes
 * (as f32_to_bf16 passes it).
 */
void __fe_trunc_f64_bf16(uint64_t a)
{
	softfloat_exceptionFlags = 0;
	int_fast16_t exp = expF64UI(a);
	uint_fast64_t sig = fracF64UI(a);
	if (exp == 0x7FF) {
		/* Converting a NaN raises invalid when it is signaling. */
		struct commonNaN nan;
		if (sig) softfloat_f64UIToCommonNaN(a, &nan);
	} else if (exp || sig) {
		if (exp) {
			sig |= UINT64_C(0x0010000000000000);
		} else {
			struct exp16_sig64 norm = softfloat_normSubnormalF64Sig(sig);
			exp = norm.exp;
			sig = norm.sig;
		}
		softfloat_roundPackToBF16(signF64UI(a), exp - 0x381,
			softfloat_shortShiftRightJam64(sig, 38));
	}
	raise_recorded();
}

void __fe_trunc_f128_bf16(unsigned __int128 a)
{
	softfloat_exceptionFlags = 0;
	uint_fast64_t ui64 = (uint64_t)(a >> 64), ui0 = (uint64_t)a;
	int_fast32_t exp = expF128UI64(ui64);
	struct uint128 sig = {0};
	sig.v64 = fracF128UI64(ui64);
	sig.v0 = ui0;
	if (exp == 0x7FFF) {
		struct commonNaN nan;
		if (sig.v64 | sig.v0) softfloat_f128UIToCommonNaN(ui64, ui0, &nan);
	} else if (exp || sig.v64 | sig.v0) {
		if (exp) {
			sig.v64 |= UINT64_C(0x0001000000000000);
		} else {
			struct exp32_sig128 norm =
				softfloat_normSubnormalF128Sig(sig.v64, sig.v0);
			exp = norm.exp;
			sig = norm.sig;
		}
		/* The leading one is at bit 48 of the upper half. */
		uint_fast16_t sig16 = sig.v64 >> 34
			| (((sig.v64 & ((UINT64_C(1) << 34) - 1)) | sig.v0) != 0);
		softfloat_roundPackToBF16(signF128UI64(ui64), exp - 0x3F81, sig16);
	}
	raise_recorded();
}

/*
 * bfloat16 arithmetic. The operation is done in float and the result narrowed.
 * float has more than twice bfloat16's precision plus two bits, so rounding
 * twice gives the correctly rounded result, and narrowing the float result
 * detects tininess as rounding the exact one would. One case needs care: an
 * inexact float result that narrows exactly. The bfloat16 result is then the
 * float one, and it is tiny when it is below the smallest normal.
 */
static void narrow_to_bf16(float32_t r)
{
	uint_fast8_t wide = softfloat_exceptionFlags;
	softfloat_exceptionFlags = 0;
	f32_to_bf16(r);
	uint_fast8_t narrow = softfloat_exceptionFlags;
	uint_fast8_t flags = (wide & ~softfloat_flag_underflow) | narrow;
	if ((wide & softfloat_flag_inexact) && !(narrow & softfloat_flag_inexact)
		&& expF32UI(r.v) == 0)
		flags |= softfloat_flag_underflow;
	softfloat_exceptionFlags = flags;
}

#define BINARY_BF16(op) \
	void __fe_##op##_bf16(uint32_t a, uint32_t b) \
	{ \
		softfloat_exceptionFlags = 0; \
		/* Widening is exact; it raises invalid for a signaling NaN. */ \
		float32_t x = bf16_to_f32(to_bf16(a)), y = bf16_to_f32(to_bf16(b)); \
		narrow_to_bf16(f32_##op(x, y)); \
		raise_recorded(); \
	}

BINARY_BF16(add)
BINARY_BF16(sub)
BINARY_BF16(mul)
BINARY_BF16(div)

void __fe_sqrt_bf16(uint32_t a)
{
	softfloat_exceptionFlags = 0;
	narrow_to_bf16(f32_sqrt(bf16_to_f32(to_bf16(a))));
	raise_recorded();
}
