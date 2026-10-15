// getrlimit(2)・setrlimit(2)・prlimit(2)。資源の上限の表をプロセスの中に持つ。
//
// カーネルは上限で断らない（fd の数も子の数も数えていない）ので、ここの値は「プログラムが自分の表の大きさを
// 決めるための目安」として答える。初めの値は Linux の既定に合わせる: 開けるファイルは 1024（上は 4096）、
// スタックはリンカーが決めた主スレッドのスタックの大きさ、ほかは無制限。setrlimit は Linux と同じ決まりで
// 書き換える（cur は max を超えない、max を上げられるのは root だけ）。prlimit は自分にだけ効く。

#define _GNU_SOURCE
#include <errno.h>
#include <sys/resource.h>
#include <unistd.h>

extern char __stack_low[];
extern char __stack_high[];

static struct rlimit limits[RLIMIT_NLIMITS];
static int initialized;

static void initialize(void) {
  if (initialized)
    return;
  for (int i = 0; i < RLIMIT_NLIMITS; i++) {
    limits[i].rlim_cur = RLIM_INFINITY;
    limits[i].rlim_max = RLIM_INFINITY;
  }
  limits[RLIMIT_NOFILE].rlim_cur = 1024;
  limits[RLIMIT_NOFILE].rlim_max = 4096;
  rlim_t stack = (rlim_t)(__stack_high - __stack_low);
  limits[RLIMIT_STACK].rlim_cur = stack;
  limits[RLIMIT_STACK].rlim_max = stack;
  initialized = 1;
}

int getrlimit(int resource, struct rlimit *rlim) {
  if (resource < 0 || resource >= RLIMIT_NLIMITS) {
    errno = EINVAL;
    return -1;
  }
  initialize();
  *rlim = limits[resource];
  return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
  if (resource < 0 || resource >= RLIMIT_NLIMITS || rlim->rlim_cur > rlim->rlim_max) {
    errno = EINVAL;
    return -1;
  }
  initialize();
  if (rlim->rlim_max > limits[resource].rlim_max && geteuid() != 0) {
    errno = EPERM;
    return -1;
  }
  limits[resource] = *rlim;
  return 0;
}

int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit) {
  if (pid != 0 && pid != getpid()) {
    errno = ESRCH;
    return -1;
  }
  if (old_limit && getrlimit(resource, old_limit) != 0)
    return -1;
  if (new_limit && setrlimit(resource, new_limit) != 0)
    return -1;
  return 0;
}
