# BrowserOS: 新しいスレッドの入口。カーネルの clone(CLONE_THREAD) が、新しいワーカーで立てた同じモジュールの
# この export を呼ぶ（→ @progate/browser-wasi の threads.ts。名前は wasi-threads の決まり）。
# C を走らせる前に、スタックと TLS の場所（clone.c の struct thread_start のオフセット 0 と 4）を決める。

	.text

	.export_name	wasi_thread_start, wasi_thread_start

	.globaltype	__stack_pointer, i32
	.globaltype	__tls_base, i32
	.functype	__browseros_thread_entry (i32, i32) -> ()

	.hidden	wasi_thread_start
	.globl	wasi_thread_start
	.type	wasi_thread_start,@function

wasi_thread_start:
	.functype	wasi_thread_start (i32, i32) -> ()

	local.get	1
	i32.load	0
	global.set	__stack_pointer

	local.get	1
	i32.load	4
	global.set	__tls_base

	local.get	0
	local.get	1
	call	__browseros_thread_entry

	end_function
