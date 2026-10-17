// clone(2) の子を走らせる `__clone`（musl の src/thread/<arch>/clone.s と同じ口）。
//
// musl の `__clone(fn, stack, flags, arg, ptid, tls, ctid)` は clone(2) を呼び、子では fn(arg) を走らせて、その戻り値で
// exit(2) する。wasm は子の実行の流れを作れないので、BrowserOS のカーネルは `CLONE_VM | CLONE_VFORK` の子を、呼んだ
// スレッドの上で同じインスタンスへ入り直して走らせる（→ @progate/browser-wasi の clone.ts）。入り直す先が、ここで
// export する `__browseros_clone_child` で、fn と arg はこのスレッドの変数で渡す。
//
// 子の段は、子が execve(2) を通すか exit(2) を呼んだところで終わり、カーネルは子の段の wasm のフレームを巻き戻して
// 親の clone から子の pid を返す。巻き戻したフレームのぶん `__stack_pointer` が下がったままになるので、呼ぶ前の値に戻す。
// 子は親のスタックの続きで走る（`stack` は使わない。CLONE_VM の子が書いたメモリはそのまま親に見える）。

#include <browseros/linux.h>
#include <stdarg.h>
#include <stdint.h>

#define SYS_clone 220
#define SYS_exit 93
#define CLONE_PARENT_SETTID 0x100000
#define CLONE_SETTLS 0x80000
#define CLONE_CHILD_CLEARTID 0x200000
#define CLONE_CHILD_SETTID 0x01000000

static _Thread_local int (*child_fn)(void *);
static _Thread_local void *child_arg;

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

/// clone の子の入口（カーネルが、clone を呼んだスレッドの上で呼ぶ）。fn(arg) の戻り値で子を終える
__attribute__((export_name("__browseros_clone_child"))) void __browseros_clone_child(void) {
  int status = child_fn(child_arg);
  __bos_syscall1(SYS_exit, status);
}

int __clone(int (*fn)(void *), void *stack, int flags, void *arg, ...) {
  // ptid・tls・ctid は、それを使う flags を渡したときだけ並んでいる
  void *ptid = 0, *tls = 0, *ctid = 0;
  if (flags & (CLONE_PARENT_SETTID | CLONE_SETTLS | CLONE_CHILD_SETTID | CLONE_CHILD_CLEARTID)) {
    va_list ap;
    va_start(ap, arg);
    ptid = va_arg(ap, void *);
    tls = va_arg(ap, void *);
    ctid = va_arg(ap, void *);
    va_end(ap);
  }
  // 子の段の中でもう 1 度 clone することがあるので、前の fn と arg を控えて戻す
  int (*outer_fn)(void *) = child_fn;
  void *outer_arg = child_arg;
  child_fn = fn;
  child_arg = arg;
  uintptr_t sp = stack_pointer();
  long r = __bos_syscall5(SYS_clone, flags, __bos_ptr(stack), __bos_ptr(ptid), __bos_ptr(tls), __bos_ptr(ctid));
  set_stack_pointer(sp);
  child_fn = outer_fn;
  child_arg = outer_arg;
  return (int)r;
}
