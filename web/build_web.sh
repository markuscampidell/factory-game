#!/usr/bin/env bash
# Builds the browser version of the game into build-web/site/
# (index.html + index.js + index.wasm + index.data).
# Needs Emscripten (emcc / em++ on the PATH). The GitHub workflow in
# .github/workflows/web.yml runs this on every push - you don't need
# Emscripten on your own PC.
set -euo pipefail
cd "$(dirname "$0")/.."
RAYLIB_VERSION=5.5
OUT=build-web

# raylib for the web, built once and reused
if [ ! -f "$OUT/raylib/src/libraylib.web.a" ] && [ ! -f "$OUT/raylib/src/libraylib.a" ]; then
  rm -rf "$OUT/raylib"
  git clone --depth 1 --branch "$RAYLIB_VERSION" https://github.com/raysan5/raylib.git "$OUT/raylib"
  make -C "$OUT/raylib/src" PLATFORM=PLATFORM_WEB -j4
fi
RAYLIB_LIB="$OUT/raylib/src/libraylib.web.a"
[ -f "$RAYLIB_LIB" ] || RAYLIB_LIB="$OUT/raylib/src/libraylib.a"

mkdir -p "$OUT/site"
em++ -std=c++17 -O2 -DNDEBUG -Ithird_party -I"$OUT/raylib/src" \
  src/core/*.cpp src/sim/*.cpp src/game/*.cpp "$RAYLIB_LIB" \
  -o "$OUT/site/index.html" \
  -sUSE_GLFW=3 -sALLOW_MEMORY_GROWTH=1 -sFORCE_FILESYSTEM=1 -lidbfs.js \
  --preload-file assets --shell-file web/shell.html
echo "Browser version built: $OUT/site/index.html"
