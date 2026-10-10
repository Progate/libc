// fork(2)。
//
// 呼んだプロセスを写して子を作る。親には子の pid、子には 0 が返り、両方がここから続く。写すのはカーネル
// （browser_os_process.fork。→ browser-os の src/wasi/fork.ts）で、線形メモリ・fd 表・シグナルの扱いを写す。
//
// 関数のフレームとローカル変数は wasm の外から読めないので、プログラムは asyncify（`wasm-opt --asyncify
// --pass-arg=asyncify-imports@browser_os_process.fork`）で組んでおく。カーネルは asyncify で積み重なりを
// `asyncify_data` へ書き出して `_start` まで巻き戻し、線形メモリを写してから、親と子の両方をここへ巻き直す。
// asyncify で組んでいなければ ENOSYS が返る。
//
// `__stack_pointer`（C のスタックの位置）は wasm のグローバルで、カーネルからは写せない。巻き直しでは関数の頭
// （スタックを確保するところ）が飛ばされるので、子では初めの値のままになる。そこで呼ぶ前の値をローカル変数に
// 控え（ローカル変数は asyncify が写す）、子で戻ったところで書き戻す。

#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>

/// asyncify が積み重なりを書き出す場所。深く入れ子になった呼び出しから fork しても足りる大きさにする
#define ASYNCIFY_DATA_BYTES (64 * 1024)

/// asyncify の書き出し場所の頭（Binaryen の決まり: いまの位置と終わり）。その後ろに積み重なりが書かれる
struct asyncify_data {
  void *current;
  void *end;
  unsigned char stack[ASYNCIFY_DATA_BYTES];
};

static struct asyncify_data data;

static inline uintptr_t stack_pointer(void) {
  uintptr_t sp;
  __asm__ volatile(".globaltype __stack_pointer, i32\n"
                   "global.get __stack_pointer\n"
                   "local.set %0\n"
                   : "=r"(sp));
  return sp;
}

static inline void set_stack_pointer(uintptr_t sp) {
  __asm__ volatile(".globaltype __stack_pointer, i32\n"
                   "local.get %0\n"
                   "global.set __stack_pointer\n"
                   :
                   : "r"(sp));
}

pid_t fork(void) {
  __browseros_run_atfork(0);
  uintptr_t sp = stack_pointer();
  data.current = data.stack;
  data.end = data.stack + sizeof(data.stack);
  int32_t pid = 0;
  int32_t error = __browseros_fork(&data, &pid);
  if (error != 0) {
    __browseros_run_atfork(1);
    errno = error;
    return -1;
  }
  if (pid == 0) {
    set_stack_pointer(sp);
    __browseros_run_atfork(2);
    return 0;
  }
  __browseros_run_atfork(1);
  return pid;
}
