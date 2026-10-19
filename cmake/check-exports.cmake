# libc.so の export を abi/ の一覧と照らす。
# libc.so はこの OS のシステム libc なので、export が ABI。名を足すときは一覧を直す
# コミットで入れ、名が消えるのは ABI の版を上げる話なので止めて聞く（→ spec.md の 4）
#
# export の一覧は llvm-nm --extern-only --defined-only で取る（wasm の .so では
# Export の節の名と一致する。hidden の内部名は出ない）
#
# 必要なもの:
#   LIBC_SO   … 調べる libc.so
#   ABI_FILE  … 正とする一覧（abi/libc.exports・abi/libc-threads.exports）

#   OUT       … 実際の export を書き出す先（update-exports が abi/ へ写す）
#   LLVM_NM   … llvm-nm
#
# cmake -P で呼ばれるので、まず IN_LIST（CMP0057）を有効にする
cmake_policy(SET CMP0057 NEW)

execute_process(
  COMMAND ${LLVM_NM} --extern-only --defined-only ${LIBC_SO}
  OUTPUT_VARIABLE nm_out
  COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" nm_lines "${nm_out}")
set(exports)
foreach(line IN LISTS nm_lines)
  # llvm-nm の形: `アドレス 種別 名前`
  string(REGEX MATCH " [A-Za-z] ([^ ]+)$" _m "${line}")
  if(CMAKE_MATCH_1)
    list(APPEND exports "${CMAKE_MATCH_1}")
  endif()
endforeach()
list(SORT exports)
list(REMOVE_DUPLICATES exports)

# 絶対に入っている入り口（fenv の wasm グローバル。→ spec.md の 3・4）
foreach(required __fe_status)
  if(NOT required IN_LIST exports)
    message(FATAL_ERROR "libc.so の export に ${required} が無い。fenv の組み方を見直す")
  endif()
endforeach()

# __c_longjmp のタグは定義してはいけない（import であること）。例外の同一性は Tag の
# インスタンスで決まるので、libc.so が自分の Tag を export すると、先に instantiate される
# メインにローダーが配った共有 Tag とずれて、.so からの siglongjmp がメインで捕まらない。
# 全員が env から import してローダーの配る 1 個の Tag を使う（__cpp_exception と同じ形）
if(__c_longjmp IN_LIST exports)
  message(FATAL_ERROR "libc.so が __c_longjmp のタグを export している。setjmp/wasm32/rt.c の BROWSEROS_LIBC_SO 組み方を見直す")
endif()

string(JOIN "\n" exports_text ${exports})
file(WRITE ${OUT} "${exports_text}\n")

if(NOT EXISTS ${ABI_FILE})
  message(FATAL_ERROR "ABI の一覧 ${ABI_FILE} が無い。`ninja update-exports` で今の出力を正にする")
endif()
file(STRINGS ${ABI_FILE} abi_exports)
if(NOT "${exports}" STREQUAL "${abi_exports}")
  message(FATAL_ERROR
    "libc.so の export が ${ABI_FILE} と違う。実際のものは ${OUT} を見る。"
    "名を足すときは `ninja update-exports` で一覧を直すコミットで入れる。"
    "名が消えているときは ABI の版を上げる話なので、消えた名を確かめて止める")
endif()
