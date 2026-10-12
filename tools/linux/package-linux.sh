#!/bin/bash
# 宁州一梦 多人 Linux 版打包脚本
# 用法：tools/linux/package-linux.sh [输出目录，默认 dist/linux/app]
# 1. 用 CMake 构建 ningzhou_web（Release，静态 libstdc++）
# 2. 复制 web/ 前端，把 assets 下的 PNG 转成 WebP（仓库里保留 PNG 原图，WebP 只在打包时生成）
#    服务端收到 *.png 请求时若存在同名 .webp 会直接返回 WebP，前端引用不用改。
# 3. 生成 run.sh（0.0.0.0:25001）
# 依赖：cmake g++ cwebp（apt install cmake g++ webp）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/dist/linux/app}"
BUILD="$ROOT/build-linux"
QUALITY="${WEBP_QUALITY:-82}"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target ningzhou_web

rm -rf "$OUT"; mkdir -p "$OUT/web"
cp "$BUILD/ningzhou_web" "$OUT/"; strip "$OUT/ningzhou_web" || true
(cd "$ROOT/web" && cp ./*.html ./*.mjs ./*.css "$OUT/web/")
command -v cwebp >/dev/null || { echo "缺少 cwebp（apt install webp）"; exit 1; }
(cd "$ROOT/web" && find assets -name '*.png' -print0) | while IFS= read -r -d '' f; do
  mkdir -p "$OUT/web/$(dirname "$f")"
  cwebp -quiet -q "$QUALITY" -m 6 -mt "$ROOT/web/$f" -o "$OUT/web/${f%.png}.webp"
done
cat > "$OUT/run.sh" <<'R'
#!/bin/bash
cd "$(dirname "$0")"
chmod +x ./ningzhou_web 2>/dev/null
exec ./ningzhou_web --no-open --host 0.0.0.0 --port 25001 --web ./web --data ./saves
R
chmod +x "$OUT/run.sh" "$OUT/ningzhou_web"
echo "打包完成：$OUT（$(du -sh "$OUT" | cut -f1)）"
