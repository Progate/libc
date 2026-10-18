/*
 * dlopen・dlsym・dlclose・dl_iterate_phdr の本体。ホストの動的ローダー（browser_dyld）へ渡す。
 *
 * このファイルは共有の libc.so にだけ入る。静的な libc.a には src/ldso/dlopen.c・__dlsym.c の
 * 「常に失敗する」stub が入り、Linux で静的に組んだプログラムと同じく dlopen は失敗する。
 * モジュールの instantiate・シンボル解決・再配置はホスト側の仕事で、ここでするのは
 * パスの正規化と、ハンドル・エラー文字列の受け渡しだけである。
 *
 * ハンドルはホストが振る 32 ビットの整数で、0 は失敗を表す。dlsym が返す関数ポインタは
 * メインと共有する間接関数テーブルの添字で、ゲストではそれがそのまま関数ポインタになる。
 * エラーは __dl_seterr に流して dlerror（スレッドごとのバッファ）に載せる
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <limits.h>
#include <link.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "dynlink.h"

__attribute__((import_module("browser_dyld"), import_name("open")))
extern uint32_t __browser_dyld_open(const char *path, size_t path_len, int flags);
__attribute__((import_module("browser_dyld"), import_name("sym")))
extern uint32_t __browser_dyld_sym(uint32_t handle, const char *name, size_t name_len);
__attribute__((import_module("browser_dyld"), import_name("close")))
extern int __browser_dyld_close(uint32_t handle);
__attribute__((import_module("browser_dyld"), import_name("error")))
extern size_t __browser_dyld_error(char *buffer, size_t capacity);

/* `module_info` が書く 1 件（ホストの動的ローダーが持つモジュールの情報） */
struct browser_dyld_module_info {
    uint32_t memory_base;
    uint32_t tls_modid;
    uint32_t tls_data;
    uint32_t reserved;
    uint64_t adds;
    uint64_t subs;
};
__attribute__((import_module("browser_dyld"), import_name("module_info")))
extern int32_t __browser_dyld_module_info(uint32_t index, struct browser_dyld_module_info *info, char *name,
                                          size_t capacity);
__attribute__((import_module("browser_dyld"), import_name("addr_info")))
extern int32_t __browser_dyld_addr_info(const void *addr, Dl_info *info);

/* ホストに溜まった直前の失敗を dlerror の文字列に写す。写したらホスト側は空になる */
static void
take_error(void)
{
    char buffer[1024];
    size_t n = __browser_dyld_error(buffer, sizeof(buffer) - 1);
    if (n == 0) return;
    if (n >= sizeof(buffer)) n = sizeof(buffer) - 1;
    buffer[n] = '\0';
    __dl_seterr("%s", buffer);
}

void *
dlopen(const char *path, int flags)
{
    /*
     * 相対パスはゲストの cwd を基準に読む必要があるが、cwd を持っているのは libc で
     * ホストからは見えない。そのため `/` を含む相対パスだけここで絶対パスへ直す。
     * `/` を含まない名前はライブラリの探索に回すので、そのまま渡す（ld.so と同じ）
     */
    char absolute[PATH_MAX];
    if (path != NULL && path[0] != '/' && strchr(path, '/') != NULL) {
        if (getcwd(absolute, sizeof(absolute)) == NULL) {
            __dl_seterr("dlopen: cannot resolve a relative path without the current directory");
            return NULL;
        }
        size_t cwd_len = strlen(absolute);
        size_t path_len = strlen(path);
        if (cwd_len + 1 + path_len + 1 > sizeof(absolute)) {
            __dl_seterr("dlopen: path is too long");
            return NULL;
        }
        absolute[cwd_len] = '/';
        memcpy(absolute + cwd_len + 1, path, path_len + 1);
        path = absolute;
    }
    uint32_t handle = __browser_dyld_open(path, path == NULL ? 0 : strlen(path), flags);
    if (handle == 0) {
        take_error();
        return NULL;
    }
    return (void *)(uintptr_t)handle;
}

/* dlsym（src/ldso/dlsym.c）から呼ばれる本体。ハンドルはそのままホストへ渡し、
 * RTLD_DEFAULT・RTLD_NEXT の解釈はホストの仕事にする（ra は使わない） */
hidden void *
__dlsym(void *restrict p, const char *restrict s, void *restrict ra)
{
    uint32_t value = __browser_dyld_sym((uint32_t)(uintptr_t)p, s, strlen(s));
    if (value == 0) {
        take_error();
        return NULL;
    }
    return (void *)(uintptr_t)value;
}

int
dlclose(void *p)
{
    int result = __browser_dyld_close((uint32_t)(uintptr_t)p);
    if (result != 0) take_error();
    return result;
}

/* dli_fname・dli_sname が指す文字列はローダーがゲストのメモリに持つ。dladdr は成否だけを返し、
 * 失敗の理由は dlerror に載せない（POSIX の dladdr と同じ） */
int
dladdr(const void *addr, Dl_info *info)
{
    return __browser_dyld_addr_info(addr, info) == 0;
}

/*
 * 読み込まれているモジュールを、プログラム自身から読み込んだ順に callback へ渡す（dl_iterate_phdr(3)）。
 * 静的な libc.a では src/ldso/wasm32/dl_iterate_phdr.c の弱い定義がプログラム自身だけを答える。
 * wasm にはプログラムヘッダーが無いので `dlpi_phdr` は NULL・`dlpi_phnum` は 0 で、
 * `dlpi_addr` はモジュールのデータを置いた番地（`__memory_base`）である。
 * プログラム自身の名前は glibc と同じく空の文字列で、共有ライブラリの名前は読み込んだファイルのパスである
 */
int
dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data)
{
    struct browser_dyld_module_info module;
    struct dl_phdr_info info = {
        .dlpi_addr = 0,
        .dlpi_name = "",
        .dlpi_phdr = NULL,
        .dlpi_phnum = 0,
    };
    /* 読み込んだ数と下ろした数は、サイドモジュールが 1 つも無く -1 が返るときもホストが書く */
    char name[PATH_MAX];
    int32_t length = __browser_dyld_module_info(0, &module, name, sizeof(name));
    info.dlpi_adds = module.adds;
    info.dlpi_subs = module.subs;
#ifdef _REENTRANT
    if (__builtin_wasm_tls_size() > 0) {
        info.dlpi_tls_modid = 1;
        info.dlpi_tls_data = __builtin_wasm_tls_base();
    }
#endif
    int result = callback(&info, sizeof(info), data);
    for (uint32_t index = 0; result == 0; index++) {
        length = __browser_dyld_module_info(index, &module, name, sizeof(name));
        if (length < 0) break;
        /* dlopen に渡すパスは PATH_MAX に縛られない。収まらなければホストは長さだけを返すので、取り直して引き直す */
        char *long_name = NULL;
        if ((size_t)length >= sizeof(name)) {
            long_name = malloc((size_t)length + 1);
            if (long_name == NULL) break;
            __browser_dyld_module_info(index, &module, long_name, (size_t)length + 1);
        }
        info.dlpi_addr = module.memory_base;
        info.dlpi_name = long_name != NULL ? long_name : name;
        info.dlpi_adds = module.adds;
        info.dlpi_subs = module.subs;
        info.dlpi_tls_modid = module.tls_modid;
        info.dlpi_tls_data = (void *)(uintptr_t)module.tls_data;
        result = callback(&info, sizeof(info), data);
        free(long_name);
    }
    return result;
}
