# fork(2)。積み重なりを写すために asyncify（wasm-opt --asyncify）で組んだプログラムだけが使えるので、
# libc 本体には入れず、使う側が選ぶ（-D_BROWSEROS_FORK -lbrowseros-fork。-D_WASI_EMULATED_GETPID -lwasi-emulated-getpid と同じ形）。
# libc 本体に入れると configure が fork を見つけ、fork + exec の道を選んだプログラムが動かなくなる（exec は無い）
if (NOT WASI STREQUAL "p1")
  return()
endif()
add_internal_library_pair(browseros-fork fork.c)
target_include_directories(browseros-fork PRIVATE ../headers/private)
target_include_directories(browseros-fork-static PRIVATE ../headers/private)
sysroot_lib(browseros-fork-static libbrowseros-fork.a)
sysroot_lib(browseros-fork libbrowseros-fork.so)
