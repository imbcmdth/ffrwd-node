#!/bin/sh
# Copies the repo's wit into the module, which carries its own since a Go
# module holds nothing outside its directory, and regenerates the bindings
# from it. COMPONENTIZE_GO names componentize-go when it is not on PATH.
set -eu
cd "$(dirname "$0")"
componentize_go=${COMPONENTIZE_GO:-componentize-go}
cp ../wit/av.wit wit/av.wit
"$componentize_go" -d wit -w ffrwd:av/node-module@0.19.1 bindings \
    --pkg-name github.com/imbcmdth/ffrwd-node/go/internal/wit \
    --export-pkg-name github.com/imbcmdth/ffrwd-node/go/internal/wit/export \
    -o internal/wit --format
# The bindings build for wasip1 alone, so the host's go test ./... passes
# them by.
for file in $(find internal/wit -name '*.go' -o -name '*.s'); do
    if ! head -1 "$file" | grep -q '^//go:build wasip1$'; then
        printf '//go:build wasip1\n\n' | cat - "$file" > "$file.tagged" && mv "$file.tagged" "$file"
    fi
done
