#!/usr/bin/env bash
set -e

# Run this script from the bgp_sim/ root directory:
#   bash bgp_web/build_wasm.sh

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT_DIR="$SCRIPT_DIR/public"

echo "========================================="
echo "  BGP Simulator — WebAssembly Build"
echo "========================================="
echo "Root : $ROOT_DIR"
echo "Output: $OUT_DIR/bgp_simulator.js"
echo ""

mkdir -p "$OUT_DIR"

emcc -std=c++17 -O2 \
    -I"$ROOT_DIR/include" \
    "$SCRIPT_DIR/wasm_src/bgp_wasm.cpp" \
    "$ROOT_DIR/src/Route.cpp" \
    "$ROOT_DIR/src/BGP.cpp" \
    "$ROOT_DIR/src/Router.cpp" \
    "$ROOT_DIR/src/Network.cpp" \
    -s WASM=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","FS"]' \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='BGPModule' \
    -s ENVIRONMENT='web' \
    -s FORCE_FILESYSTEM=1 \
    -s INITIAL_MEMORY=268435456 \
    -o "$OUT_DIR/bgp_simulator.js"

echo ""
echo "========================================="
echo "  Build complete!"
echo "  Generated:"
echo "    bgp_web/public/bgp_simulator.js"
echo "    bgp_web/public/bgp_simulator.wasm"
echo "========================================="
