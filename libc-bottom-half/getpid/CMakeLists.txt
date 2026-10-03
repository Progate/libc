# BrowserOS の libc（p1）は getpid を libc 本体に持つので、ここは空にする（→ getpid-empty.c）
if (WASI STREQUAL "p1")
  add_internal_library_pair(wasi-emulated-getpid getpid-empty.c)
else()
  add_internal_library_pair(wasi-emulated-getpid getpid.c)
endif()
sysroot_lib(wasi-emulated-getpid-static libwasi-emulated-getpid.a)
sysroot_lib(wasi-emulated-getpid libwasi-emulated-getpid.so)
