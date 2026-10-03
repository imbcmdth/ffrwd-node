#!/bin/sh
# Builds build/glow.wasm: the node compiled with wasi-sdk and linked with
# the ffrwd-node C++ library, which `sh build.sh modules` in the SDK's cpp/
# directory builds.
#
#   sh build.sh         build/glow.wasm
#   sh build.sh test    builds the tests for this machine and runs them
#
#   FFRWD_NODE        the SDK's cpp/ directory
#   FFRWD_NODE_BUILD  where its library was built, $FFRWD_NODE/build unless set
#   WASI_SDK          wasi-sdk 34 or newer
#   CXX               a C++23 compiler for this machine, for the tests
set -eu
cd "$(dirname "$0")"
sdk=${FFRWD_NODE:-../../../../../cpp}
lib=${FFRWD_NODE_BUILD:-$sdk/build}
wasi=${WASI_SDK:?set WASI_SDK to your wasi-sdk}
cxx="$wasi/bin/clang++ --target=wasm32-wasip2 -std=c++23 -fno-exceptions -fno-rtti"
mkdir -p build

if [ "${1:-}" = test ]; then
    ${CXX:-clang++} -std=c++23 -O1 -I"$sdk/include" -I"$sdk/tests" -o build/glow_test \
        $(ls "$sdk"/src/*.cpp | grep -v glue.cpp) "$sdk/tests/main.cpp" src/glow_test.cpp
    exec build/glow_test
fi

$cxx -O2 -I"$sdk/include" -c src/glow.cpp -o build/glow.o
$cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -o build/glow.wasm build/glow.o \
    "$lib/wasm/libffrwd-node.a" "$lib/wasm/node_module.o" "$lib/gen/node_module_component_type.o"
