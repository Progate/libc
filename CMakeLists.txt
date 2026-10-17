# BrowserOS の libc: musl（libc-top-half/musl）を wasm32 の arch（arch/wasm32・src/*/wasm32・crt/wasm32）で組む。
#
# ソースの選び方は musl の Makefile と同じ: src/*/*.c と src/malloc/mallocng/*.c を全部組み、
# src/<dir>/wasm32/ に同じ名前のものがあればそちらに置き換える。カーネルを呼ぶのは arch の syscall の入口
# （src/internal/wasm32/syscall.c）だけで、カーネルは @progate/browser-os の Linux の syscall の入口。
#
#   cmake -S . -B build -G Ninja -DCMAKE_C_COMPILER=<wasi-sdk>/bin/clang -DTARGET_TRIPLE=wasm32-wasip1
#   ninja -C build && ninja -C build check-symbols
#
# 出口は build/sysroot/{include,lib,share}/<target>。

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> --format=gnu qc <TARGET> <OBJECTS>")
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_ASM_COMPILER_WORKS 1)
set(CMAKE_SYSTEM_NAME Generic)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

set(TARGET_TRIPLE "wasm32-wasip1" CACHE STRING "wasm32-wasip1 か wasm32-wasip1-threads")
set(BUILTINS_LIB "" CACHE FILEPATH "libc.so のリンクに使う compiler-rt（libclang_rt.builtins.a）")
set(CMAKE_C_COMPILER_TARGET ${TARGET_TRIPLE})
set(CMAKE_ASM_COMPILER_TARGET ${TARGET_TRIPLE})

project(browseros-libc LANGUAGES C ASM)

if(TARGET_TRIPLE MATCHES "-threads$")
  set(THREADS ON)
elseif(TARGET_TRIPLE MATCHES "^wasm32-wasip1$")
  set(THREADS OFF)
else()
  message(FATAL_ERROR "知らないターゲット: ${TARGET_TRIPLE}")
endif()

set(MUSL ${CMAKE_SOURCE_DIR}/libc-top-half/musl)
set(GEN ${CMAKE_BINARY_DIR}/gen)
set(SYSROOT ${CMAKE_BINARY_DIR}/sysroot)
set(SYSROOT_INC ${SYSROOT}/include/${TARGET_TRIPLE})
set(SYSROOT_LIB ${SYSROOT}/lib/${TARGET_TRIPLE})
set(SYSROOT_SHARE ${SYSROOT}/share/${TARGET_TRIPLE})

find_program(LLVM_AR NAMES llvm-ar HINTS ${CMAKE_C_COMPILER}/.. REQUIRED)
find_program(LLVM_NM NAMES llvm-nm HINTS ${CMAKE_C_COMPILER}/.. REQUIRED)
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
  --target=${TARGET_TRIPLE}
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
  list(APPEND common_flags -pthread -matomics -mbulk-memory -mthread-model posix -ftls-model=local-exec)
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

# fork と _Fork は -lbrowseros-fork に分ける（→ src/process/wasm32/fork_data.c）
set(fork_sources
  ${MUSL}/src/process/fork.c ${MUSL}/src/process/_Fork.c ${MUSL}/src/process/vfork.c
  ${MUSL}/src/process/wasm32/fork_data.c)
list(REMOVE_ITEM sources ${fork_sources})
# setjmp と longjmp は wasm の例外で作る -lsetjmp（-mllvm -wasm-enable-sjlj で組んだプログラムが使う）
file(GLOB setjmp_sources ${MUSL}/src/setjmp/wasm32/*)
list(REMOVE_ITEM sources ${setjmp_sources})
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
musl_objects(fork-static OFF ${fork_sources})
# fork 系の ENOSYS stub（-lbrowseros-nofork。引かないと fork を残すコードがリンクに止まる → browseros/nofork.c）
musl_objects(nofork-static OFF ${CMAKE_SOURCE_DIR}/browseros/nofork.c)
musl_objects(setjmp-static OFF ${setjmp_sources})
target_compile_options(setjmp-static PRIVATE -mllvm -wasm-enable-sjlj -mexception-handling)

add_library(c STATIC $<TARGET_OBJECTS:libc-static> $<TARGET_OBJECTS:softfloat-static> $<TARGET_OBJECTS:fts-static>)
add_library(browseros-fork STATIC $<TARGET_OBJECTS:fork-static>)
add_library(browseros-nofork STATIC $<TARGET_OBJECTS:nofork-static>)
add_library(setjmp STATIC $<TARGET_OBJECTS:setjmp-static>)
set_target_properties(c browseros-fork browseros-nofork setjmp PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${SYSROOT_LIB})

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

# 空のライブラリ（musl は libc.a に全部を持つ。-lm などを付けて組むプログラムのため）
set(empty_libs m rt pthread crypt util xnet resolv dl c-printscan-long-double c-printscan-no-floating-point
  wasi-emulated-getpid wasi-emulated-mman wasi-emulated-process-clocks wasi-emulated-signal)
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

# 共有ライブラリ（1 本の版だけ。スレッド版の共有メモリでは共有ライブラリを組めない）
if(NOT THREADS)
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

add_custom_command(
  OUTPUT ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/undefined-symbols.txt
         ${SYSROOT_SHARE}/include-all.c ${SYSROOT_SHARE}/predefined-macros.txt
  COMMAND ${CMAKE_COMMAND}
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
    -DTARGET_TRIPLE=${TARGET_TRIPLE}
    -DTHREADS=${THREADS}
    -DSYSROOT_LIB=${SYSROOT_LIB}
    -DSYSROOT_INC=${SYSROOT_INC}
    -DCMAKE_NM=${LLVM_NM}
    -DOUT_DIR=${SYSROOT_SHARE}
    -P ${CMAKE_SOURCE_DIR}/cmake/run-check-symbols.cmake
  DEPENDS c browseros-fork browseros-nofork setjmp crt empty-libs headers ${CMAKE_SOURCE_DIR}/cmake/run-check-symbols.cmake)
add_custom_target(check-symbols
  COMMAND diff -wur ${CMAKE_SOURCE_DIR}/expected/${TARGET_TRIPLE} ${SYSROOT_SHARE}
  DEPENDS ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/predefined-macros.txt)
add_custom_target(update-symbols
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${SYSROOT_SHARE} ${CMAKE_SOURCE_DIR}/expected/${TARGET_TRIPLE}
  DEPENDS ${SYSROOT_SHARE}/defined-symbols.txt ${SYSROOT_SHARE}/predefined-macros.txt)
