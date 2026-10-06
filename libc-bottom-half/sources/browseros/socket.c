// BSD ソケット（socket・bind・listen・accept・connect・send・recv・sendto・recvfrom・sendmsg・recvmsg・
// getsockname・getpeername・getsockopt・setsockopt・socketpair）。
//
// WASI preview1 の標準には sock_accept・sock_recv・sock_send・sock_shutdown しか無いが、BrowserOS のカーネル
// （@progate/browser-wasi の p1/sockets.ts）は WasmEdge の拡張と同じ名前と引数の syscall（sock_open・sock_bind・
// sock_listen・sock_connect・sock_getlocaladdr・sock_getpeeraddr・sock_recv_from・sock_send_to・sock_getsockopt・
// sock_setsockopt）でカーネルのソケット表を渡す。この libc はそれを POSIX の関数に直す。TCP・UDP・UNIX ドメインの
// ソケットはカーネルの表にあり、ポート 0 を割り当てて getsockname で読み戻すこともできる。
//
// カーネルが持たないのは名前解決の答えである。外へ出る接続はカーネルの gateway が名前のまま繋ぐ（Host・SNI・CORS は
// 名前で決まる）が、ソケットの API は名前を運べない。そこで getaddrinfo は名前に予約済みの 240.0.0.0/4 の番地を
// 1 つ割り当てて覚え（→ netdb.c）、connect はその番地を名前に戻して `/dev/tcp/<名前>/<ポート>` を開き、
// fd_renumber でソケットの番号へ移す（カーネルの名前のままの接続。→ browser-os の src/net/dev-socket.ts）。
//
// ソケットの種類・受け取ったオプション・名前で繋いだ相手は、カーネルの表に持たせる場所が無いので libc が fd ごとに
// 覚える。close・dup2 で消し、dup で写す（→ close.c・dup.c の __browseros_socket_*）。覚えていない fd
// （posix_spawn で継いだもの）は、カーネルがソケットと答えれば TCP のソケットとして扱う。

#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stddef.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <wasi/api.h>

#define WASI_IMPORT(name) __attribute__((import_module("wasi_snapshot_preview1"), import_name(name)))

// カーネルとやりとりするアドレスの入れ物（WasmEdge の addressBuffer）。ゲストから渡すときは生の番地だけ
// （IPv4 は 4 バイト、IPv6 は 16 バイト）か、UNIX ドメインなら 128 バイトの rawSockaddrAny で、
// カーネルから受け取るときはいつも rawSockaddrAny（family の u16 と 126 バイト）になる
typedef struct {
  uint8_t *buf;
  uint32_t buf_len;
} address_buffer;

#define RAW_SOCKADDR_BYTES 128

// カーネルの syscall の種類の値（WasmEdge と同じ）。アドレスの族の値は libc の AF_INET・AF_INET6・AF_UNIX と同じ
#define KERNEL_SOCK_DGRAM 1
#define KERNEL_SOCK_STREAM 2
#define KERNEL_SOL_SOCKET 0
#define KERNEL_SO_REUSEADDR 0
#define KERNEL_SO_ERROR 2
#define KERNEL_SO_BROADCAST 4

WASI_IMPORT("sock_open") int32_t __browseros_sock_open(int32_t family, int32_t type, int32_t *fd);
WASI_IMPORT("sock_bind") int32_t __browseros_sock_bind(int32_t fd, address_buffer *address, int32_t port);
WASI_IMPORT("sock_listen") int32_t __browseros_sock_listen(int32_t fd, int32_t backlog);
WASI_IMPORT("sock_connect") int32_t __browseros_sock_connect(int32_t fd, address_buffer *address, int32_t port);
WASI_IMPORT("sock_getlocaladdr")
int32_t __browseros_sock_getlocaladdr(int32_t fd, address_buffer *address, uint32_t *port);
WASI_IMPORT("sock_getpeeraddr")
int32_t __browseros_sock_getpeeraddr(int32_t fd, address_buffer *address, uint32_t *port);
WASI_IMPORT("sock_recv_from")
int32_t __browseros_sock_recv_from(int32_t fd, const __wasi_iovec_t *iovs, size_t iovs_len, address_buffer *address,
                                   int32_t flags, uint32_t *port, size_t *nread, uint16_t *oflags);
WASI_IMPORT("sock_send_to")
int32_t __browseros_sock_send_to(int32_t fd, const __wasi_ciovec_t *iovs, size_t iovs_len, address_buffer *address,
                                 int32_t port, int32_t flags, size_t *nwritten);
WASI_IMPORT("sock_setsockopt")
int32_t __browseros_sock_setsockopt(int32_t fd, int32_t level, int32_t name, const void *value, uint32_t length);
WASI_IMPORT("sock_getsockopt")
int32_t __browseros_sock_getsockopt(int32_t fd, int32_t level, int32_t name, void *value, uint32_t length);

// 名前で繋ぐときに開くカーネルの装置（→ browser-os の src/net/dev-socket.ts）
#define TCP_ROOT "/dev/tcp"

// libc が fd ごとに覚えること
struct socket_state {
  int known;
  int domain;
  int type;
  int protocol;
  int listening;
  // SO_RCVTIMEO・SO_SNDTIMEO。カーネルの待ちには期限が無いので、libc が poll で待ってから読み書きする
  struct timeval receive_timeout;
  struct timeval send_timeout;
  // 受け取って覚えておくだけのオプション（接続の調整は外の TCP の持ち主である gateway が決める）。読み戻せる
  int reuse_address;
  int reuse_port;
  int keep_alive;
  int broadcast;
  int no_delay;
  int send_buffer;
  int receive_buffer;
  struct linger linger;
  // 名前で繋いだ相手（getaddrinfo が名前に割り当てた番地）。カーネルは名前しか知らないので libc が答える
  int has_named_peer;
  struct sockaddr_storage named_peer;
  socklen_t named_peer_length;
};

static struct socket_state *states;
static size_t state_count;
static pthread_mutex_t states_lock = PTHREAD_MUTEX_INITIALIZER;

// Linux の既定の送受信のバッファの大きさ（読み戻したときの値）
#define DEFAULT_BUFFER_SIZE 212992

static int fail(int error) {
  errno = error;
  return -1;
}

// fd がカーネルのソケットか（fdstat の種類で見る）
static int kernel_socket(int fd) {
  __wasi_fdstat_t stat;
  if (__wasi_fd_fdstat_get(fd, &stat) != 0)
    return -EBADF;
  return stat.fs_filetype == __WASI_FILETYPE_SOCKET_STREAM || stat.fs_filetype == __WASI_FILETYPE_SOCKET_DGRAM;
}

// fd の覚えを写して返す。覚えていなければ、カーネルがソケットと答えた fd は TCP のソケットとして扱う。
// ソケットでなければ ENOTSOCK、開いていなければ EBADF で -1
static int lookup(int fd, struct socket_state *out) {
  if (fd < 0)
    return fail(EBADF);
  pthread_mutex_lock(&states_lock);
  int found = (size_t)fd < state_count && states[fd].known;
  if (found)
    *out = states[fd];
  pthread_mutex_unlock(&states_lock);
  if (found)
    return 0;
  int kind = kernel_socket(fd);
  if (kind < 0)
    return fail(-kind);
  if (!kind)
    return fail(ENOTSOCK);
  memset(out, 0, sizeof *out);
  out->known = 1;
  out->domain = AF_INET;
  out->type = SOCK_STREAM;
  out->protocol = IPPROTO_TCP;
  out->send_buffer = out->receive_buffer = DEFAULT_BUFFER_SIZE;
  return 0;
}

// fd の覚えを置き換える。足りなければ表を伸ばす
static int store(int fd, const struct socket_state *state) {
  pthread_mutex_lock(&states_lock);
  if ((size_t)fd >= state_count) {
    size_t count = state_count ? state_count : 16;
    while (count <= (size_t)fd)
      count *= 2;
    struct socket_state *grown = realloc(states, count * sizeof *grown);
    if (grown == NULL) {
      pthread_mutex_unlock(&states_lock);
      return fail(ENOMEM);
    }
    memset(grown + state_count, 0, (count - state_count) * sizeof *grown);
    states = grown;
    state_count = count;
  }
  states[fd] = *state;
  pthread_mutex_unlock(&states_lock);
  return 0;
}

void __browseros_socket_forget(int fd) {
  pthread_mutex_lock(&states_lock);
  if (fd >= 0 && (size_t)fd < state_count)
    states[fd].known = 0;
  pthread_mutex_unlock(&states_lock);
}

void __browseros_socket_copy(int from, int to) {
  struct socket_state state;
  pthread_mutex_lock(&states_lock);
  int found = from >= 0 && (size_t)from < state_count && states[from].known;
  if (found)
    state = states[from];
  pthread_mutex_unlock(&states_lock);
  if (found)
    store(to, &state);
  else
    __browseros_socket_forget(to);
}

static int kernel_family(int domain) {
  return domain == AF_INET || domain == AF_INET6 || domain == AF_UNIX ? domain : -1;
}

// libc の sockaddr をカーネルへ渡す形にする。名前に割り当てた番地なら、その名前を host に入れて 1 を返す
static int to_kernel(const struct sockaddr *address, socklen_t length, int domain, uint8_t *raw,
                     address_buffer *buffer, int *port, char *host, size_t host_size) {
  if (address == NULL)
    return fail(EFAULT);
  if (length < (socklen_t)sizeof(sa_family_t))
    return fail(EINVAL);
  if (address->sa_family != domain && !(domain == AF_INET6 && address->sa_family == AF_INET))
    return fail(EAFNOSUPPORT);
  memset(raw, 0, RAW_SOCKADDR_BYTES);
  switch (address->sa_family) {
  case AF_INET: {
    if (length < (socklen_t)sizeof(struct sockaddr_in))
      return fail(EINVAL);
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    int named = __browseros_name_of_address(ntohl(in->sin_addr.s_addr), host, host_size);
    // 名前の範囲なのに getaddrinfo が割り当てていない番地は、作り話の番地で繋ぐ先が無い
    if (named < 0)
      return fail(EHOSTUNREACH);
    if (named) {
      *port = ntohs(in->sin_port);
      return 1;
    }
    memcpy(raw, &in->sin_addr, 4);
    buffer->buf = raw;
    buffer->buf_len = 4;
    *port = ntohs(in->sin_port);
    return 0;
  }
  case AF_INET6: {
    if (length < (socklen_t)sizeof(struct sockaddr_in6))
      return fail(EINVAL);
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
    memcpy(raw, &in6->sin6_addr, 16);
    buffer->buf = raw;
    buffer->buf_len = 16;
    *port = ntohs(in6->sin6_port);
    return 0;
  }
  case AF_UNIX: {
    const struct sockaddr_un *un = (const struct sockaddr_un *)address;
    size_t path_length = length - offsetof(struct sockaddr_un, sun_path);
    if (length <= offsetof(struct sockaddr_un, sun_path) || path_length > sizeof un->sun_path)
      return fail(EINVAL);
    size_t used = strnlen(un->sun_path, path_length);
    if (used == 0 || used > RAW_SOCKADDR_BYTES - 3)
      return fail(EINVAL);
    raw[0] = AF_UNIX;
    memcpy(raw + 2, un->sun_path, used);
    buffer->buf = raw;
    buffer->buf_len = RAW_SOCKADDR_BYTES;
    *port = 0;
    return 0;
  }
  default:
    return fail(EAFNOSUPPORT);
  }
}

// カーネルが書いた rawSockaddrAny を libc の sockaddr にして、呼んだ側の入れ物へ切り詰めて写す（POSIX と同じく、
// *length には本当の長さを書く）
static void from_kernel(const uint8_t *raw, uint32_t port, struct sockaddr *address, socklen_t *length) {
  struct sockaddr_storage storage;
  socklen_t actual;
  memset(&storage, 0, sizeof storage);
  uint16_t family = (uint16_t)(raw[0] | raw[1] << 8);
  if (family == AF_INET6) {
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&storage;
    in6->sin6_family = AF_INET6;
    in6->sin6_port = htons((uint16_t)port);
    memcpy(&in6->sin6_addr, raw + 2, 16);
    actual = sizeof *in6;
  } else if (family == AF_UNIX) {
    struct sockaddr_un *un = (struct sockaddr_un *)&storage;
    un->sun_family = AF_UNIX;
    size_t used = strnlen((const char *)raw + 2, RAW_SOCKADDR_BYTES - 2);
    memcpy(un->sun_path, raw + 2, used);
    actual = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + used + 1);
  } else {
    struct sockaddr_in *in = (struct sockaddr_in *)&storage;
    in->sin_family = AF_INET;
    in->sin_port = htons((uint16_t)port);
    memcpy(&in->sin_addr, raw + 2, 4);
    actual = sizeof *in;
  }
  if (address != NULL && length != NULL) {
    memcpy(address, &storage, *length < actual ? *length : actual);
    *length = actual;
  }
}

int socket(int domain, int type, int protocol) {
  int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (kernel_family(domain) < 0)
    return fail(EAFNOSUPPORT);
  if (type != SOCK_STREAM && type != SOCK_DGRAM)
    return fail(EPROTOTYPE);
  if (domain != AF_UNIX && protocol != 0 &&
      protocol != (type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP))
    return fail(EPROTONOSUPPORT);
  int32_t fd;
  int32_t error = __browseros_sock_open(domain, type == SOCK_STREAM ? KERNEL_SOCK_STREAM : KERNEL_SOCK_DGRAM, &fd);
  if (error != 0)
    return fail(error);
  struct socket_state state;
  memset(&state, 0, sizeof state);
  state.known = 1;
  state.domain = domain;
  state.type = type;
  state.protocol = domain == AF_UNIX ? 0 : type == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
  state.send_buffer = state.receive_buffer = DEFAULT_BUFFER_SIZE;
  if (store(fd, &state) != 0) {
    close(fd);
    return -1;
  }
  if ((flags & SOCK_NONBLOCK) && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK) != 0) {
    int saved = errno;
    close(fd);
    return fail(saved);
  }
  if (flags & SOCK_CLOEXEC)
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  return fd;
}

int bind(int fd, const struct sockaddr *address, socklen_t length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  uint8_t raw[RAW_SOCKADDR_BYTES];
  address_buffer buffer;
  int port;
  // 名前に割り当てた番地は名前の代わりで、この世界のどこにも無いので結べない
  char host[1];
  int named = to_kernel(address, length, state.domain, raw, &buffer, &port, host, sizeof host);
  if (named < 0)
    return errno == EHOSTUNREACH ? fail(EADDRNOTAVAIL) : -1;
  if (named)
    return fail(EADDRNOTAVAIL);
  int32_t error = __browseros_sock_bind(fd, &buffer, port);
  return error != 0 ? fail(error) : 0;
}

int listen(int fd, int backlog) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (state.type != SOCK_STREAM)
    return fail(EOPNOTSUPP);
  int32_t error = __browseros_sock_listen(fd, backlog < 0 ? 0 : backlog);
  if (error != 0)
    return fail(error);
  state.listening = 1;
  return store(fd, &state);
}

// 期限つきの待ち。期限が無ければすぐ 1。期限までに用意できなければ EAGAIN（Linux の SO_RCVTIMEO と同じ）
static int wait_ready(int fd, short events, const struct timeval *timeout) {
  if (timeout->tv_sec == 0 && timeout->tv_usec == 0)
    return 1;
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0 && (flags & O_NONBLOCK))
    return 1;
  struct pollfd entry = {.fd = fd, .events = events};
  long long ms = (long long)timeout->tv_sec * 1000 + timeout->tv_usec / 1000;
  int ready = poll(&entry, 1, ms > 0x7fffffff ? 0x7fffffff : (int)ms);
  if (ready < 0)
    return -1;
  if (ready == 0)
    return fail(EAGAIN);
  return 1;
}

static int accept_common(int fd, struct sockaddr *address, socklen_t *length, int flags) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (flags & ~(SOCK_NONBLOCK | SOCK_CLOEXEC))
    return fail(EINVAL);
  if (!state.listening && state.type != SOCK_STREAM)
    return fail(EOPNOTSUPP);
  if (wait_ready(fd, POLLIN, &state.receive_timeout) < 0)
    return -1;
  int32_t accepted;
  __wasi_errno_t error = __wasi_sock_accept(fd, (flags & SOCK_NONBLOCK) ? __WASI_FDFLAGS_NONBLOCK : 0,
                                            (__wasi_fd_t *)&accepted);
  if (error != 0)
    return fail(error);
  struct socket_state child;
  memset(&child, 0, sizeof child);
  child.known = 1;
  child.domain = state.domain;
  child.type = SOCK_STREAM;
  child.protocol = state.protocol;
  child.send_buffer = child.receive_buffer = DEFAULT_BUFFER_SIZE;
  store(accepted, &child);
  if (flags & SOCK_NONBLOCK)
    fcntl(accepted, F_SETFL, fcntl(accepted, F_GETFL) | O_NONBLOCK);
  if (flags & SOCK_CLOEXEC)
    fcntl(accepted, F_SETFD, FD_CLOEXEC);
  if (address != NULL && length != NULL && getpeername(accepted, address, length) != 0) {
    // 相手の番地が取れないこと自体は受け付けの失敗ではない（UNIX ドメインの名前の無い相手など）
    memset(address, 0, *length);
    if (*length >= (socklen_t)sizeof(sa_family_t))
      address->sa_family = state.domain;
    *length = sizeof(sa_family_t);
  }
  return accepted;
}

int accept(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return accept_common(fd, address, length, 0);
}

int accept4(int fd, struct sockaddr *restrict address, socklen_t *restrict length, int flags) {
  return accept_common(fd, address, length, flags);
}

int connect(int fd, const struct sockaddr *address, socklen_t length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (state.listening)
    return fail(EISCONN);
  uint8_t raw[RAW_SOCKADDR_BYTES];
  address_buffer buffer;
  int port;
  char host[256];
  int named = to_kernel(address, length, state.domain, raw, &buffer, &port, host, sizeof host);
  if (named < 0)
    return -1;
  if (!named) {
    int32_t error = __browseros_sock_connect(fd, &buffer, port);
    return error != 0 ? fail(error) : 0;
  }
  if (state.type != SOCK_STREAM)
    return fail(EAFNOSUPPORT);
  // 名前で繋ぐ。開くと繋がるので、繋がった fd をソケットの番号へ移す（番号は呼んだ側が持っているもののまま）
  char path[sizeof host + 32];
  snprintf(path, sizeof path, TCP_ROOT "/%s/%d", host, port);
  int flags = fcntl(fd, F_GETFL);
  int fd_flags = fcntl(fd, F_GETFD);
  int connected = open(path, O_RDWR);
  if (connected < 0)
    return fail(errno == ENOENT ? ENETUNREACH : errno);
  __wasi_errno_t error = __wasi_fd_renumber(connected, fd);
  if (error != 0) {
    close(connected);
    return fail(error);
  }
  if (flags >= 0 && (flags & O_NONBLOCK))
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  if (fd_flags >= 0 && (fd_flags & FD_CLOEXEC))
    fcntl(fd, F_SETFD, FD_CLOEXEC);
  state.has_named_peer = 1;
  memcpy(&state.named_peer, address, sizeof(struct sockaddr_in));
  state.named_peer_length = sizeof(struct sockaddr_in);
  return store(fd, &state);
}

static int name_of(int fd, struct sockaddr *address, socklen_t *length, int peer) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (address == NULL || length == NULL)
    return fail(EFAULT);
  if (peer && state.has_named_peer) {
    memcpy(address, &state.named_peer, *length < state.named_peer_length ? *length : state.named_peer_length);
    *length = state.named_peer_length;
    return 0;
  }
  uint8_t raw[RAW_SOCKADDR_BYTES];
  address_buffer buffer = {.buf = raw, .buf_len = sizeof raw};
  uint32_t port = 0;
  int32_t error = peer ? __browseros_sock_getpeeraddr(fd, &buffer, &port)
                       : __browseros_sock_getlocaladdr(fd, &buffer, &port);
  if (error != 0) {
    // 名前で繋いだ接続（覚えを失ったもの）の自分の側は、カーネルが外の TCP を持っていないので答えが無い。
    // Linux で繋ぐ前の getsockname と同じく、族だけの「どこでもない」番地を返す
    if (!peer && state.domain != AF_UNIX) {
      struct sockaddr_in any;
      memset(&any, 0, sizeof any);
      any.sin_family = AF_INET;
      memcpy(address, &any, *length < sizeof any ? *length : sizeof any);
      *length = sizeof any;
      return 0;
    }
    return fail(error == EINVAL && peer ? ENOTCONN : error);
  }
  from_kernel(raw, port, address, length);
  return 0;
}

int getsockname(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return name_of(fd, address, length, 0);
}

int getpeername(int fd, struct sockaddr *restrict address, socklen_t *restrict length) {
  return name_of(fd, address, length, 1);
}

// iovec を受け取る送り。stream は sock_send、データグラムは宛先つきの sock_send_to
static ssize_t send_vector(int fd, const struct iovec *iov, size_t count, int flags, const struct sockaddr *to,
                           socklen_t to_length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (flags & ~(MSG_DONTWAIT | MSG_NOSIGNAL | MSG_MORE | MSG_EOR | MSG_OOB))
    return fail(EOPNOTSUPP);
  if (!(flags & MSG_DONTWAIT) && wait_ready(fd, POLLOUT, &state.send_timeout) < 0)
    return -1;
  size_t written = 0;
  __wasi_errno_t error;
  if (state.type == SOCK_DGRAM) {
    uint8_t raw[RAW_SOCKADDR_BYTES];
    address_buffer buffer = {.buf = raw, .buf_len = 0};
    int port = 0;
    if (to != NULL) {
      char host[1];
      int named = to_kernel(to, to_length, state.domain, raw, &buffer, &port, host, sizeof host);
      if (named < 0)
        return -1;
      // 名前のままのデータグラムは gateway が運べない
      if (named)
        return fail(ENETUNREACH);
    } else {
      // 宛先の無いデータグラムは、connect した相手へ送る
      struct sockaddr_storage peer;
      socklen_t peer_length = sizeof peer;
      if (name_of(fd, (struct sockaddr *)&peer, &peer_length, 1) != 0)
        return fail(EDESTADDRREQ);
      char host[1];
      if (to_kernel((struct sockaddr *)&peer, peer_length, state.domain, raw, &buffer, &port, host, sizeof host) != 0)
        return fail(EDESTADDRREQ);
    }
    error = __browseros_sock_send_to(fd, (const __wasi_ciovec_t *)iov, count, &buffer, port, 0, &written);
  } else {
    // 繋がったストリームでは宛先は使わない（Linux の TCP と同じ）
    error = __wasi_sock_send(fd, (const __wasi_ciovec_t *)iov, count, 0, &written);
  }
  if (error != 0) {
    errno = error;
    if (error == EPIPE && !(flags & MSG_NOSIGNAL))
      __browseros_broken_pipe();
    return -1;
  }
  return (ssize_t)written;
}

// iovec へ受け取る。データグラムは送り主の番地も返す
static ssize_t receive_vector(int fd, struct iovec *iov, size_t count, int flags, struct sockaddr *from,
                              socklen_t *from_length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  if (flags & ~(MSG_PEEK | MSG_WAITALL | MSG_DONTWAIT | MSG_TRUNC))
    return fail(EOPNOTSUPP);
  if (flags & MSG_DONTWAIT) {
    struct pollfd entry = {.fd = fd, .events = POLLIN};
    if (poll(&entry, 1, 0) == 0)
      return fail(EAGAIN);
  } else if (wait_ready(fd, POLLIN, &state.receive_timeout) < 0) {
    return -1;
  }
  int kernel_flags = flags & (MSG_PEEK | MSG_WAITALL);
  size_t got = 0;
  __wasi_errno_t error;
  if (state.type == SOCK_DGRAM || from != NULL) {
    uint8_t raw[RAW_SOCKADDR_BYTES];
    memset(raw, 0, sizeof raw);
    address_buffer buffer = {.buf = raw, .buf_len = sizeof raw};
    uint32_t port = 0;
    uint16_t oflags = 0;
    error = __browseros_sock_recv_from(fd, (const __wasi_iovec_t *)iov, count, &buffer, kernel_flags, &port, &got,
                                       &oflags);
    if (error == 0 && from != NULL && from_length != NULL) {
      if (state.has_named_peer) {
        memcpy(from, &state.named_peer, *from_length < state.named_peer_length ? *from_length : state.named_peer_length);
        *from_length = state.named_peer_length;
      } else if (raw[0] != 0 || raw[1] != 0) {
        from_kernel(raw, port, from, from_length);
      } else {
        *from_length = 0;
      }
    }
  } else {
    __wasi_roflags_t oflags;
    error = __wasi_sock_recv(fd, (const __wasi_iovec_t *)iov, count, kernel_flags, &got, &oflags);
  }
  if (error != 0)
    return fail(error);
  return (ssize_t)got;
}

ssize_t send(int fd, const void *data, size_t length, int flags) {
  struct iovec iov = {.iov_base = (void *)data, .iov_len = length};
  return send_vector(fd, &iov, 1, flags, NULL, 0);
}

ssize_t sendto(int fd, const void *data, size_t length, int flags, const struct sockaddr *to, socklen_t to_length) {
  struct iovec iov = {.iov_base = (void *)data, .iov_len = length};
  return send_vector(fd, &iov, 1, flags, to, to_length);
}

ssize_t recv(int fd, void *data, size_t length, int flags) {
  struct iovec iov = {.iov_base = data, .iov_len = length};
  return receive_vector(fd, &iov, 1, flags, NULL, NULL);
}

ssize_t recvfrom(int fd, void *restrict data, size_t length, int flags, struct sockaddr *restrict from,
                 socklen_t *restrict from_length) {
  struct iovec iov = {.iov_base = data, .iov_len = length};
  return receive_vector(fd, &iov, 1, flags, from, from_length);
}

// 補助データ（SCM_RIGHTS で fd を渡すなど）はカーネルが運べないので、付いていれば断る
ssize_t sendmsg(int fd, const struct msghdr *message, int flags) {
  if (message == NULL)
    return fail(EFAULT);
  if (message->msg_controllen != 0)
    return fail(EOPNOTSUPP);
  return send_vector(fd, message->msg_iov, message->msg_iovlen, flags, message->msg_name, message->msg_namelen);
}

ssize_t recvmsg(int fd, struct msghdr *message, int flags) {
  if (message == NULL)
    return fail(EFAULT);
  socklen_t length = message->msg_namelen;
  ssize_t got = receive_vector(fd, message->msg_iov, message->msg_iovlen, flags, message->msg_name,
                               message->msg_name != NULL ? &length : NULL);
  if (got < 0)
    return -1;
  if (message->msg_name != NULL)
    message->msg_namelen = length;
  message->msg_controllen = 0;
  message->msg_flags = 0;
  return got;
}

static int read_int(const void *value, socklen_t length, int *out) {
  if (value == NULL || length < (socklen_t)sizeof(int))
    return fail(EINVAL);
  memcpy(out, value, sizeof(int));
  return 0;
}

static int read_timeval(const void *value, socklen_t length, struct timeval *out) {
  if (value == NULL || length < (socklen_t)sizeof(struct timeval))
    return fail(EINVAL);
  memcpy(out, value, sizeof *out);
  if (out->tv_usec < 0 || out->tv_usec >= 1000000)
    return fail(EDOM);
  return 0;
}

int setsockopt(int fd, int level, int name, const void *value, socklen_t length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  int number;
  if (level == SOL_SOCKET) {
    switch (name) {
    case SO_RCVTIMEO:
      if (read_timeval(value, length, &state.receive_timeout) != 0)
        return -1;
      return store(fd, &state);
    case SO_SNDTIMEO:
      if (read_timeval(value, length, &state.send_timeout) != 0)
        return -1;
      return store(fd, &state);
    case SO_LINGER:
      if (value == NULL || length < (socklen_t)sizeof(struct linger))
        return fail(EINVAL);
      memcpy(&state.linger, value, sizeof state.linger);
      return store(fd, &state);
    case SO_REUSEADDR:
    case SO_BROADCAST: {
      if (read_int(value, length, &number) != 0)
        return -1;
      // カーネルの表は、ポートを聞く側が閉じた時点で空ける（TIME_WAIT が無い）ので、知らせても振る舞いは変わらない
      int32_t kernel_value = number != 0;
      __browseros_sock_setsockopt(fd, KERNEL_SOL_SOCKET,
                                  name == SO_REUSEADDR ? KERNEL_SO_REUSEADDR : KERNEL_SO_BROADCAST, &kernel_value,
                                  sizeof kernel_value);
      if (name == SO_REUSEADDR)
        state.reuse_address = number != 0;
      else
        state.broadcast = number != 0;
      return store(fd, &state);
    }
    case SO_REUSEPORT:
    case SO_KEEPALIVE:
    case SO_SNDBUF:
    case SO_RCVBUF:
      if (read_int(value, length, &number) != 0)
        return -1;
      if (name == SO_REUSEPORT)
        state.reuse_port = number != 0;
      else if (name == SO_KEEPALIVE)
        state.keep_alive = number != 0;
      // Linux は頼まれた大きさの倍を持つ（読み戻すと倍になる）
      else if (name == SO_SNDBUF)
        state.send_buffer = number * 2;
      else
        state.receive_buffer = number * 2;
      return store(fd, &state);
    default:
      return fail(ENOPROTOOPT);
    }
  }
  if (level == IPPROTO_TCP && state.domain != AF_UNIX && state.type == SOCK_STREAM) {
    switch (name) {
    case TCP_NODELAY:
      if (read_int(value, length, &number) != 0)
        return -1;
      state.no_delay = number != 0;
      return store(fd, &state);
    case TCP_KEEPIDLE:
    case TCP_KEEPINTVL:
    case TCP_KEEPCNT:
      if (read_int(value, length, &number) != 0)
        return -1;
      return number < 1 ? fail(EINVAL) : 0;
    default:
      return fail(ENOPROTOOPT);
    }
  }
  if ((level == IPPROTO_IPV6 && name == IPV6_V6ONLY) || (level == IPPROTO_IP && name == IP_TOS))
    return read_int(value, length, &number);
  return fail(ENOPROTOOPT);
}

static int write_option(void *value, socklen_t *length, const void *data, socklen_t size) {
  if (value == NULL || length == NULL)
    return fail(EFAULT);
  memcpy(value, data, *length < size ? *length : size);
  *length = size;
  return 0;
}

int getsockopt(int fd, int level, int name, void *restrict value, socklen_t *restrict length) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  int number;
  if (level == SOL_SOCKET) {
    switch (name) {
    case SO_TYPE:
      number = state.type;
      break;
    case SO_DOMAIN:
      number = state.domain;
      break;
    case SO_PROTOCOL:
      number = state.protocol;
      break;
    case SO_ACCEPTCONN:
      number = state.listening;
      break;
    case SO_ERROR: {
      int32_t pending = 0;
      if (__browseros_sock_getsockopt(fd, KERNEL_SOL_SOCKET, KERNEL_SO_ERROR, &pending, sizeof pending) != 0)
        pending = 0;
      number = pending;
      break;
    }
    case SO_REUSEADDR:
      number = state.reuse_address;
      break;
    case SO_REUSEPORT:
      number = state.reuse_port;
      break;
    case SO_KEEPALIVE:
      number = state.keep_alive;
      break;
    case SO_BROADCAST:
      number = state.broadcast;
      break;
    case SO_SNDBUF:
      number = state.send_buffer;
      break;
    case SO_RCVBUF:
      number = state.receive_buffer;
      break;
    case SO_RCVTIMEO:
      return write_option(value, length, &state.receive_timeout, sizeof state.receive_timeout);
    case SO_SNDTIMEO:
      return write_option(value, length, &state.send_timeout, sizeof state.send_timeout);
    case SO_LINGER:
      return write_option(value, length, &state.linger, sizeof state.linger);
    default:
      return fail(ENOPROTOOPT);
    }
    return write_option(value, length, &number, sizeof number);
  }
  if (level == IPPROTO_TCP && state.domain != AF_UNIX && state.type == SOCK_STREAM && name == TCP_NODELAY) {
    number = state.no_delay;
    return write_option(value, length, &number, sizeof number);
  }
  return fail(ENOPROTOOPT);
}

// UNIX ドメインのつながった 1 組。カーネルの表に、ほかから見えない名前で一時の聞き手を置いて繋ぎ、受け付けたら消す
int socketpair(int domain, int type, int protocol, int fds[2]) {
  int flags = type & (SOCK_NONBLOCK | SOCK_CLOEXEC);
  type &= ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (domain != AF_UNIX)
    return fail(EOPNOTSUPP);
  if (type != SOCK_STREAM)
    return fail(EOPNOTSUPP);
  (void)protocol;
  static unsigned serial;
  struct sockaddr_un name;
  memset(&name, 0, sizeof name);
  name.sun_family = AF_UNIX;
  snprintf(name.sun_path, sizeof name.sun_path, "/.socketpair/%d/%u", getpid(),
           __atomic_fetch_add(&serial, 1, __ATOMIC_RELAXED));
  int listener = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listener < 0)
    return -1;
  int client = -1;
  int server = -1;
  if (bind(listener, (struct sockaddr *)&name, sizeof name) != 0 || listen(listener, 1) != 0)
    goto failed;
  client = socket(AF_UNIX, SOCK_STREAM | flags, 0);
  if (client < 0 || connect(client, (struct sockaddr *)&name, sizeof name) != 0)
    goto failed;
  server = accept4(listener, NULL, NULL, flags);
  if (server < 0)
    goto failed;
  close(listener);
  fds[0] = client;
  fds[1] = server;
  return 0;
failed: {
  int saved = errno;
  close(listener);
  if (client >= 0)
    close(client);
  return fail(saved);
}
}

int sockatmark(int fd) {
  struct socket_state state;
  if (lookup(fd, &state) != 0)
    return -1;
  // 帯域外のデータは運ばないので、印に来ていることは無い
  return 0;
}
