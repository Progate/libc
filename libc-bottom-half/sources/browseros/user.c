// 利用者とグループ: getuid(2)・getgid(2)・getgroups(2) と chown(2)・fchown(2)・lchown(2)・fchownat(2)。
//
// この OS の利用者は 1 人で、番号はカーネルが決める（root なら 0、それ以外は 1000。`id` と同じ決め方）。グループも
// 同じ番号の 1 つだけで、実効の番号は実の番号と同じである（set-user-ID のプログラムは無い）。
//
// FS はファイルの持ち主を持たないので、すべてのファイルは自分の持ち物に見える（stat の st_uid / st_gid は自分の
// 番号。→ fstat.c / fstatat.c）。chown は Linux と同じく、root なら誰にでも、root でなければ自分（と自分のグループ）
// へだけ変えられる。持ち主を覚える場所が無いので、成功しても stat の見え方は変わらない。

#include <browseros/host.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/// カーネルに番号を聞く。BrowserOS でないホスト（ENOSYS）では root とみなす（wasi-libc の世界には利用者が居ない）
static void credentials(uid_t *uid, gid_t *gid) {
  uint32_t u, g;
  if (__browseros_credentials(&u, &g) != 0)
    u = g = 0;
  *uid = u;
  *gid = g;
}

uid_t getuid(void) {
  uid_t uid;
  gid_t gid;
  credentials(&uid, &gid);
  return uid;
}

uid_t geteuid(void) { return getuid(); }

gid_t getgid(void) {
  uid_t uid;
  gid_t gid;
  credentials(&uid, &gid);
  return gid;
}

gid_t getegid(void) { return getgid(); }

/*
 * 補助グループの表はこのプロセス（モジュール）の中に持つ。カーネルは利用者とグループを 1 つずつしか知らないので、
 * ここで覚えないと setgroups で入れた分が getgroups に返ってこない。初期値は自分のグループ 1 つだけ
 */
static gid_t supplementary[NGROUPS_MAX];
static int supplementary_len = -1;

static int supplementary_default(void) {
  if (supplementary_len < 0) {
    supplementary[0] = getgid();
    supplementary_len = 1;
  }
  return supplementary_len;
}

int getgroups(int size, gid_t list[]) {
  int count = supplementary_default();
  if (size == 0)
    return count;
  if (size < count) {
    errno = EINVAL;
    return -1;
  }
  for (int i = 0; i < count; i++)
    list[i] = supplementary[i];
  return count;
}

/*
 * 補助グループを入れ替える。Linux と同じく root（uid 0）だけが変えられる——この OS の利用者は
 * 権限を上げる仕組みが無いので、root でなければ常に EPERM である（CAP_SETGID を持たないのと同じ）
 */
int setgroups(size_t count, const gid_t *list) {
  uid_t uid;
  gid_t gid;
  credentials(&uid, &gid);
  if (uid != 0) {
    errno = EPERM;
    return -1;
  }
  if (count > NGROUPS_MAX) {
    errno = EINVAL;
    return -1;
  }
  for (size_t i = 0; i < count; i++)
    supplementary[i] = list[i];
  supplementary_len = (int)count;
  return 0;
}

/// 持ち主を owner / group に変えてよいか（-1 は変えない）。在るかどうかは呼ぶ側が先に確かめる
static int may_change_owner(uid_t owner, gid_t group) {
  uid_t uid;
  gid_t gid;
  credentials(&uid, &gid);
  if (uid == 0)
    return 0;
  if ((owner != (uid_t)-1 && owner != uid) || (group != (gid_t)-1 && group != gid)) {
    errno = EPERM;
    return -1;
  }
  return 0;
}

int fchownat(int fd, const char *path, uid_t owner, gid_t group, int flag) {
  struct stat st;
  if (fstatat(fd, path, &st, flag) != 0)
    return -1;
  return may_change_owner(owner, group);
}

int chown(const char *path, uid_t owner, gid_t group) {
  return fchownat(AT_FDCWD, path, owner, group, 0);
}

// この FS にシンボリックリンクは無いので、リンクそのものの持ち主を変える lchown は chown と同じになる
int lchown(const char *path, uid_t owner, gid_t group) {
  return fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}

int fchown(int fd, uid_t owner, gid_t group) {
  struct stat st;
  if (fstat(fd, &st) != 0)
    return -1;
  return may_change_owner(owner, group);
}
