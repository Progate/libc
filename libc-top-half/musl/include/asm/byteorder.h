#ifndef _ASM_BYTEORDER_H
#define _ASM_BYTEORDER_H

/*
 * バイトの並び（Linux の include/uapi/linux/byteorder/little_endian.h のうち印のぶん）。wasm32 はリトルエンディアン
 */

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif

#endif
