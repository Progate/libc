/* BrowserOS: 入口（START）は、入った瞬間のスタックポインターを START_c へ渡す。ほかのアーキテクチャの crt_arch.h と同じく、
 * カーネルは execve(2) で起動のスタック（argc・argv・NULL・envp・NULL・auxv と、その上の文字列・AT_RANDOM の 16 バイト）を
 * 積み、スタックポインターをその頭に置いてから入口を呼ぶ。wasm のスタックポインターは `__stack_pointer` の global で、
 * 静的なプログラムでは定義したものを export し、動的な PIE ではローダーが作って import させる */
__asm__(
".globaltype __stack_pointer, i32\n"
".functype " START "_c (i32) -> ()\n"
".globl " START "\n"
".export_name " START ", " START "\n"
".type " START ",@function\n"
START ":\n"
".functype " START " () -> ()\n"
"	global.get __stack_pointer\n"
"	call " START "_c\n"
"	end_function\n"
);
