#!/bin/sh
# Builds build/levels.wasm: the node compiled with wasi-sdk and linked with
# the ffrwd-node C++ library, which `sh build.sh modules` in the SDK's cpp/
# directory builds.
#
#   sh build.sh         build/levels.wasm
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
wasi=${WASI_SDK:-C:/tools/wasi-sdk-34.0-x86_64-windows}
cxx="$wasi/bin/clang++ --target=wasm32-wasip2 -std=c++23 -fno-exceptions -fno-rtti"
mkdir -p build

if [ "${1:-}" = test ]; then
    ${CXX:-clang++} -std=c++23 -O1 -I"$sdk/include" -I"$sdk/tests" -o build/levels_test \
        $(ls "$sdk"/src/*.cpp | grep -v glue.cpp) "$sdk/tests/main.cpp" src/levels_test.cpp
    exec build/levels_test
fi

$cxx -O2 -I"$sdk/include" -c src/levels.cpp -o build/levels.o
$cxx -mexec-model=reactor -Wl,--gc-sections -Wl,--strip-all -o build/levels.wasm build/levels.o \
    "$lib/wasm/libffrwd-node.a" "$lib/wasm/node_module.o" "$lib/gen/node_module_component_type.o"
