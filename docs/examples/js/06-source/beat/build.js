import { mkdirSync, writeFileSync } from 'node:fs';

import { componentize } from '@bytecodealliance/componentize-js';
import { DISABLED, witDir } from '@ffrwd/node/build';
import { build } from 'esbuild';

// `buildNode` turns the clocks off, and `beat` reads the wall clock and waits
// on it. This is the same build with the clocks kept.
mkdirSync('build', { recursive: true });
await build({
  entryPoints: ['src/beat.js'],
  outfile: 'build/beat.bundle.js',
  bundle: true,
  format: 'esm',
  platform: 'neutral',
  target: 'es2022',
  mainFields: ['module', 'main'],
  logLevel: 'warning',
});
const { component } = await componentize({
  sourcePath: 'build/beat.bundle.js',
  witPath: witDir(),
  worldName: 'node-module',
  disableFeatures: DISABLED.filter((feature) => feature !== 'clocks'),
});
writeFileSync('build/beat.wasm', component);
