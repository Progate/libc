/* BrowserOS: ELF の crti.o は .init の冒頭の片割れだが、wasm のコンストラクタは
 * __wasm_call_ctors にまとまっていて crti/crtn の仕事は無い。Linux のリンクの形
 * （Scrt1.o + crti.o … crtn.o）に合わせて置く、中身の無いオブジェクト */
typedef int browseros_unused_t;
