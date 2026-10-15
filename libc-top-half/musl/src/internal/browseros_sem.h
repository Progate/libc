// BrowserOS の名前つきセマフォ（sem_open）。
//
// 名前つきセマフォの値と待ち手はカーネルの表にある（→ browser-os の src/process/semaphores.ts）。プロセスは
// それぞれ別の線形メモリを持ち、musl のように /dev/shm のファイルを MAP_SHARED で写して futex で待つことが
// できないためである。sem_open が返す sem_t には「名前つきである印」とカーネルの handle だけを置き、sem_wait・
// sem_timedwait・sem_trywait・sem_post・sem_getvalue は印を見てカーネルへ回す。
//
// 印は __val[2] に置く。sem_init はそこに 0（プロセス間）か 128（プロセス内）を置くので、それと重ならない値にする。
// 実装は libc-bottom-half/sources/browseros/semaphore.c にある。

#ifndef __BROWSEROS_SEM_H
#define __BROWSEROS_SEM_H

#include <semaphore.h>
#include <time.h>

#define __BROWSEROS_SEM_NAMED 0x53454d4e

#define __browseros_sem_is_named(sem) ((sem)->__val[2] == __BROWSEROS_SEM_NAMED)

// 弱い参照にする。sem_init のセマフォしか使わないプログラムに semaphore.c（と、カーネルの browser_os_sem の import）を
// 引き込まないため。名前つきの印が付いた sem_t は sem_open からしか作られないので、そのときは必ず在る
int __browseros_sem_timedwait(sem_t *sem, const struct timespec *at) __attribute__((__weak__));
int __browseros_sem_trywait(sem_t *sem) __attribute__((__weak__));
int __browseros_sem_post(sem_t *sem) __attribute__((__weak__));
int __browseros_sem_getvalue(sem_t *sem, int *valp) __attribute__((__weak__));

#endif
