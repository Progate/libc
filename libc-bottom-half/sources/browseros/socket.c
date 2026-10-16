// BSD ソケット（socket・socketpair・bind・listen・accept・accept4・connect・getsockname・getpeername・send・sendto・
// sendmsg・recv・recvfrom・recvmsg・getsockopt・setsockopt・shutdown・sockatmark）。
//
// ソケットはカーネルの socket(2) などを呼ぶだけである（→ browser-os の src/wasi/linux/sockets.ts）。種類・受け取った
// オプション・タイムアウト・名前で繋いだ相手はカーネルがソケットごとに持ち、名前に割り当てた番地（240.0.0.0/4）への
// connect もカーネルが名前のまま繋ぐ（→ netdb.c）。
//
// この libc のヘッダーの値（AF_*・SOCK_*・SOL_SOCKET・SO_*・SHUT_*）と struct sockaddr の並びは Linux と同じなので、
// そのまま渡す。

#include <browseros/libc.h>
#include <browseros/linux.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define SYS_socket 198
#define SYS_socketpair 199
#define SYS_bind 200
#define SYS_listen 201
#define SYS_connect 203
#define SYS_getsockname 204
#define SYS_getpeername 205
#define SYS_sendto 206
#define SYS_recvfrom 207
#define SYS_setsockopt 208
#define SYS_getsockopt 209
#define SYS_shutdown 210
#define SYS_sendmsg 211
#define SYS_recvmsg 212
#define SYS_accept4 242

static int ret(long r) { return __browseros_syscall_ret(r); }

int socket(int domain, int type, int protocol) {
  return ret(__bos_syscall3(SYS_socket, domain, type, protocol));
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
  return ret(__bos_syscall4(SYS_socketpair, domain, type, protocol, __bos_ptr(fds)));
}

int bind(int fd, const struct sockaddr *address, socklen_t length) {
  return ret(__bos_syscall3(SYS_bind, fd, __bos_ptr(address), length));
}

int listen(int fd, int backlog) { return ret(__bos_syscall2(SYS_listen, fd, backlog)); }

int accept4(int fd, struct sockaddr *restrict address, socklen_t *restrict length, int flags) {
  return ret(__bos_syscall4(SYS_accept4, fd, __bos_ptr(address), __bos_ptr(length), flags));
}

int accept(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return accept4(fd, address, length, 0);
}

int connect(int fd, const struct sockaddr *address, socklen_t length) {
  return ret(__bos_syscall3(SYS_connect, fd, __bos_ptr(address), length));
}

int getsockname(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return ret(__bos_syscall3(SYS_getsockname, fd, __bos_ptr(address), __bos_ptr(length)));
}

int getpeername(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return ret(__bos_syscall3(SYS_getpeername, fd, __bos_ptr(address), __bos_ptr(length)));
}

// ソケットはほかのプロセスと話す口なので、送る前と受けた後に MAP_SHARED の写しを同期する（→ memory_sync.c）
ssize_t sendto(int fd, const void *data, size_t length, int flags, const struct sockaddr *to, socklen_t to_length) {
  __browseros_memory_sync();
  return ret(__browseros_syscall(SYS_sendto, fd, __bos_ptr(data), length, flags, __bos_ptr(to), to_length));
}

ssize_t send(int fd, const void *data, size_t length, int flags) { return sendto(fd, data, length, flags, 0, 0); }

ssize_t recvfrom(int fd, void *restrict data, size_t length, int flags, struct sockaddr *restrict from,
                 socklen_t *restrict from_length) {
  ssize_t got =
      ret(__browseros_syscall(SYS_recvfrom, fd, __bos_ptr(data), length, flags, __bos_ptr(from), __bos_ptr(from_length)));
  if (got > 0)
    __browseros_memory_sync();
  return got;
}

ssize_t recv(int fd, void *data, size_t length, int flags) { return recvfrom(fd, data, length, flags, 0, 0); }

ssize_t sendmsg(int fd, const struct msghdr *message, int flags) {
  if (!message) {
    errno = EFAULT;
    return -1;
  }
  __browseros_memory_sync();
  return ret(__bos_syscall3(SYS_sendmsg, fd, __bos_ptr(message), flags));
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
  if (!message) {
    errno = EFAULT;
    return -1;
  }
  ssize_t got = ret(__bos_syscall3(SYS_recvmsg, fd, __bos_ptr(message), flags));
  if (got > 0)
    __browseros_memory_sync();
  return got;
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t length) {
  return ret(__bos_syscall5(SYS_setsockopt, fd, level, name, __bos_ptr(value), length));
}

int getsockopt(int fd, int level, int name, void *restrict value, socklen_t *restrict length) {
  return ret(__bos_syscall5(SYS_getsockopt, fd, level, name, __bos_ptr(value), __bos_ptr(length)));
}

int shutdown(int fd, int how) {
  return ret(__bos_syscall2(SYS_shutdown, fd, how));
}

/// 帯域外のデータは運ばないので、印に来ていることは無い
int sockatmark(int fd) {
  int type;
  socklen_t length = sizeof type;
  if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &length) != 0)
    return -1;
  return 0;
}

// dup・close がソケットとして覚えていたことを写す・忘れる口は、カーネルがソケットの状態を持つので何もしない
void __browseros_socket_forget(int fd) { (void)fd; }

void __browseros_socket_copy(int from, int to) {
  (void)from;
  (void)to;
}
