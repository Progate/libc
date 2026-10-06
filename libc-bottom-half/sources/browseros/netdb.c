// 名前解決（getaddrinfo・getnameinfo・gethostbyname ほか）。
//
// BrowserOS のプロセスには DNS が無い。外への接続は、カーネルの gateway が名前のまま繋ぐ（宛先の Host・SNI・CORS は
// 名前で決まるので、ここで IP に解いてしまうと正しく繋がらない。→ browser-os の src/net/gateway.ts）。
// そこで名前は次の順に答える。
//
//   1. 数で書いた番地（`127.0.0.1`・`::1`）はそのまま
//   2. `localhost` と `*.localhost` はループバック（RFC 6761。musl と同じ）
//   3. /etc/hosts に載る名前は、そこに書かれた番地（Linux の files と同じ。カーネルも同じ名簿で繋ぐ）
//   4. それ以外の名前は、予約済みで経路の無い 240.0.0.0/4 から番地を 1 つ割り当て、その名前の代わりにする。
//      connect はその番地を名前に戻し、カーネルに名前のまま繋がせる（→ socket.c）。割り当てはプロセスの中で
//      名前ごとに決まり、getnameinfo と gethostbyaddr はその番地から名前を引き戻す
//
// サービスの名前（`http` など）は /etc/services で引く（musl の getservbyname_r）。BrowserOS の世界は /etc/services を
// 持たないことがあるので、そのときは IANA の割り当てのうち広く使うものをこの中の表で引く。

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <browseros/libc.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

// 名前に割り当てる番地の範囲（240.0.0.0/4。将来のために予約されていて、どこへも経路が無い）
#define NAMED_BASE 0xf0000000u
#define NAMED_MASK 0xf0000000u
#define MAX_NAMES 4096

static char *names[MAX_NAMES];
static size_t name_count;
static pthread_mutex_t names_lock = PTHREAD_MUTEX_INITIALIZER;

// 名前の代わりの番地（ホストのバイト順）。初めての名前には新しく割り当てる。割り当てられなければ 0
static uint32_t address_for_name(const char *name) {
  uint32_t address = 0;
  pthread_mutex_lock(&names_lock);
  for (size_t i = 0; i < name_count; i++) {
    if (strcasecmp(names[i], name) == 0) {
      address = NAMED_BASE + (uint32_t)i + 1;
      break;
    }
  }
  if (address == 0 && name_count < MAX_NAMES) {
    char *copy = strdup(name);
    if (copy != NULL) {
      names[name_count] = copy;
      address = NAMED_BASE + (uint32_t)name_count + 1;
      name_count++;
    }
  }
  pthread_mutex_unlock(&names_lock);
  return address;
}

// 番地が名前の代わりなら、その名前を host に写して 1。名前の代わりでなければ 0。範囲の中なのに割り当てていない
// 番地（作り話の番地）は -1
int __browseros_name_of_address(uint32_t address, char *host, size_t size) {
  if ((address & NAMED_MASK) != NAMED_BASE)
    return 0;
  int found = -1;
  pthread_mutex_lock(&names_lock);
  size_t index = address - NAMED_BASE - 1;
  if (address != NAMED_BASE && index < name_count) {
    if (size > 0) {
      strncpy(host, names[index], size - 1);
      host[size - 1] = '\0';
    }
    found = 1;
  }
  pthread_mutex_unlock(&names_lock);
  return found;
}

// /etc/services が無い世界で引く、よく使うサービスの割り当て（IANA）
static const struct {
  const char *name;
  unsigned short port;
} WELL_KNOWN_SERVICES[] = {
    {"ftp-data", 20}, {"ftp", 21},     {"ssh", 22},        {"telnet", 23},     {"smtp", 25},
    {"domain", 53},   {"http", 80},    {"www", 80},        {"pop3", 110},      {"nntp", 119},
    {"ntp", 123},     {"imap", 143},   {"imap2", 143},     {"ldap", 389},      {"https", 443},
    {"submissions", 465}, {"submission", 587}, {"ldaps", 636}, {"imaps", 993}, {"pop3s", 995},
    {"mysql", 3306},  {"postgresql", 5432}, {"redis", 6379}, {"http-alt", 8080},
};

// サービスの名前か数をポート（ホストのバイト順）にする。見つからなければ -1
static int port_of_service(const char *service, int socktype, int numeric_only) {
  if (service == NULL || *service == '\0')
    return 0;
  char *end;
  unsigned long value = strtoul(service, &end, 10);
  if (*end == '\0')
    return value > 65535 ? -1 : (int)value;
  if (numeric_only)
    return -1;
  struct servent entry;
  struct servent *result = NULL;
  char buffer[256];
  if (getservbyname_r(service, socktype == SOCK_DGRAM ? "udp" : "tcp", &entry, buffer, sizeof buffer, &result) ==
          0 &&
      result != NULL)
    return ntohs((uint16_t)result->s_port);
  FILE *services = fopen("/etc/services", "re");
  if (services != NULL) {
    fclose(services);
    return -1;
  }
  for (size_t i = 0; i < sizeof WELL_KNOWN_SERVICES / sizeof WELL_KNOWN_SERVICES[0]; i++)
    if (strcmp(WELL_KNOWN_SERVICES[i].name, service) == 0)
      return WELL_KNOWN_SERVICES[i].port;
  return -1;
}

// ポートからサービスの名前を引く（/etc/services か、無ければこの中の表）。見つからなければ NULL
static const char *service_of_port(int port, int datagram, char *buffer, size_t size) {
  FILE *services = fopen("/etc/services", "re");
  if (services == NULL) {
    for (size_t i = 0; i < sizeof WELL_KNOWN_SERVICES / sizeof WELL_KNOWN_SERVICES[0]; i++)
      if (WELL_KNOWN_SERVICES[i].port == port)
        return WELL_KNOWN_SERVICES[i].name;
    return NULL;
  }
  const char *found = NULL;
  char line[256];
  while (fgets(line, sizeof line, services) != NULL) {
    char *comment = strchr(line, '#');
    if (comment != NULL)
      *comment = '\0';
    char name[64];
    unsigned number;
    char protocol[8];
    if (sscanf(line, "%63s %u/%7s", name, &number, protocol) == 3 && (int)number == port &&
        strcmp(protocol, datagram ? "udp" : "tcp") == 0) {
      strncpy(buffer, name, size - 1);
      buffer[size - 1] = '\0';
      found = buffer;
      break;
    }
  }
  fclose(services);
  return found;
}

// /etc/hosts で名前を引き、family の番地を out に書く（4 か 16 バイト）。見つかれば 1
static int hosts_lookup(const char *name, int family, unsigned char *out) {
  FILE *hosts = fopen("/etc/hosts", "re");
  if (hosts == NULL)
    return 0;
  char line[512];
  int found = 0;
  while (!found && fgets(line, sizeof line, hosts) != NULL) {
    char *comment = strchr(line, '#');
    if (comment != NULL)
      *comment = '\0';
    char *save;
    char *address = strtok_r(line, " \t\r\n", &save);
    if (address == NULL)
      continue;
    unsigned char bytes[16];
    int kind = inet_pton(AF_INET, address, bytes) == 1 ? AF_INET : inet_pton(AF_INET6, address, bytes) == 1 ? AF_INET6 : 0;
    if (kind == 0 || (family != AF_UNSPEC && kind != family))
      continue;
    for (char *alias = strtok_r(NULL, " \t\r\n", &save); alias != NULL; alias = strtok_r(NULL, " \t\r\n", &save)) {
      if (strcasecmp(alias, name) == 0) {
        memcpy(out, bytes, kind == AF_INET ? 4 : 16);
        found = kind;
        break;
      }
    }
  }
  fclose(hosts);
  return found;
}

// /etc/hosts で番地から名前を引く。見つかれば 1
static int hosts_reverse(int family, const unsigned char *address, char *host, size_t size) {
  FILE *hosts = fopen("/etc/hosts", "re");
  if (hosts == NULL)
    return 0;
  char line[512];
  int found = 0;
  while (!found && fgets(line, sizeof line, hosts) != NULL) {
    char *comment = strchr(line, '#');
    if (comment != NULL)
      *comment = '\0';
    char *save;
    char *text = strtok_r(line, " \t\r\n", &save);
    unsigned char bytes[16];
    if (text == NULL || inet_pton(family, text, bytes) != 1 || memcmp(bytes, address, family == AF_INET ? 4 : 16) != 0)
      continue;
    char *name = strtok_r(NULL, " \t\r\n", &save);
    if (name != NULL && strlen(name) < size) {
      strcpy(host, name);
      found = 1;
    }
  }
  fclose(hosts);
  return found;
}

static int is_localhost(const char *name) {
  size_t length = strlen(name);
  if (length > 0 && name[length - 1] == '.')
    length--;
  if (length == 9 && strncasecmp(name, "localhost", 9) == 0)
    return 1;
  return length > 10 && strncasecmp(name + length - 10, ".localhost", 10) == 0;
}

// 名前か番地を 1 つの番地にする（上の 1〜4）。family は AF_INET か AF_INET6 で書き、それを返す。答えられなければ EAI_*
static int resolve(const char *node, int family, int flags, unsigned char *out) {
  if (family != AF_INET6 && inet_pton(AF_INET, node, out) == 1)
    return AF_INET;
  if (family != AF_INET && inet_pton(AF_INET6, node, out) == 1)
    return AF_INET6;
  if (flags & AI_NUMERICHOST)
    return EAI_NONAME;
  if (is_localhost(node)) {
    if (family == AF_INET6) {
      memset(out, 0, 16);
      out[15] = 1;
      return AF_INET6;
    }
    const unsigned char loopback[4] = {127, 0, 0, 1};
    memcpy(out, loopback, 4);
    return AF_INET;
  }
  int kind = hosts_lookup(node, family, out);
  if (kind != 0)
    return kind;
  // 名前の代わりの番地は IPv4 の範囲にしか無い。IPv6 だけを求められたら、IPv4 を写した番地（::ffff:240.x.x.x）で答える
  uint32_t address = address_for_name(node);
  if (address == 0)
    return EAI_MEMORY;
  uint32_t network = htonl(address);
  if (family == AF_INET6) {
    memset(out, 0, 10);
    out[10] = out[11] = 0xff;
    memcpy(out + 12, &network, 4);
    return AF_INET6;
  }
  memcpy(out, &network, 4);
  return AF_INET;
}

int getaddrinfo(const char *restrict node, const char *restrict service, const struct addrinfo *restrict hints,
                struct addrinfo **restrict result) {
  int family = hints ? hints->ai_family : AF_UNSPEC;
  int socktype = hints ? hints->ai_socktype : 0;
  int protocol = hints ? hints->ai_protocol : 0;
  int flags = hints ? hints->ai_flags : 0;
  if (result == NULL)
    return EAI_FAIL;
  if (node == NULL && service == NULL)
    return EAI_NONAME;
  if (family != AF_UNSPEC && family != AF_INET && family != AF_INET6)
    return EAI_FAMILY;
  if (socktype != 0 && socktype != SOCK_STREAM && socktype != SOCK_DGRAM)
    return EAI_SOCKTYPE;
  if (flags & ~(AI_PASSIVE | AI_CANONNAME | AI_NUMERICHOST | AI_V4MAPPED | AI_ALL | AI_ADDRCONFIG | AI_NUMERICSERV))
    return EAI_BADFLAGS;
  if ((flags & AI_CANONNAME) && node == NULL)
    return EAI_BADFLAGS;

  int port = port_of_service(service, socktype, flags & AI_NUMERICSERV);
  if (port < 0)
    return EAI_SERVICE;

  unsigned char address[16];
  int kind;
  if (node == NULL) {
    // 名前が無いのは、待つ側（AI_PASSIVE）ならどこでも、繋ぐ側ならループバック
    kind = family == AF_INET6 ? AF_INET6 : AF_INET;
    memset(address, 0, sizeof address);
    if (!(flags & AI_PASSIVE)) {
      if (kind == AF_INET) {
        address[0] = 127;
        address[3] = 1;
      } else {
        address[15] = 1;
      }
    }
  } else {
    kind = resolve(node, family, flags, address);
    if (kind < 0)
      return kind;
  }

  // 種類を指定されなければ、ストリームとデータグラムの両方を返す（Linux と同じ並び）
  int types[2] = {socktype ? socktype : SOCK_STREAM, SOCK_DGRAM};
  int type_count = socktype ? 1 : 2;
  struct addrinfo *head = NULL;
  struct addrinfo **tail = &head;
  for (int i = 0; i < type_count; i++) {
    size_t address_size = kind == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6);
    size_t canon_size = (i == 0 && (flags & AI_CANONNAME)) ? strlen(node) + 1 : 0;
    struct addrinfo *entry = calloc(1, sizeof *entry + address_size + canon_size);
    if (entry == NULL) {
      freeaddrinfo(head);
      return EAI_MEMORY;
    }
    entry->ai_family = kind;
    entry->ai_socktype = types[i];
    entry->ai_protocol = protocol ? protocol : types[i] == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
    entry->ai_addrlen = address_size;
    entry->ai_addr = (struct sockaddr *)(entry + 1);
    if (kind == AF_INET) {
      struct sockaddr_in *in = (struct sockaddr_in *)entry->ai_addr;
      in->sin_family = AF_INET;
      in->sin_port = htons((uint16_t)port);
      memcpy(&in->sin_addr, address, 4);
    } else {
      struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)entry->ai_addr;
      in6->sin6_family = AF_INET6;
      in6->sin6_port = htons((uint16_t)port);
      memcpy(&in6->sin6_addr, address, 16);
    }
    if (canon_size) {
      entry->ai_canonname = (char *)entry->ai_addr + address_size;
      memcpy(entry->ai_canonname, node, canon_size);
    }
    *tail = entry;
    tail = &entry->ai_next;
  }
  *result = head;
  return 0;
}

void freeaddrinfo(struct addrinfo *list) {
  while (list != NULL) {
    struct addrinfo *next = list->ai_next;
    free(list);
    list = next;
  }
}

int getnameinfo(const struct sockaddr *restrict address, socklen_t length, char *restrict host, socklen_t host_size,
                char *restrict service, socklen_t service_size, int flags) {
  if (address == NULL)
    return EAI_FAIL;
  const unsigned char *bytes;
  int family = address->sa_family;
  int port;
  if (family == AF_INET && length >= (socklen_t)sizeof(struct sockaddr_in)) {
    const struct sockaddr_in *in = (const struct sockaddr_in *)address;
    bytes = (const unsigned char *)&in->sin_addr;
    port = ntohs(in->sin_port);
  } else if (family == AF_INET6 && length >= (socklen_t)sizeof(struct sockaddr_in6)) {
    const struct sockaddr_in6 *in6 = (const struct sockaddr_in6 *)address;
    bytes = (const unsigned char *)&in6->sin6_addr;
    port = ntohs(in6->sin6_port);
  } else {
    return EAI_FAMILY;
  }

  if (host != NULL && host_size > 0) {
    char text[256] = "";
    int named = 0;
    if (!(flags & NI_NUMERICHOST)) {
      uint32_t v4;
      if (family == AF_INET || (bytes[10] == 0xff && bytes[11] == 0xff && !memcmp(bytes, "\0\0\0\0\0\0\0\0\0\0", 10))) {
        memcpy(&v4, family == AF_INET ? bytes : bytes + 12, 4);
        named = __browseros_name_of_address(ntohl(v4), text, sizeof text) == 1;
      }
      if (!named)
        named = hosts_reverse(family, bytes, text, sizeof text);
    }
    if (!named) {
      if (flags & NI_NAMEREQD)
        return EAI_NONAME;
      inet_ntop(family, bytes, text, sizeof text);
    }
    if (strlen(text) >= (size_t)host_size)
      return EAI_OVERFLOW;
    strcpy(host, text);
  }

  if (service != NULL && service_size > 0) {
    char buffer[64];
    const char *name = (flags & NI_NUMERICSERV) ? NULL : service_of_port(port, flags & NI_DGRAM, buffer, sizeof buffer);
    char text[16];
    if (name == NULL) {
      snprintf(text, sizeof text, "%d", port);
      name = text;
    }
    if (strlen(name) >= (size_t)service_size)
      return EAI_OVERFLOW;
    strcpy(service, name);
  }
  return 0;
}

// gethostbyname などの答えの置き場。答えは呼んだ側が次の呼び出しまでに写す約束（POSIX）
static struct {
  struct hostent entry;
} host_result;

static int fill_hostent(const char *name, int family, struct hostent *entry, char *buffer, size_t size,
                        struct hostent **result, int *error) {
  unsigned char address[16];
  int kind = resolve(name, family, 0, address);
  if (kind < 0) {
    *result = NULL;
    *error = HOST_NOT_FOUND;
    return kind == EAI_MEMORY ? ENOMEM : ENOENT;
  }
  size_t address_size = kind == AF_INET ? 4 : 16;
  size_t name_size = strlen(name) + 1;
  // 並び: 別名の表（NULL だけ）・番地の表（1 つと NULL）・番地・名前
  size_t needed = sizeof(char *) + 2 * sizeof(char *) + address_size + name_size;
  if (size < needed) {
    *result = NULL;
    *error = NO_RECOVERY;
    return ERANGE;
  }
  char **aliases = (char **)buffer;
  char **addresses = aliases + 1;
  unsigned char *bytes = (unsigned char *)(addresses + 2);
  char *copy = (char *)bytes + address_size;
  aliases[0] = NULL;
  memcpy(bytes, address, address_size);
  addresses[0] = (char *)bytes;
  addresses[1] = NULL;
  memcpy(copy, name, name_size);
  entry->h_name = copy;
  entry->h_aliases = aliases;
  entry->h_addrtype = kind;
  entry->h_length = (int)address_size;
  entry->h_addr_list = addresses;
  *result = entry;
  *error = 0;
  return 0;
}

int gethostbyname2_r(const char *name, int family, struct hostent *entry, char *buffer, size_t size,
                     struct hostent **result, int *error) {
  if (family != AF_INET && family != AF_INET6) {
    *result = NULL;
    *error = NO_RECOVERY;
    return EAFNOSUPPORT;
  }
  return fill_hostent(name, family, entry, buffer, size, result, error);
}

int gethostbyname_r(const char *name, struct hostent *entry, char *buffer, size_t size, struct hostent **result,
                    int *error) {
  return gethostbyname2_r(name, AF_INET, entry, buffer, size, result, error);
}

struct hostent *gethostbyname2(const char *name, int family) {
  static char buffer[sizeof(char *) * 3 + 16 + 256];
  struct hostent *result;
  int error;
  if (strlen(name) >= 256) {
    h_errno = HOST_NOT_FOUND;
    return NULL;
  }
  if (gethostbyname2_r(name, family, &host_result.entry, buffer, sizeof buffer, &result, &error) != 0) {
    h_errno = error;
    return NULL;
  }
  return result;
}

struct hostent *gethostbyname(const char *name) {
  return gethostbyname2(name, AF_INET);
}

int gethostbyaddr_r(const void *address, socklen_t length, int family, struct hostent *entry, char *buffer,
                    size_t size, struct hostent **result, int *error) {
  struct sockaddr_storage storage;
  memset(&storage, 0, sizeof storage);
  if (family == AF_INET && length == 4) {
    struct sockaddr_in *in = (struct sockaddr_in *)&storage;
    in->sin_family = AF_INET;
    memcpy(&in->sin_addr, address, 4);
  } else if (family == AF_INET6 && length == 16) {
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&storage;
    in6->sin6_family = AF_INET6;
    memcpy(&in6->sin6_addr, address, 16);
  } else {
    *result = NULL;
    *error = NO_RECOVERY;
    return EINVAL;
  }
  char name[256];
  if (getnameinfo((struct sockaddr *)&storage, sizeof storage, name, sizeof name, NULL, 0, NI_NAMEREQD) != 0) {
    *result = NULL;
    *error = HOST_NOT_FOUND;
    return ENOENT;
  }
  int status = fill_hostent(name, family, entry, buffer, size, result, error);
  // 名前から引き直した番地ではなく、尋ねられた番地を答えに置く
  if (status == 0)
    memcpy(entry->h_addr_list[0], address, length);
  return status;
}

struct hostent *gethostbyaddr(const void *address, socklen_t length, int family) {
  static char buffer[sizeof(char *) * 3 + 16 + 256];
  struct hostent *result;
  int error;
  if (gethostbyaddr_r(address, length, family, &host_result.entry, buffer, sizeof buffer, &result, &error) != 0) {
    h_errno = error;
    return NULL;
  }
  return result;
}
