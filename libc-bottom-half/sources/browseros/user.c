// 利用者とグループ: getuid(2)・getgid(2)・getgroups(2)・setgroups(2)。chown(2) の仲間は paths.c にある。
//
// この OS の利用者は 1 人で、番号はカーネルが決める（root なら 0、それ以外は 1000。`id` と同じ決め方）。グループも
// 同じ番号の 1 つだけで、実効の番号は実の番号と同じである（set-user-ID のプログラムは無い）。
//
// FS はファイルの持ち主を持たないので、すべてのファイルは自分の持ち物に見える（stat の st_uid / st_gid は自分の
// 番号。→ fstat.c / fstatat.c）。chown は Linux と同じく、root なら誰にでも、root でなければ自分（と自分のグループ）
// へだけ変えられる。持ち主を覚える場所が無いので、成功しても stat の見え方は変わらない。

#include <browseros/linux.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/// カーネルに番号を聞く（getuid(2)・getgid(2)）。BrowserOS でないホスト（ENOSYS）では root とみなす
static void credentials(uid_t *uid, gid_t *gid) {
  long u = __bos_syscall0(__BOS_SYS_getuid);
  long g = __bos_syscall0(__BOS_SYS_getgid);
  *uid = u < 0 ? 0 : (uid_t)u;
  *gid = g < 0 ? 0 : (gid_t)g;
}

uid_t getuid(void) {
  long uid = __bos_syscall0(__BOS_SYS_getuid);
  return uid < 0 ? 0 : (uid_t)uid;
}

uid_t geteuid(void) {
  long uid = __bos_syscall0(__BOS_SYS_geteuid);
  return uid < 0 ? 0 : (uid_t)uid;
}

gid_t getgid(void) {
  long gid = __bos_syscall0(__BOS_SYS_getgid);
  return gid < 0 ? 0 : (gid_t)gid;
}

gid_t getegid(void) {
  long gid = __bos_syscall0(__BOS_SYS_getegid);
  return gid < 0 ? 0 : (gid_t)gid;
}

/// 補助グループの表はカーネルが持つ（getgroups(2)・setgroups(2)。root でなければ setgroups は EPERM）
int getgroups(int size, gid_t list[]) {
  return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_getgroups, size, __bos_ptr(list)));
}

int setgroups(size_t count, const gid_t *list) {
  return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_setgroups, count, __bos_ptr(list)));
}
