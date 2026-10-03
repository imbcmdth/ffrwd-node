#!/bin/sh
# Builds the module into build/invert.wasm. COMPONENTIZE_GO and GO name
# componentize-go and a Go of 1.25.5 or newer when they are not on PATH.
set -eu
cd "$(dirname "$0")"
componentize_go=${COMPONENTIZE_GO:-componentize-go}
go=${GO:-go}
wit="$("$go" list -m -f '{{.Dir}}' github.com/imbcmdth/ffrwd-node/go)/wit"
"$componentize_go" -d "$wit" -w ffrwd:av/node-module@0.19.1 build --go "$go" -o build/invert.wasm
