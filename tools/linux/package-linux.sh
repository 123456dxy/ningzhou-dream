#!/bin/bash
# 宁州一梦 多人 Linux 版打包脚本
# 用法：tools/linux/package-linux.sh [输出目录，默认 dist/linux/app]
# 1. 用 CMake 构建 ningzhou_web（Release，静态 libstdc++）
# 2. 复制整个 web/ 前端（含 v2/ 手机试行版与 assets；若有新增 PNG 会在打包时转成 WebP）
#    服务端收到旧的 *.png 请求时若存在同名 .webp 会直接返回 WebP，兼容旧存档里的立绘路径。
# 3. 生成 run.sh（0.0.0.0:25001）
# 依赖：cmake g++（仅当 web/assets 里有 PNG 时才需要 cwebp）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="${1:-$ROOT/dist/linux/app}"
BUILD="$ROOT/build-linux"
QUALITY="${WEBP_QUALITY:-82}"

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target ningzhou_web

rm -rf "$OUT"; mkdir -p "$OUT/web"
cp "$BUILD/ningzhou_web" "$OUT/"; strip "$OUT/ningzhou_web" || true
# 整个 web/ 目录原样复制（含 v2/ 等子目录），再去掉说明文档并把残留 PNG 转成 WebP
cp -a "$ROOT/web/." "$OUT/web/"
find "$OUT/web" -name '*.md' -delete
(cd "$OUT/web" && find . -name '*.png' -print0) | while IFS= read -r -d '' f; do
  command -v cwebp >/dev/null || { echo "缺少 cwebp（apt install webp）"; exit 1; }
  [ -f "$OUT/web/${f%.png}.webp" ] || cwebp -quiet -q "$QUALITY" -m 6 -mt "$OUT/web/$f" -o "$OUT/web/${f%.png}.webp"
  rm -f "$OUT/web/$f"
done
cat > "$OUT/run.sh" <<'R'
#!/bin/bash
cd "$(dirname "$0")"
chmod +x ./ningzhou_web 2>/dev/null
exec ./ningzhou_web --no-open --host 0.0.0.0 --port 25001 --web ./web --data ./saves
R
chmod +x "$OUT/run.sh" "$OUT/ningzhou_web"
echo "打包完成：$OUT（$(du -sh "$OUT" | cut -f1)）"
