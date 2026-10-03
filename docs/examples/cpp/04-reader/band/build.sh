#!/bin/sh
# Builds build/band.wasm: the node compiled with wasi-sdk and linked with
# the ffrwd-node C++ library, which `sh build.sh modules` in the SDK's cpp/
# directory builds.
#
#   FFRWD_NODE        the SDK's cpp/ directory
#   FFRWD_NODE_BUILD  where its library was built, $FFRWD_NODE/build unless set
#   WASI_SDK          wasi-sdk 34 or newer
set -eu
cd "$(dirname "$0")"
sdk=${FFRWD_NODE:-../../../../../cpp}
lib=${FFRWD_NODE_BUILD:-$sdk/build}
wasi=${WASI_SDK:?set WASI_SDK to your wasi-sdk}
cxx="$wasi/bin/clang++ --target=wasm32-wasip2 -std=c++23 -fno-exceptions -fno-rtti"
mkdir -p build

$cxx -O2 -I"$sdk/include" -c src/band.cpp -o build/band.o
$cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -o build/band.wasm build/band.o \
    "$lib/wasm/libffrwd-node.a" "$lib/wasm/node_module.o" "$lib/gen/node_module_component_type.o"
