// Asks the sidecar to describe the built example and print its shape.
// FFRWD_WASM names the sidecar when `ffrwd-wasm` is not on PATH.

import { execFileSync } from 'node:child_process';

const sidecar = process.env.FFRWD_WASM?.trim() || 'ffrwd-wasm';
for (const args of [['--describe', 'build/dim.wasm'], ['--shape', 'build/dim.wasm', '--bound', 'v']]) {
  console.log(execFileSync(sidecar, args, { encoding: 'utf8' }).trim());
}
