// 中身の無い翻訳単位。
//
// BrowserOS の libc はプロセスの CPU 時間を libc 本体に持つ（→ sources/browseros/clocks.c）。それでも `-lwasi-emulated-process-clocks` を付けて組むプログラム
// （wasi-sdk 向けの configure が足すもの）がそのまま組めるよう、同じ名前の空のライブラリを置く。
// 中身を残すと、libc と同じ名前の関数が二重に定義される。
typedef int __browseros_empty_translation_unit;
