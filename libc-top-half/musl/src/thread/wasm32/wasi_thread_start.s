# BrowserOS: 新しいスレッドの入口。カーネルの clone(CLONE_THREAD) が、新しいワーカーで立てた同じモジュールの
# この export を呼ぶ（→ @progate/browser-wasi の threads.ts。名前は wasi-threads の決まり）。
# C を走らせる前に、スタックと TLS の場所（clone.c の struct thread_start のオフセット 0 と 4）を決める。

	.text

	.export_name	wasi_thread_start, wasi_thread_start

	.globaltype	__stack_pointer, i32
	.globaltype	__tls_base, i32
	.functype	__browseros_thread_entry (i32, i32) -> ()
	.functype	__wasm_init_tls (i32) -> ()

	.hidden	wasi_thread_start
	.globl	wasi_thread_start
	.type	wasi_thread_start,@function

wasi_thread_start:
	.functype	wasi_thread_start (i32, i32) -> ()

	local.get	1
	i32.load	0
	global.set	__stack_pointer

	# __wasm_init_tls は __tls_base を置き、.tdata の初めの値をこのスレッドの区画へ写し、
	# TLS の番地を差す GOT.data エントリ（__tls_base + offset）もこのインスタンスの分として
	# 当て直す（呼ばないと __pthread_self() など GOT 経由の TLS が 0 番地を指す）
	local.get	1
	i32.load	4
	call	__wasm_init_tls

	local.get	0
	local.get	1
	call	__browseros_thread_entry

	end_function
