#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lock.h"

char *__wasilibc_cwd = "/";
DECLARE_WEAK_LOCK(__wasilibc_cwd_lock);

// BrowserOS: プロセスの作業ディレクトリへ移ってから始める。
//
// WASI preview1 に chdir の syscall は無く、cwd を持つのはこの libc（__wasilibc_cwd）である。初期値の "/" のままだと、
// シェルがどこにいても fopen("input.txt") が /input.txt を開く。BrowserOS のカーネルは子の作業ディレクトリを
// 決めて PWD に入れて渡す（渡された PWD は捨てて cwd から作り直すので、食い違わない）。それをここで引き受ける。
//
// cwd を読み書きするもの（getcwd・chdir・相対パスを引くすべての関数）はこのファイルを引き込むので、cwd を使う
// プログラムでは必ず走る。利用者のコンストラクタ（優先度 101 以降）より前に置き、名前空間スコープの
// std::ifstream in("input.txt") も作業ディレクトリから開けるようにする。PWD が無いか移れないとき
// （wasmtime を直に叩いたときなど）は "/" のままで続ける
__attribute__((constructor(50))) static void adopt_pwd(void) {
  const char *pwd = getenv("PWD");
  if (pwd && pwd[0] == '/')
    chdir(pwd);
}

char *getcwd(char *buf, size_t size) {
  // Critical section contains no yield points, so we can use weak locks.
  WEAK_LOCK(__wasilibc_cwd_lock);
  if (!buf) {
    buf = strdup(__wasilibc_cwd);
    if (!buf) {
      errno = ENOMEM;
      WEAK_UNLOCK(__wasilibc_cwd_lock);
      return NULL;
    }
  } else {
    size_t len = strlen(__wasilibc_cwd);
    if (size < len + 1) {
      errno = ERANGE;
      WEAK_UNLOCK(__wasilibc_cwd_lock);
      return NULL;
    }
    strcpy(buf, __wasilibc_cwd);
  }
  WEAK_UNLOCK(__wasilibc_cwd_lock);
  return buf;
}
