#ifndef __wasilibc___header_sys_socket_h
#define __wasilibc___header_sys_socket_h

#include <__struct_msghdr.h>
#include <__struct_sockaddr.h>
#include <__struct_sockaddr_storage.h>

#include <wasi/version.h>
#include <features.h>

#define SHUT_RD 1
#define SHUT_WR 2
#define SHUT_RDWR (SHUT_RD | SHUT_WR)

#if defined(__wasip1__) && defined(__wasilibc_browseros)
/*
 * BrowserOS の libc はカーネルのソケット表で BSD ソケットを持つ（→ libc-bottom-half/sources/browseros/socket.c）。
 * フラグとオプションの値は Linux と同じにする（カーネルへ渡すときに libc が WASI の値へ直す）
 */
#define MSG_OOB 0x0001
#define MSG_PEEK 0x0002
#define MSG_DONTROUTE 0x0004
#define MSG_CTRUNC 0x0008
#define MSG_TRUNC 0x0020
#define MSG_DONTWAIT 0x0040
#define MSG_EOR 0x0080
#define MSG_WAITALL 0x0100
#define MSG_NOSIGNAL 0x4000
#define MSG_MORE 0x8000
#define MSG_CMSG_CLOEXEC 0x40000000

#define SOL_IP 0
#define SOL_TCP 6
#define SOL_UDP 17
#define SOL_IPV6 41

#define SOMAXCONN 4096

#define SO_DEBUG 1
#define SO_REUSEADDR 2
#define SO_ERROR 4
#define SO_DONTROUTE 5
#define SO_BROADCAST 6
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_OOBINLINE 10
#define SO_LINGER 13
#define SO_REUSEPORT 15
#define SO_RCVLOWAT 18
#define SO_SNDLOWAT 19
#define SO_ACCEPTCONN 30
#define SO_PROTOCOL 38
#define SO_DOMAIN 39
#define SO_RCVTIMEO 66
#define SO_SNDTIMEO 67

#define PF_LOCAL 3
#define PF_UNIX PF_LOCAL
#define PF_FILE PF_LOCAL
#define AF_LOCAL PF_LOCAL
#define AF_FILE PF_LOCAL

/* 補助データ（musl の 32 ビットの並び）。カーネルは補助データを運ばないので、sendmsg は付いていれば断る */
struct cmsghdr {
  socklen_t cmsg_len;
  int cmsg_level;
  int cmsg_type;
};

#define SCM_RIGHTS 0x01
#define SCM_CREDENTIALS 0x02

#define __CMSG_LEN(cmsg) (((cmsg)->cmsg_len + sizeof(long) - 1) & ~(long)(sizeof(long) - 1))
#define __CMSG_NEXT(cmsg) ((unsigned char *)(cmsg) + __CMSG_LEN(cmsg))
#define __MHDR_END(mhdr) ((unsigned char *)(mhdr)->msg_control + (mhdr)->msg_controllen)

#define CMSG_DATA(cmsg) ((unsigned char *)(((struct cmsghdr *)(cmsg)) + 1))
#define CMSG_NXTHDR(mhdr, cmsg)                                                                  \
  ((cmsg)->cmsg_len < sizeof(struct cmsghdr) ||                                                  \
           __CMSG_LEN(cmsg) + sizeof(struct cmsghdr) >= (size_t)(__MHDR_END(mhdr) - (unsigned char *)(cmsg)) \
       ? 0                                                                                       \
       : (struct cmsghdr *)__CMSG_NEXT(cmsg))
#define CMSG_FIRSTHDR(mhdr)                                                                      \
  ((size_t)(mhdr)->msg_controllen >= sizeof(struct cmsghdr) ? (struct cmsghdr *)(mhdr)->msg_control \
                                                            : (struct cmsghdr *)0)
#define CMSG_ALIGN(len) (((len) + sizeof(size_t) - 1) & (size_t) ~(sizeof(size_t) - 1))
#define CMSG_SPACE(len) (CMSG_ALIGN(len) + CMSG_ALIGN(sizeof(struct cmsghdr)))
#define CMSG_LEN(len) (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))
#elif defined(__wasip1__)
#include <wasi/api.h>
#define MSG_PEEK __WASI_RIFLAGS_RECV_PEEK
#define MSG_WAITALL __WASI_RIFLAGS_RECV_WAITALL
#define MSG_TRUNC __WASI_ROFLAGS_RECV_DATA_TRUNCATED
#elif defined(__wasip2__) || defined(__wasip3__)
#define MSG_DONTWAIT 0x0040
#define MSG_NOSIGNAL 0x4000
#define MSG_PEEK 0x0002
#define MSG_WAITALL 0x0100
#define MSG_TRUNC 0x0020

#define SOL_IP 0
#define SOL_TCP 6
#define SOL_UDP 17
#define SOL_IPV6 41

#define SOMAXCONN 128

#define SO_REUSEADDR 2
#define SO_ERROR 4
#define SO_SNDBUF 7
#define SO_RCVBUF 8
#define SO_KEEPALIVE 9
#define SO_ACCEPTCONN 30
#define SO_PROTOCOL 38
#define SO_DOMAIN 39

#if __LONG_MAX == 0x7fffffff
#define SO_RCVTIMEO 66
#define SO_SNDTIMEO 67
#else
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21
#endif

#else
#error "Unknown WASI version"
#endif

#define SOCK_DGRAM 5
#define SOCK_STREAM 6

#define SOCK_NONBLOCK (0x00004000)
#define SOCK_CLOEXEC (0x00002000)

#define SOL_SOCKET 0x7fffffff

#define SO_TYPE 3

#define PF_UNSPEC 0
#define PF_INET 1
#define PF_INET6 2

#define AF_UNSPEC PF_UNSPEC
#define AF_INET PF_INET
#define AF_INET6 PF_INET6
#define AF_UNIX 3

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif
