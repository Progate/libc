/* BrowserOS: 動的リンカー（ldso）は使わない（動的に読むモジュールは @progate/browser-dyld が扱う） */
#define LDSO_ARCH "wasm32"
#define TPOFF_K 0
