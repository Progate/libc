// BSD ソケット（socket・socketpair・bind・listen・accept・accept4・connect・getsockname・getpeername・send・sendto・
// sendmsg・recv・recvfrom・recvmsg・getsockopt・setsockopt・shutdown・sockatmark）。
//
// ソケットはカーネルの socket(2) などを呼ぶだけである（→ browser-os の src/wasi/linux/sockets.ts）。種類・受け取った
// オプション・タイムアウト・名前で繋いだ相手はカーネルがソケットごとに持ち、名前に割り当てた番地（240.0.0.0/4）への
// connect もカーネルが名前のまま繋ぐ（→ netdb.c）。
//
// この libc のヘッダーの値（AF_*・SOCK_*・SOL_SOCKET・SHUT_*）は WASI のもので Linux と違うので、ここで Linux の値に
// 直して渡し、受け取った struct sockaddr の族を戻す。struct sockaddr の並びは Linux と同じである。

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

// Linux の値（riscv32 の Linux）
#define LINUX_AF_UNIX 1
#define LINUX_AF_INET 2
#define LINUX_AF_INET6 10
#define LINUX_SOCK_STREAM 1
#define LINUX_SOCK_DGRAM 2
#define LINUX_SOCK_NONBLOCK 04000
#define LINUX_SOCK_CLOEXEC 02000000
#define LINUX_SOL_SOCKET 1

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

static int linux_family(int family) {
  return family == AF_INET ? LINUX_AF_INET : family == AF_INET6 ? LINUX_AF_INET6 : family == AF_UNIX ? LINUX_AF_UNIX : family;
}

static int libc_family(int family) {
  return family == LINUX_AF_INET ? AF_INET : family == LINUX_AF_INET6 ? AF_INET6 : family == LINUX_AF_UNIX ? AF_UNIX : family;
}

static int linux_type(int type) {
  int base = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  int linux = base == SOCK_STREAM ? LINUX_SOCK_STREAM : base == SOCK_DGRAM ? LINUX_SOCK_DGRAM : base;
  if (type & SOCK_NONBLOCK)
    linux |= LINUX_SOCK_NONBLOCK;
  if (type & SOCK_CLOEXEC)
    linux |= LINUX_SOCK_CLOEXEC;
  return linux;
}

static int linux_level(int level) { return level == SOL_SOCKET ? LINUX_SOL_SOCKET : level; }

/// 渡す struct sockaddr の族を Linux の値にした写し。`buffer` に書いて、渡す番地を返す
static const struct sockaddr *to_linux(const struct sockaddr *address, socklen_t length,
                                       struct sockaddr_storage *buffer) {
  if (!address || length < (socklen_t)sizeof(sa_family_t))
    return address;
  memcpy(buffer, address, length < sizeof *buffer ? length : sizeof *buffer);
  ((struct sockaddr *)buffer)->sa_family = linux_family(address->sa_family);
  return (const struct sockaddr *)buffer;
}

/// 受け取った struct sockaddr の族をこの libc の値に戻す
static void from_linux(struct sockaddr *address, const socklen_t *length) {
  if (address && length && *length >= (socklen_t)sizeof(sa_family_t))
    address->sa_family = libc_family(address->sa_family);
}

int socket(int domain, int type, int protocol) {
  return ret(__bos_syscall3(SYS_socket, linux_family(domain), linux_type(type), protocol));
}

int socketpair(int domain, int type, int protocol, int fds[2]) {
  return ret(__bos_syscall4(SYS_socketpair, linux_family(domain), linux_type(type), protocol, __bos_ptr(fds)));
}

int bind(int fd, const struct sockaddr *address, socklen_t length) {
  struct sockaddr_storage buffer;
  return ret(__bos_syscall3(SYS_bind, fd, __bos_ptr(to_linux(address, length, &buffer)), length));
}

int listen(int fd, int backlog) { return ret(__bos_syscall2(SYS_listen, fd, backlog)); }

int accept4(int fd, struct sockaddr *restrict address, socklen_t *restrict length, int flags) {
  int accepted =
      ret(__bos_syscall4(SYS_accept4, fd, __bos_ptr(address), __bos_ptr(length), linux_type(flags) & ~0xf));
  if (accepted >= 0)
    from_linux(address, length);
  return accepted;
}

int accept(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return accept4(fd, address, length, 0);
}

int connect(int fd, const struct sockaddr *address, socklen_t length) {
  struct sockaddr_storage buffer;
  return ret(__bos_syscall3(SYS_connect, fd, __bos_ptr(to_linux(address, length, &buffer)), length));
}

int getsockname(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  int r = ret(__bos_syscall3(SYS_getsockname, fd, __bos_ptr(address), __bos_ptr(length)));
  if (r == 0)
    from_linux(address, length);
  return r;
}

int getpeername(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  int r = ret(__bos_syscall3(SYS_getpeername, fd, __bos_ptr(address), __bos_ptr(length)));
  if (r == 0)
    from_linux(address, length);
  return r;
}

// ソケットはほかのプロセスと話す口なので、送る前と受けた後に MAP_SHARED の写しを同期する（→ memory_sync.c）
ssize_t sendto(int fd, const void *data, size_t length, int flags, const struct sockaddr *to, socklen_t to_length) {
  struct sockaddr_storage buffer;
  __browseros_memory_sync();
  return ret(__browseros_syscall(SYS_sendto, fd, __bos_ptr(data), length, flags, __bos_ptr(to_linux(to, to_length, &buffer)),
                                 to_length));
}

ssize_t send(int fd, const void *data, size_t length, int flags) { return sendto(fd, data, length, flags, 0, 0); }

ssize_t recvfrom(int fd, void *restrict data, size_t length, int flags, struct sockaddr *restrict from,
                 socklen_t *restrict from_length) {
  ssize_t got =
      ret(__browseros_syscall(SYS_recvfrom, fd, __bos_ptr(data), length, flags, __bos_ptr(from), __bos_ptr(from_length)));
  if (got >= 0)
    from_linux(from, from_length);
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
  struct sockaddr_storage buffer;
  struct msghdr copy = *message;
  copy.msg_name = (void *)to_linux(message->msg_name, message->msg_namelen, &buffer);
  __browseros_memory_sync();
  return ret(__bos_syscall3(SYS_sendmsg, fd, __bos_ptr(&copy), flags));
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
  if (!message) {
    errno = EFAULT;
    return -1;
  }
  ssize_t got = ret(__bos_syscall3(SYS_recvmsg, fd, __bos_ptr(message), flags));
  if (got >= 0)
    from_linux(message->msg_name, &message->msg_namelen);
  if (got > 0)
    __browseros_memory_sync();
  return got;
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t length) {
  return ret(__bos_syscall5(SYS_setsockopt, fd, linux_level(level), name, __bos_ptr(value), length));
}

int getsockopt(int fd, int level, int name, void *restrict value, socklen_t *restrict length) {
  int r = ret(__bos_syscall5(SYS_getsockopt, fd, linux_level(level), name, __bos_ptr(value), __bos_ptr(length)));
  // 種類と族はこの libc の値で答える
  if (r == 0 && level == SOL_SOCKET && value && length && *length >= sizeof(int)) {
    int *number = value;
    if (name == SO_TYPE)
      *number = *number == LINUX_SOCK_STREAM ? SOCK_STREAM : *number == LINUX_SOCK_DGRAM ? SOCK_DGRAM : *number;
    else if (name == SO_DOMAIN)
      *number = libc_family(*number);
  }
  return r;
}

int shutdown(int fd, int how) {
  // SHUT_RD・SHUT_WR・SHUT_RDWR は、この libc では 1・2・3、Linux では 0・1・2
  int linux_how = how == SHUT_RD ? 0 : how == SHUT_WR ? 1 : how == SHUT_RDWR ? 2 : -1;
  return ret(__bos_syscall2(SYS_shutdown, fd, linux_how));
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
