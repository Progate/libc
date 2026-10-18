# BrowserOS の libc: musl（libc-top-half/musl）を wasm32 の arch（arch/wasm32・src/*/wasm32・crt/wasm32）で組む。
#
# ソースの選び方は musl の Makefile と同じ: src/*/*.c と src/malloc/mallocng/*.c を全部組み、
# src/<dir>/wasm32/ に同じ名前のものがあればそちらに置き換える。カーネルを呼ぶのは arch の syscall の入口
# （src/internal/wasm32/syscall.c）だけで、カーネルは @progate/browser-os の Linux の syscall の入口。
#
#   cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=<llvm>/bin/clang -DTARGET_TRIPLE=wasm32-linux-musl
#   ninja -C build && ninja -C build check-symbols
#
# 新しい道（wasm32-linux-musl(-threads)）の出口は build/sysroot/usr/{include,lib/<版>}。
# 旧い道（wasm32-wasip1(-threads)）はこれまでどおり build/sysroot/{include,lib,share}/<target>。

cmake_minimum_required(VERSION 3.20)

# s（索引）込みで作る。ホストの ranlib（macOS の BSD ranlib）は wasm のアーカイブを
# SYMDEF だけのものに書き換えて壊すので、ranlib の段は何もしないコマンドに差し替える
# （CMAKE_RANLIB は project() がコンパイラ情報で上書きするので、後の方で差し替える）
set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> --format=gnu qcs <TARGET> <OBJECTS>")
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_SYSTEM_NAME Generic)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# TARGET_TRIPLE はコンパイルのターゲットではなく版（バリアント）の名: wasm32-linux-musl(-threads) が
# 共有の libc.so の道（sysroot は multiarch の形）、wasm32-wasip1(-threads) はこれまでの道
set(TARGET_TRIPLE "wasm32-wasip1" CACHE STRING "wasm32-linux-musl(-threads) か wasm32-wasip1(-threads)")
set(BUILTINS_LIB "" CACHE FILEPATH "libc.so のリンクに使う compiler-rt（libclang_rt.builtins.a）")
if(TARGET_TRIPLE MATCHES "^wasm32-linux-musl(-threads)?$")
  set(LINUX_MUSL ON)
  set(COMPILE_TARGET wasm32-unknown-linux-musl)
elseif(TARGET_TRIPLE MATCHES "^wasm32-wasip1(-threads)?$")
  set(LINUX_MUSL OFF)
  set(COMPILE_TARGET ${TARGET_TRIPLE})
else()
  message(FATAL_ERROR "知らないターゲット: ${TARGET_TRIPLE}")
endif()
set(CMAKE_C_COMPILER_TARGET ${COMPILE_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${COMPILE_TARGET})

project(browseros-libc LANGUAGES C ASM)

# project() が検出したホストの ranlib を差し替える（→ ファイル頭のコメント）
set(CMAKE_RANLIB /usr/bin/true)

if(TARGET_TRIPLE MATCHES "-threads$")
  set(THREADS ON)
else()
  set(THREADS OFF)
endif()

set(MUSL ${CMAKE_SOURCE_DIR}/libc-top-half/musl)
set(GEN ${CMAKE_BINARY_DIR}/gen)
set(SYSROOT ${CMAKE_BINARY_DIR}/sysroot)
if(LINUX_MUSL)
  # Linux の sysroot の形: ヘッダーは 2 版で 1 つ、ライブラリは版ごとの lib に分ける（→ spec.md の 2）
  set(SYSROOT_INC ${SYSROOT}/usr/include)
  set(SYSROOT_LIB ${SYSROOT}/usr/lib/${TARGET_TRIPLE})
else()
  set(SYSROOT_INC ${SYSROOT}/include/${TARGET_TRIPLE})
  set(SYSROOT_LIB ${SYSROOT}/lib/${TARGET_TRIPLE})
endif()
set(SYSROOT_SHARE ${SYSROOT}/share/${TARGET_TRIPLE})

find_program(LLVM_AR NAMES llvm-ar HINTS ${CMAKE_C_COMPILER}/.. REQUIRED)
find_program(LLVM_NM NAMES llvm-nm HINTS ${CMAKE_C_COMPILER}/.. REQUIRED)
# libc.so の fork のための asyncify（abi/asyncify-onlylist.txt）。binaryen の版は packages 側で決め、
# ここでは PATH か -DWASM_OPT= で見つける
find_program(WASM_OPT NAMES wasm-opt)
set(CMAKE_AR ${LLVM_AR})
set(CMAKE_NM ${LLVM_NM})

# =============================================================================
# 生成するヘッダー（musl の Makefile の obj/include/bits と obj/src/internal/version.h）

file(MAKE_DIRECTORY ${GEN}/include/bits ${GEN}/src/internal)
add_custom_command(
  OUTPUT ${GEN}/include/bits/alltypes.h
  COMMAND sed -f ${MUSL}/tools/mkalltypes.sed ${MUSL}/arch/wasm32/bits/alltypes.h.in ${MUSL}/include/alltypes.h.in > ${GEN}/include/bits/alltypes.h
  DEPENDS ${MUSL}/arch/wasm32/bits/alltypes.h.in ${MUSL}/include/alltypes.h.in ${MUSL}/tools/mkalltypes.sed)
add_custom_command(
  OUTPUT ${GEN}/include/bits/syscall.h
  COMMAND cp ${MUSL}/arch/wasm32/bits/syscall.h.in ${GEN}/include/bits/syscall.h
  COMMAND sed -n -e s/__NR_/SYS_/p < ${MUSL}/arch/wasm32/bits/syscall.h.in >> ${GEN}/include/bits/syscall.h
  DEPENDS ${MUSL}/arch/wasm32/bits/syscall.h.in)
file(READ ${MUSL}/VERSION musl_version)
string(STRIP "${musl_version}" musl_version)
file(WRITE ${GEN}/src/internal/version.h "#define VERSION \"${musl_version}\"\n")
add_custom_target(generated-headers DEPENDS ${GEN}/include/bits/alltypes.h ${GEN}/include/bits/syscall.h)

# =============================================================================
# 組み立ての引数

set(common_flags
  --target=${COMPILE_TARGET}
  -std=c99 -ffreestanding -nostdinc
  -frounding-math -fno-strict-aliasing -fno-trapping-math
  -ffunction-sections -fdata-sections
  # 内部の setjmp / longjmp（pthread_cleanup など）を wasm の例外へ落とす。落とさないと
  # `U setjmp` の生シンボルが残り、実体は -lsetjmp の __wasm_* にしか無いのでリンク不能になる
  -mllvm -wasm-enable-sjlj
  -fno-unwind-tables -fno-asynchronous-unwind-tables
  -D_XOPEN_SOURCE=700
  -O2
  -Wno-parentheses -Wno-shift-op-parentheses -Wno-bitwise-op-parentheses
  -Wno-logical-op-parentheses -Wno-string-plus-int -Wno-dangling-else
  -Wno-unknown-pragmas -Wno-unused-command-line-argument -Wno-ignored-attributes)
if(THREADS)
  list(APPEND common_flags -pthread -matomics -mbulk-memory -mthread-model posix)
  if(NOT LINUX_MUSL)
    # 旧い道は TLS の命令を local-exec に固定していた。新しい道では TLS の解決はターゲットの既定
    # （hidden は local-exec、外に見せるものは GOT 経由）に任せる
    list(APPEND common_flags -ftls-model=local-exec)
  endif()
else()
  list(APPEND common_flags -mthread-model single)
endif()

set(musl_includes
  ${MUSL}/arch/wasm32 ${MUSL}/arch/generic ${GEN}/src/internal ${MUSL}/src/include ${MUSL}/src/internal
  ${GEN}/include ${MUSL}/include)

# =============================================================================
# ソース（musl の Makefile と同じ選び方）

file(GLOB base_sources ${MUSL}/src/*/*.c ${MUSL}/src/malloc/mallocng/*.c)
file(GLOB arch_sources ${MUSL}/src/*/wasm32/*.c ${MUSL}/src/*/wasm32/*.s ${MUSL}/src/*/wasm32/*.S)
foreach(arch_source IN LISTS arch_sources)
  get_filename_component(dir ${arch_source} DIRECTORY)
  get_filename_component(dir ${dir} DIRECTORY)
  get_filename_component(stem ${arch_source} NAME_WE)
  list(FILTER base_sources EXCLUDE REGEX "^${dir}/${stem}\\.c$")
endforeach()
set(sources ${base_sources} ${arch_sources})

# fork と _Fork は -lbrowseros-fork に分ける（→ src/process/wasm32/fork_data.c）。
# setjmp と longjmp は wasm の例外で作る -lsetjmp（-mllvm -wasm-enable-sjlj で組んだプログラムが使う）。
# どちらも新しい道（libc.so）では libc の中に戻す（→ spec.md の 4・9）
set(fork_sources
  ${MUSL}/src/process/fork.c ${MUSL}/src/process/_Fork.c ${MUSL}/src/process/vfork.c
  ${MUSL}/src/process/wasm32/fork_data.c)
file(GLOB setjmp_sources ${MUSL}/src/setjmp/wasm32/*)
if(LINUX_MUSL)
  # 新しい道では使わない入り口。init_modules.c は共有の libc.so にだけ入れる（静的な libc.a には入らない）
  list(REMOVE_ITEM sources
    ${MUSL}/src/env/wasm32/init_libc_export.c
    ${MUSL}/src/env/wasm32/init_modules.c
    ${MUSL}/src/env/wasm32/static_tls_dyld.c
    ${MUSL}/src/ldso/wasm32/dlfcn_dyld.c
    ${MUSL}/src/signal/wasm32/__browseros_signal.c)
else()
  list(REMOVE_ITEM sources ${fork_sources} ${setjmp_sources}
    ${MUSL}/src/env/wasm32/init_modules.c
    ${MUSL}/src/env/wasm32/static_tls_dyld.c
    ${MUSL}/src/ldso/wasm32/dlfcn_dyld.c)
endif()
# 名前解決とインターフェース表は BrowserOS の形に置き換える（→ src/network/wasm32/netdb.c・interfaces.c）。
# musl のものは DNS を自分で喋る・netlink を開く前提で、この OS にはそのどちらも無い
set(browseros_replaced_sources
  ${MUSL}/src/network/getaddrinfo.c ${MUSL}/src/network/freeaddrinfo.c ${MUSL}/src/network/getnameinfo.c
  ${MUSL}/src/network/gethostbyname.c ${MUSL}/src/network/gethostbyname2.c ${MUSL}/src/network/gethostbyname_r.c
  ${MUSL}/src/network/gethostbyname2_r.c ${MUSL}/src/network/gethostbyaddr.c ${MUSL}/src/network/gethostbyaddr_r.c
  ${MUSL}/src/network/getifaddrs.c ${MUSL}/src/network/if_nameindex.c ${MUSL}/src/network/if_freenameindex.c
  ${MUSL}/src/network/if_nametoindex.c ${MUSL}/src/network/if_indextoname.c)
list(REMOVE_ITEM sources ${browseros_replaced_sources})
# スレッドの入口はスレッド版だけ
if(NOT THREADS)
  list(REMOVE_ITEM sources ${MUSL}/src/thread/wasm32/wasi_thread_start.s)
endif()
list(SORT sources)

set(softfloat_dir ${CMAKE_SOURCE_DIR}/softfloat)
set(softfloat_names
  bf16_to_f32.c
  f128_add.c f128_div.c f128_mul.c f128_sqrt.c f128_sub.c
  f128_to_f16.c f128_to_f32.c f128_to_f64.c
  f16_add.c f16_div.c f16_mul.c f16_sqrt.c f16_sub.c
  f32_add.c f32_div.c f32_mul.c f32_sqrt.c f32_sub.c
  f32_to_bf16.c f32_to_f16.c
  f64_add.c f64_div.c f64_mul.c f64_sqrt.c f64_sub.c
  f64_to_f16.c f64_to_f32.c
  s_addMagsF128.c s_addMagsF16.c s_addMagsF32.c s_addMagsF64.c
  s_approxRecipSqrt32_1.c s_approxRecipSqrt_1Ks.c
  s_normRoundPackToF128.c s_normRoundPackToF32.c s_normRoundPackToF64.c
  s_normSubnormalF128Sig.c s_normSubnormalF16Sig.c s_normSubnormalF32Sig.c
  s_normSubnormalF64Sig.c
  s_roundPackToBF16.c s_roundPackToF128.c s_roundPackToF16.c
  s_roundPackToF32.c s_roundPackToF64.c
  s_shiftRightJam128.c s_shiftRightJam128Extra.c
  s_subMagsF128.c s_subMagsF16.c s_subMagsF32.c s_subMagsF64.c
  softfloat_state.c
  8086-SSE/s_bf16UIToCommonNaN.c 8086-SSE/s_commonNaNToBF16UI.c
  8086-SSE/s_commonNaNToF16UI.c 8086-SSE/s_commonNaNToF32UI.c
  8086-SSE/s_commonNaNToF64UI.c 8086-SSE/s_f128UIToCommonNaN.c
  8086-SSE/s_f32UIToCommonNaN.c 8086-SSE/s_f64UIToCommonNaN.c
  8086-SSE/s_propagateNaNF128UI.c 8086-SSE/s_propagateNaNF16UI.c
  8086-SSE/s_propagateNaNF32UI.c 8086-SSE/s_propagateNaNF64UI.c
  8086-SSE/softfloat_raiseFlags.c)
list(TRANSFORM softfloat_names PREPEND ${softfloat_dir}/berkeley-softfloat-3/source/ OUTPUT_VARIABLE softfloat_sources)
list(APPEND softfloat_sources ${softfloat_dir}/fenv-raise.c)

set(fts_sources ${CMAKE_SOURCE_DIR}/fts/musl-fts/fts.c)

# =============================================================================
# オブジェクト

function(musl_objects name pic)
  add_library(${name} OBJECT EXCLUDE_FROM_ALL ${ARGN})
  add_dependencies(${name} generated-headers)
  target_compile_options(${name} PRIVATE ${common_flags})
  target_include_directories(${name} PRIVATE ${musl_includes})
  if(pic)
    target_compile_options(${name} PRIVATE -fPIC)
  endif()
endfunction()

function(softfloat_objects name pic)
  add_library(${name} OBJECT EXCLUDE_FROM_ALL ${softfloat_sources})
  add_dependencies(${name} generated-headers)
  target_compile_options(${name} PRIVATE ${common_flags}
    -include ${softfloat_dir}/names.h
    -Wno-unused-parameter -Wno-unused-variable -Wno-sign-compare -Wno-implicit-fallthrough -Wno-unused-label)
  target_compile_definitions(${name} PRIVATE
    SOFTFLOAT_FAST_INT64 INLINE_LEVEL=5 SOFTFLOAT_FAST_DIV32TO16 SOFTFLOAT_FAST_DIV64TO32)
  target_include_directories(${name} PRIVATE
    ${softfloat_dir} ${softfloat_dir}/berkeley-softfloat-3/source/include ${softfloat_dir}/berkeley-softfloat-3/source/8086-SSE
    ${GEN}/include ${MUSL}/arch/wasm32 ${MUSL}/arch/generic ${MUSL}/include)
  if(pic)
    target_compile_options(${name} PRIVATE -fPIC)
  endif()
endfunction()

function(fts_objects name pic)
  add_library(${name} OBJECT EXCLUDE_FROM_ALL ${fts_sources})
  add_dependencies(${name} generated-headers)
  target_compile_options(${name} PRIVATE ${common_flags} -std=gnu99 -Wno-unused-parameter -Wno-unused-variable)
  target_include_directories(${name} PRIVATE
    ${CMAKE_SOURCE_DIR}/fts ${CMAKE_SOURCE_DIR}/fts/musl-fts
    ${GEN}/include ${MUSL}/arch/wasm32 ${MUSL}/arch/generic ${MUSL}/include)
  if(pic)
    target_compile_options(${name} PRIVATE -fPIC)
  endif()
endfunction()

musl_objects(libc-static OFF ${sources})
softfloat_objects(softfloat-static OFF)
fts_objects(fts-static OFF)

add_library(c STATIC $<TARGET_OBJECTS:libc-static> $<TARGET_OBJECTS:softfloat-static> $<TARGET_OBJECTS:fts-static>)
set(static_libs c)
if(NOT LINUX_MUSL)
  musl_objects(fork-static OFF ${fork_sources})
  # fork 系の ENOSYS stub（-lbrowseros-nofork。引かないと fork を残すコードがリンクに止まる → browseros/nofork.c）
  musl_objects(nofork-static OFF ${CMAKE_SOURCE_DIR}/browseros/nofork.c)
  musl_objects(setjmp-static OFF ${setjmp_sources})
  target_compile_options(setjmp-static PRIVATE -mllvm -wasm-enable-sjlj -mexception-handling)
  add_library(browseros-fork STATIC $<TARGET_OBJECTS:fork-static>)
  add_library(browseros-nofork STATIC $<TARGET_OBJECTS:nofork-static>)
  add_library(setjmp STATIC $<TARGET_OBJECTS:setjmp-static>)
  list(APPEND static_libs browseros-fork browseros-nofork setjmp)
endif()
set_target_properties(${static_libs} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${SYSROOT_LIB})

if(LINUX_MUSL)
  # Linux のリンクの形（→ spec.md の 5.4）: 静的は crt1.o、動的な PIE は Scrt1.o + crti.o + crtn.o、
  # 静的な PIE は rcrt1.o。Scrt1.o・rcrt1.o は crt1.c を違うフラグで組む（musl の Makefile と同じ）。
  # Scrt1.o は __libc_start_init を持たず、libc.so がローダーの init_modules を呼ぶ方に任せる
  set(crt_flags_crt1 -DCRT)
  set(crt_flags_Scrt1 -DCRT -DCRT_SHARED -fPIC)
  set(crt_flags_rcrt1 -DCRT -DCRT_STATIC_PIE -fPIC)
  foreach(crt crt1 Scrt1 rcrt1 crti crtn)
    if(crt MATCHES "^(Scrt1|rcrt1)$")
      set(crt_source crt1)
    else()
      set(crt_source ${crt})
    endif()
    add_library(${crt}-object OBJECT ${MUSL}/crt/wasm32/${crt_source}.c)
    add_dependencies(${crt}-object generated-headers)
    target_compile_options(${crt}-object PRIVATE ${common_flags} ${crt_flags_${crt}})
    target_include_directories(${crt}-object PRIVATE ${musl_includes})
  endforeach()
  add_custom_command(
    OUTPUT ${SYSROOT_LIB}/crt1.o ${SYSROOT_LIB}/Scrt1.o ${SYSROOT_LIB}/rcrt1.o
           ${SYSROOT_LIB}/crti.o ${SYSROOT_LIB}/crtn.o
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-object> ${SYSROOT_LIB}/crt1.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:Scrt1-object> ${SYSROOT_LIB}/Scrt1.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:rcrt1-object> ${SYSROOT_LIB}/rcrt1.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crti-object> ${SYSROOT_LIB}/crti.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crtn-object> ${SYSROOT_LIB}/crtn.o
    DEPENDS crt1-object Scrt1-object rcrt1-object crti-object crtn-object
    COMMAND_EXPAND_LISTS)
  add_custom_target(crt ALL
    DEPENDS ${SYSROOT_LIB}/crt1.o ${SYSROOT_LIB}/Scrt1.o ${SYSROOT_LIB}/rcrt1.o
            ${SYSROOT_LIB}/crti.o ${SYSROOT_LIB}/crtn.o)
else()
  # crt1.o（crt1-command.o も同じもの）と crt1-reactor.o
  foreach(crt crt1 crt1-reactor)
    add_library(${crt}-object OBJECT ${MUSL}/crt/wasm32/${crt}.c)
    add_dependencies(${crt}-object generated-headers)
    target_compile_options(${crt}-object PRIVATE ${common_flags} -DCRT)
    target_include_directories(${crt}-object PRIVATE ${musl_includes})
  endforeach()
  add_custom_command(
    OUTPUT ${SYSROOT_LIB}/crt1.o ${SYSROOT_LIB}/crt1-command.o ${SYSROOT_LIB}/crt1-reactor.o
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-object> ${SYSROOT_LIB}/crt1.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-object> ${SYSROOT_LIB}/crt1-command.o
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_OBJECTS:crt1-reactor-object> ${SYSROOT_LIB}/crt1-reactor.o
    DEPENDS crt1-object crt1-reactor-object $<TARGET_OBJECTS:crt1-object> $<TARGET_OBJECTS:crt1-reactor-object>
    COMMAND_EXPAND_LISTS)
  add_custom_target(crt ALL DEPENDS ${SYSROOT_LIB}/crt1.o ${SYSROOT_LIB}/crt1-command.o ${SYSROOT_LIB}/crt1-reactor.o)
endif()

if(LINUX_MUSL)
  # musl と同じ分だけ（dlopen・pthread などは libc の中にあるので、-ldl・-lpthread は空の stub。
  # libwasi-emulated-*・libc-printscan-* のようなこの OS 特有のものは持たない）
  set(empty_libs m pthread dl rt util xnet crypt)
else()
  # 空のライブラリ（musl は libc.a に全部を持つ。-lm などを付けて組むプログラムのため）
  set(empty_libs m rt pthread crypt util xnet resolv dl c-printscan-long-double c-printscan-no-floating-point
    wasi-emulated-getpid wasi-emulated-mman wasi-emulated-process-clocks wasi-emulated-signal)
endif()
set(empty_outputs)
foreach(lib IN LISTS empty_libs)
  list(APPEND empty_outputs ${SYSROOT_LIB}/lib${lib}.a)
  add_custom_command(
    OUTPUT ${SYSROOT_LIB}/lib${lib}.a
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${SYSROOT_LIB}/lib${lib}.a
    COMMAND ${LLVM_AR} --format=gnu crs ${SYSROOT_LIB}/lib${lib}.a)
endforeach()
add_custom_target(empty-libs ALL DEPENDS ${empty_outputs})

if(LINUX_MUSL)
  if(NOT WASM_OPT)
    message(FATAL_ERROR "wasm-opt が見つからない（libc.so の asyncify に必要。binaryen を PATH へ、または -DWASM_OPT= で指定）")
  endif()
  # libc.so がこの OS のシステム libc。読み込む側の NEEDED には -soname で書いた名が入る（→ lld の dylink）。
  # TLS の 3 つは wasm-ld が作るシンボルなので名前ごとに export を付ける。
  # 静的な道と同じソースを -fPIC で組み直し、ローダーの入口（init_modules.c）と
  # 動的ローダーへ渡す dlfcn の本体（dlfcn_dyld.c）を足す。
  # dlclose.c は静的側の stub なので共有側では外し、dlfcn_dyld.c の本体を使う
  set(shared_sources ${sources} ${MUSL}/src/env/wasm32/init_modules.c
    ${MUSL}/src/env/wasm32/static_tls_dyld.c
    ${MUSL}/src/ldso/wasm32/dlfcn_dyld.c)
  list(REMOVE_ITEM shared_sources ${MUSL}/src/ldso/dlclose.c)
  musl_objects(libc-shared ON ${shared_sources})
  target_compile_options(libc-shared PRIVATE -fvisibility=default)
  softfloat_objects(softfloat-shared ON)
  fts_objects(fts-shared ON)
  if(THREADS)
    set(abi_exports_name libc-threads.exports)
    # スレッド版は TLS（__wasm_init_tls・__tls_size・__tls_align）を wasm-ld に作ってもらう。
    # これらは isMultithreaded（--shared-memory）のときだけ合成される
    set(libc_so_extra_link -Wl,--shared-memory)
  else()
    set(abi_exports_name libc.exports)
    set(libc_so_extra_link "")
  endif()
  # wasm-opt の asyncify-onlylist はカンマ区切りの直接指定なので、一覧ファイルからここで連結する
  file(STRINGS ${CMAKE_SOURCE_DIR}/abi/asyncify-onlylist.txt asyncify_onlylist)
  list(JOIN asyncify_onlylist "," asyncify_onlylist_arg)
  add_custom_command(
    OUTPUT ${SYSROOT_LIB}/libc.so
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB} ${SYSROOT_SHARE}
    COMMAND ${CMAKE_C_COMPILER}
      --target=${COMPILE_TARGET} -shared -nostdlib
      -Wl,--experimental-pic -Wl,-soname,libc.so -Wl,--no-entry
      -Wl,--unresolved-symbols=import-dynamic ${libc_so_extra_link}
      -Wl,--export-if-defined=__wasm_init_tls
      -Wl,--export-if-defined=__tls_size
      -Wl,--export-if-defined=__tls_align
      -o ${SYSROOT_LIB}/libc.so
      $<TARGET_OBJECTS:libc-shared> $<TARGET_OBJECTS:softfloat-shared> $<TARGET_OBJECTS:fts-shared>
      ${BUILTINS_LIB}
    # fork のために、決めた関数だけに asyncify をかけて配る（spec.md の 8）。
    # 巻き戻しの起きる import は syscall だけで、asyncify の状態はメインから import するので
    # サイドモジュールの relocatable で組む。対象の関数は abi/asyncify-onlylist.txt
    COMMAND ${CMAKE_COMMAND} -E env ${WASM_OPT}
      ${SYSROOT_LIB}/libc.so --asyncify
      --pass-arg=asyncify-imports@browser_os_linux.syscall
      --pass-arg=asyncify-relocatable
      --pass-arg=asyncify-onlylist@${asyncify_onlylist_arg}
      -o ${SYSROOT_LIB}/libc.so.tmp
    COMMAND ${CMAKE_COMMAND} -E rename ${SYSROOT_LIB}/libc.so.tmp ${SYSROOT_LIB}/libc.so
    # export の一覧は abi/ のものと毎回照らす。違ったらここで止まる（→ check-exports.cmake）
    COMMAND ${CMAKE_COMMAND}
      -DLIBC_SO=${SYSROOT_LIB}/libc.so
      -DABI_FILE=${CMAKE_SOURCE_DIR}/abi/${abi_exports_name}
      -DOUT=${GEN}/${abi_exports_name}
      -DLLVM_NM=${LLVM_NM}
      -P ${CMAKE_SOURCE_DIR}/cmake/check-exports.cmake
    # abi/ の一覧は DEPENDS に入れない（最初のビルドでまだ無いので、不在時は check-exports.cmake が
    # update-exports を案内して止まる）
    DEPENDS libc-shared softfloat-shared fts-shared
      $<TARGET_OBJECTS:libc-shared> $<TARGET_OBJECTS:softfloat-shared> $<TARGET_OBJECTS:fts-shared>
      ${CMAKE_SOURCE_DIR}/cmake/check-exports.cmake
      ${CMAKE_SOURCE_DIR}/abi/asyncify-onlylist.txt
    COMMAND_EXPAND_LISTS)
  add_custom_target(shared-libs ALL DEPENDS ${SYSROOT_LIB}/libc.so)
  # abi/ の一覧を今の出力で書き直す（名を足すときはこの一覧を直すコミットで入れる）
  add_custom_target(update-exports
    COMMAND ${CMAKE_COMMAND} -E copy ${GEN}/${abi_exports_name} ${CMAKE_SOURCE_DIR}/abi/${abi_exports_name}
    DEPENDS ${GEN}/${abi_exports_name})
# 共有ライブラリ（1 本の版だけ。スレッド版の共有メモリでは共有ライブラリを組めない）
elseif(NOT THREADS)
  musl_objects(libc-shared ON ${sources})
  target_compile_options(libc-shared PRIVATE -fvisibility=default)
  softfloat_objects(softfloat-shared ON)
  fts_objects(fts-shared ON)
  musl_objects(fork-shared ON ${fork_sources})
  musl_objects(setjmp-shared ON ${setjmp_sources})
  target_compile_options(setjmp-shared PRIVATE -mllvm -wasm-enable-sjlj -mexception-handling)
  set(shared_link --target=${TARGET_TRIPLE} -shared -nostdlib -Wl,--experimental-pic -Wl,--no-entry)
  function(shared_library output flags)
    add_custom_command(
      OUTPUT ${SYSROOT_LIB}/${output}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
      COMMAND ${CMAKE_C_COMPILER} ${shared_link} ${flags} -o ${SYSROOT_LIB}/${output} ${ARGN}
      DEPENDS ${ARGN}
      COMMAND_EXPAND_LISTS)
  endfunction()
  # libc.so は副モジュールのリンクの相手（NEEDED と名前の一覧）で、読み込まれない（→ @progate/browser-dyld）。
  # fork・setjmp を使う関数の名前は、使う側のライブラリから引く
  shared_library(libc.so "-Wl,--unresolved-symbols=import-dynamic"
    $<TARGET_OBJECTS:libc-shared> $<TARGET_OBJECTS:softfloat-shared> $<TARGET_OBJECTS:fts-shared> ${BUILTINS_LIB})
  shared_library(libbrowseros-fork.so "-Wl,--unresolved-symbols=import-dynamic" $<TARGET_OBJECTS:fork-shared>)
  shared_library(libsetjmp.so "-Wl,--unresolved-symbols=import-dynamic" $<TARGET_OBJECTS:setjmp-shared>)
  set(shared_outputs ${SYSROOT_LIB}/libc.so ${SYSROOT_LIB}/libbrowseros-fork.so ${SYSROOT_LIB}/libsetjmp.so)
  # 中身の無い共有ライブラリは、空のオブジェクトから作る
  file(WRITE ${GEN}/empty.c "")
  add_library(empty-shared OBJECT EXCLUDE_FROM_ALL ${GEN}/empty.c)
  target_compile_options(empty-shared PRIVATE --target=${TARGET_TRIPLE} -fPIC)
  foreach(lib dl wasi-emulated-getpid wasi-emulated-mman wasi-emulated-process-clocks wasi-emulated-signal)
    add_custom_command(
      OUTPUT ${SYSROOT_LIB}/lib${lib}.so
      COMMAND ${CMAKE_COMMAND} -E make_directory ${SYSROOT_LIB}
      COMMAND ${CMAKE_C_COMPILER} ${shared_link} -o ${SYSROOT_LIB}/lib${lib}.so $<TARGET_OBJECTS:empty-shared>
      DEPENDS $<TARGET_OBJECTS:empty-shared>
      COMMAND_EXPAND_LISTS)
    list(APPEND shared_outputs ${SYSROOT_LIB}/lib${lib}.so)
  endforeach()
  add_custom_target(shared-libs ALL DEPENDS ${shared_outputs})
endif()

# =============================================================================
# ヘッダー（musl の include と、arch/generic/bits を arch/wasm32/bits で上書きしたもの）

file(GLOB_RECURSE public_headers RELATIVE ${MUSL}/include ${MUSL}/include/*.h)
file(GLOB generic_bits RELATIVE ${MUSL}/arch/generic/bits ${MUSL}/arch/generic/bits/*.h)
file(GLOB arch_bits RELATIVE ${MUSL}/arch/wasm32/bits ${MUSL}/arch/wasm32/bits/*.h)
set(header_outputs)
function(install_header source destination)
  add_custom_command(
    OUTPUT ${SYSROOT_INC}/${destination}
    COMMAND ${CMAKE_COMMAND} -E copy ${source} ${SYSROOT_INC}/${destination}
    DEPENDS ${source})
  set(header_outputs ${header_outputs} ${SYSROOT_INC}/${destination} PARENT_SCOPE)
endfunction()
foreach(header IN LISTS public_headers)
  install_header(${MUSL}/include/${header} ${header})
endforeach()
foreach(header IN LISTS generic_bits)
  if(NOT header IN_LIST arch_bits)
    install_header(${MUSL}/arch/generic/bits/${header} bits/${header})
  endif()
endforeach()
foreach(header IN LISTS arch_bits)
  install_header(${MUSL}/arch/wasm32/bits/${header} bits/${header})
endforeach()
install_header(${GEN}/include/bits/alltypes.h bits/alltypes.h)
install_header(${GEN}/include/bits/syscall.h bits/syscall.h)
install_header(${CMAKE_SOURCE_DIR}/fts/musl-fts/fts.h fts.h)
add_custom_target(headers ALL DEPENDS ${header_outputs})
add_dependencies(c headers)

# =============================================================================
# 公開している記号・ヘッダー・マクロが expected/ と同じか

# 版ごとに無いターゲットもあるので依存は組み立てる
set(check_symbols_deps c crt empty-libs headers)
if(LINUX_MUSL)
  list(APPEND check_symbols_deps shared-libs)
else()
  list(APPEND check_symbols_deps browseros-fork browseros-nofork setjmp)
endif()
add_custom_command(
  OUTPUT ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/undefined-symbols.txt
         ${SYSROOT_SHARE}/include-all.c ${SYSROOT_SHARE}/predefined-macros.txt
  COMMAND ${CMAKE_COMMAND}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DTARGET_TRIPLE=${COMPILE_TARGET}
    -DTHREADS=${THREADS}
    -DSYSROOT_LIB=${SYSROOT_LIB}
    -DSYSROOT_INC=${SYSROOT_INC}
    -DCMAKE_NM=${LLVM_NM}
    -DOUT_DIR=${SYSROOT_SHARE}
    -P ${CMAKE_SOURCE_DIR}/cmake/run-check-symbols.cmake
  DEPENDS ${check_symbols_deps} ${CMAKE_SOURCE_DIR}/cmake/run-check-symbols.cmake)
add_custom_target(check-symbols
  COMMAND diff -wur ${CMAKE_SOURCE_DIR}/expected/${TARGET_TRIPLE} ${SYSROOT_SHARE}
  DEPENDS ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/predefined-macros.txt)
add_custom_target(update-symbols
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${SYSROOT_SHARE} ${CMAKE_SOURCE_DIR}/expected/${TARGET_TRIPLE}
  DEPENDS ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/predefined-macros.txt)
