#include <stdlib.h>
#include <features.h>
#ifdef __wasilibc_browseros
#include <signal.h>
#endif

void abort(void) {
#ifdef __wasilibc_browseros
  // BrowserOS: musl と同じく SIGABRT を起こす。ハンドラが戻ったら既定の動作に戻してもう一度起こし、
  // 親の waitpid に WTERMSIG == SIGABRT で見えるように終わる
  raise(SIGABRT);
  struct sigaction dfl = {.sa_handler = SIG_DFL};
  sigaction(SIGABRT, &dfl, 0);
  sigset_t abrt;
  sigemptyset(&abrt);
  sigaddset(&abrt, SIGABRT);
  sigprocmask(SIG_UNBLOCK, &abrt, 0);
  raise(SIGABRT);
#endif
  // wasm doesn't support signals, so just trap to halt the program.
  __builtin_trap();
}
