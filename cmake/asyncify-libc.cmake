# libc.so に fork のための asyncify をかける（spec.md の 8）。対象は abi/asyncify-onlylist.txt の関数と、
# pthread_create.c の static な start（主スレッド以外の fork で、子がスレッドの入口から巻き直すときに通る）。
#
# start は名前では指せない。同じ名前の static 関数が timer_create.c・mq_notify.c にもあり、binaryen は重なった名前に
# 関数の番号を付けて分ける（start_2788 など）ので、番号は libc を直すたびに変わる。そこで binaryen の呼び出しの
# グラフから、start（と番号の付いた名前）のうち __pthread_exit を呼ぶものを探す。ちょうど 1 つでなければ止める
#
# 必要なもの:
#   WASM_OPT  … wasm-opt
#   LIBC_SO   … asyncify をかける libc.so（書き換える）
#   ONLYLIST  … abi/asyncify-onlylist.txt

execute_process(
  COMMAND ${WASM_OPT} ${LIBC_SO} --print-call-graph -o ${LIBC_SO}.call-graph.wasm
  OUTPUT_VARIABLE graph
  RESULT_VARIABLE status)
file(REMOVE ${LIBC_SO}.call-graph.wasm)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "wasm-opt --print-call-graph が失敗した（${status}）")
endif()
string(REGEX MATCHALL "\"start(_[0-9]+)?\" -> \"__pthread_exit\"" found "${graph}")
list(LENGTH found count)
if(NOT count EQUAL 1)
  message(FATAL_ERROR "pthread_create.c の start が見つからない（__pthread_exit を呼ぶ start が ${count} 個）: ${found}")
endif()
string(REGEX REPLACE "^\"([^\"]+)\".*" "\\1" thread_start "${found}")

file(STRINGS ${ONLYLIST} names)
list(APPEND names ${thread_start})
list(JOIN names "," onlylist)
execute_process(
  COMMAND ${WASM_OPT} ${LIBC_SO} --asyncify
    --pass-arg=asyncify-imports@browser_os_linux.syscall
    --pass-arg=asyncify-relocatable
    --pass-arg=asyncify-onlylist@${onlylist}
    -o ${LIBC_SO}.tmp
  RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "wasm-opt --asyncify が失敗した（${status}）")
endif()
file(RENAME ${LIBC_SO}.tmp ${LIBC_SO})
