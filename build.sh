#!/bin/sh
# Builds the library and the example nodes to wasm components, and the tests
# for the host.
#
#   sh build.sh            components and tests
#   sh build.sh modules    build/dim.wasm and build/spot.wasm
#   sh build.sh test       build/host/tests, then runs it; with FFRWD_WIT_DIR
#                          set, first checks wit/av.wit against the av.wit
#                          there
#
# What it needs:
#
#   WASI_SDK      wasi-sdk 34 or newer, unpacked. Its clang targets
#                 wasm32-wasip2 and links a component itself.
#   WIT_BINDGEN   wit-bindgen 0.57.1, for the C bindings of wit/av.wit.
#   WASM_TOOLS    wasm-tools 1.258.0, to validate what was built (optional).
#   CXX           a host C++23 compiler for the tests (clang 18 or newer).
#
# Each defaults to the name on PATH, WASI_SDK to C:/tools/wasi-sdk-34.0-
# x86_64-windows or /opt/wasi-sdk, whichever is there.
#
# A module of your own builds the same way: compile its sources with the
# flags in `wasm_cxx` below, and link them with build/wasm/libffrwd-node.a
# and the two binding objects, as `link_module` does. The library is built
# for size and a node's own code for speed, where its pixels are.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
cd "$here"

if [ -z "${WASI_SDK:-}" ]; then
    if [ -d C:/tools/wasi-sdk-34.0-x86_64-windows ]; then
        WASI_SDK=C:/tools/wasi-sdk-34.0-x86_64-windows
    else
        WASI_SDK=/opt/wasi-sdk
    fi
fi
wit_bindgen=${WIT_BINDGEN:-wit-bindgen}
wasm_tools=${WASM_TOOLS:-wasm-tools}
host_cxx=${CXX:-clang++}

out=build
gen=$out/gen
objects=$out/wasm

wasm_cc="$WASI_SDK/bin/clang --target=wasm32-wasip2 -Os -ffunction-sections -fdata-sections"
wasm_cxx="$WASI_SDK/bin/clang++ --target=wasm32-wasip2 -std=c++23 -Os -fno-exceptions -fno-rtti \
    -ffunction-sections -fdata-sections -Iinclude -I$gen"
library="json fields time shape params out glue"
examples="dim spot"

bindings() {
    mkdir -p "$gen" "$objects"
    "$wit_bindgen" c wit/av.wit --world node-module --out-dir "$gen" >/dev/null 2>&1
    $wasm_cc -c "$gen/node_module.c" -o "$objects/node_module.o"
}

lib() {
    rm -f "$objects/libffrwd-node.a"
    for name in $library; do
        $wasm_cxx -Os -c "src/$name.cpp" -o "$objects/$name.o"
    done
    "$WASI_SDK/bin/llvm-ar" rcs "$objects/libffrwd-node.a" $(for name in $library; do echo "$objects/$name.o"; done)
}

link_module() {
    name=$1
    $wasm_cxx -O2 -c "examples/$name/$name.cpp" -o "$objects/$name.o"
    $wasm_cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all \
        -o "$out/$name.wasm" "$objects/$name.o" "$objects/libffrwd-node.a" \
        "$objects/node_module.o" "$gen/node_module_component_type.o"
    if command -v "$wasm_tools" >/dev/null 2>&1; then
        "$wasm_tools" validate "$out/$name.wasm"
    fi
    echo "built $out/$name.wasm ($(wc -c < "$out/$name.wasm") bytes)"
}

modules() {
    bindings
    lib
    for name in $examples; do link_module "$name"; done
}

tests() {
    if [ -n "${FFRWD_WIT_DIR:-}" ]; then
        cmp wit/av.wit "$FFRWD_WIT_DIR/av.wit" || { echo "wit/av.wit is not $FFRWD_WIT_DIR/av.wit" >&2; exit 1; }
    fi
    mkdir -p "$out/host"
    sources=""
    for name in $library; do
        [ "$name" = glue ] || sources="$sources src/$name.cpp"
    done
    $host_cxx -std=c++23 -O1 -g -Wall -Wextra -Iinclude -Iexamples -o "$out/host/tests" \
        $sources tests/*.cpp
    "$out/host/tests"
}

case "${1:-all}" in
    modules) modules ;;
    test) tests ;;
    all) modules && tests ;;
    *) echo "usage: sh build.sh [modules|test|all]" >&2; exit 2 ;;
esac
