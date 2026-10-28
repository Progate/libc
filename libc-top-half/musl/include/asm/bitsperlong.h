#ifndef _ASM_BITSPERLONG_H
#define _ASM_BITSPERLONG_H

/*
 * long の幅（Linux の asm-generic/bitsperlong.h）。wasm32 は ILP32 なので 32。BrowserOS が倣う riscv32 の Linux と同じく
 * time_t は 64 ビットで（__USE_TIME_BITS64）、<sound/asound.h> はこの 2 つから 64 ビットの時刻の構造体を選ぶ
 */

#define __BITS_PER_LONG 32
#define __BITS_PER_LONG_LONG 64

#endif
