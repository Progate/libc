#ifndef __wasilibc___header_sys_resource_h
#define __wasilibc___header_sys_resource_h

#include <__struct_rusage.h>

/* BrowserOS: getrusage(2) の who は Linux の値 */
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD 1

#ifdef __cplusplus
extern "C" {
#endif

int getrusage(int who, struct rusage *usage);

#ifdef __cplusplus
}
#endif

#endif
