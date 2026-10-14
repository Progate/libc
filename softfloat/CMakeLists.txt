# Berkeley SoftFloat, for the functions of fenv-raise.c that compute the
# exceptions of a floating-point operation exactly (see
# berkeley-softfloat-3/NOTICE.wasi-libc.md and update-softfloat.sh).

set(softfloat_sources
  bf16_to_f32.c
  f128_add.c f128_div.c f128_mul.c f128_sqrt.c f128_sub.c
  f128_to_f16.c f128_to_f32.c f128_to_f64.c
  f16_add.c f16_div.c f16_mul.c f16_sqrt.c f16_sub.c
  f32_add.c f32_div.c f32_mul.c f32_sqrt.c f32_sub.c
  f32_to_bf16.c f32_to_f16.c
  f64_add.c f64_div.c f64_mul.c f64_sqrt.c f64_sub.c
  f64_to_f16.c f64_to_f32.c
  s_addMagsF128.c s_addMagsF16.c s_addMagsF32.c s_addMagsF64.c
  s_approxRecipSqrt32_1.c s_approxRecipSqrt_1Ks.c
  s_normRoundPackToF128.c s_normRoundPackToF32.c s_normRoundPackToF64.c
  s_normSubnormalF128Sig.c s_normSubnormalF16Sig.c s_normSubnormalF32Sig.c
  s_normSubnormalF64Sig.c
  s_roundPackToBF16.c s_roundPackToF128.c s_roundPackToF16.c
  s_roundPackToF32.c s_roundPackToF64.c
  s_shiftRightJam128.c s_shiftRightJam128Extra.c
  s_subMagsF128.c s_subMagsF16.c s_subMagsF32.c s_subMagsF64.c
  softfloat_state.c
  8086-SSE/s_bf16UIToCommonNaN.c 8086-SSE/s_commonNaNToBF16UI.c
  8086-SSE/s_commonNaNToF16UI.c 8086-SSE/s_commonNaNToF32UI.c
  8086-SSE/s_commonNaNToF64UI.c 8086-SSE/s_f128UIToCommonNaN.c
  8086-SSE/s_f32UIToCommonNaN.c 8086-SSE/s_f64UIToCommonNaN.c
  8086-SSE/s_propagateNaNF128UI.c 8086-SSE/s_propagateNaNF16UI.c
  8086-SSE/s_propagateNaNF32UI.c 8086-SSE/s_propagateNaNF64UI.c
  8086-SSE/softfloat_raiseFlags.c
)
list(TRANSFORM softfloat_sources PREPEND berkeley-softfloat-3/source/)

add_internal_object_library(softfloat fenv-raise.c ${softfloat_sources})
foreach(obj softfloat-shared softfloat-static)
  target_include_directories(${obj} PRIVATE
    .
    berkeley-softfloat-3/source/include
    berkeley-softfloat-3/source/8086-SSE)
  # The options of the upstream build for 64-bit hosts
  # (build/Linux-x86_64-GCC/Makefile), and the renaming of every symbol into the
  # reserved namespace (names.h).
  target_compile_definitions(${obj} PRIVATE
    SOFTFLOAT_FAST_INT64 INLINE_LEVEL=5
    SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32)
  target_compile_options(${obj} PRIVATE
    -include ${CMAKE_CURRENT_SOURCE_DIR}/names.h
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare
    -Wno-implicit-fallthrough -Wno-unused-label)
endforeach()
