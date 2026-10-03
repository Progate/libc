# BrowserOS の libc（p1）はプロセスの CPU 時間を libc 本体に持つので、ここは空にする（→ clocks-empty.c）
if (WASI STREQUAL "p1")
  add_internal_library_pair(wasi-emulated-process-clocks clocks-empty.c)
else()
  add_internal_library_pair(wasi-emulated-process-clocks
      clock.c
      getrusage.c
      times.c)
endif()

sysroot_lib(wasi-emulated-process-clocks-static libwasi-emulated-process-clocks.a)
sysroot_lib(wasi-emulated-process-clocks libwasi-emulated-process-clocks.so)
