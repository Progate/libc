#ifndef __wasilibc___header_dirent_h
#define __wasilibc___header_dirent_h

/*
 * BrowserOS: d_type は Linux（musl の include/dirent.h）の値にする。カーネルの getdents64(2) が返す d_type をそのまま
 * 使える。S_IF* との変換も Linux と同じ式になる
 */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12
#define DT_WHT 14

#define IFTODT(x) ((x)>>12 & 017)
#define DTTOIF(x) ((x)<<12)

#include <__struct_dirent.h>
#include <__typedef_DIR.h>

#ifdef __cplusplus
extern "C" {
#endif

int __wasilibc_iftodt(int x);
int __wasilibc_dttoif(int x);

int closedir(DIR *);
DIR *opendir(const char *);
DIR *fdopendir(int);
int fdclosedir(DIR *);
struct dirent *readdir(DIR *);
void rewinddir(DIR *);
void seekdir(DIR *, long);
long telldir(DIR *);
DIR *opendirat(int, const char *);
void rewinddir(DIR *);
int scandirat(int, const char *, struct dirent ***,
              int (*)(const struct dirent *),
              int (*)(const struct dirent **, const struct dirent **));

#ifdef __cplusplus
}
#endif

#endif
