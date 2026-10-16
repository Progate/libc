#ifndef __wasilibc___header_sys_stat_h
#define __wasilibc___header_sys_stat_h

#include <__struct_stat.h>

#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec

#include <__mode_t.h>

/* BrowserOS: utimensat(2) の特別な tv_nsec は Linux の値 */
#define UTIME_NOW  0x3fffffff
#define UTIME_OMIT 0x3ffffffe

#endif
