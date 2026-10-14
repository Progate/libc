// WebAssembly has no floating-point exceptions or alternate rounding modes,
// so there's no need to prevent expressions from moving or force their
// evaluation.
//
// BrowserOS の libc は例外フラグをソフトウェアで持ち、演算のあとにフラグを立てるのは fork の clang である
// （→ src/fenv/wasm32/fenv.c）。ここで素通しにすると `fp_barrier(1.0) / 0.0` が定数に畳まれ、log(0) が
// FE_DIVBYZERO を立てない。BrowserOS では定義せず、src/internal/libm.h の volatile を使う版に任せる
#ifndef __wasilibc_browseros

#define fp_barrierf fp_barrierf
static inline float fp_barrierf(float x)
{
	return x;
}

#define fp_barrier fp_barrier
static inline double fp_barrier(double x)
{
	return x;
}

#define fp_force_evalf fp_force_evalf
static inline void fp_force_evalf(float x)
{
}

#define fp_force_eval fp_force_eval
static inline void fp_force_eval(double x)
{
}
#endif
