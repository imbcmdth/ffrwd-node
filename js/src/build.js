// Builds a module's entry into a wasip2 component of the node world:
// esbuild bundles it into the one file componentize-js takes, and
// componentize-js compiles that against `ffrwd:av@0.19.1`.

import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));

/** The directory holding `av.wit`: the package's own copy when it was
 * packed, the repo's otherwise. */
export function witDir() {
  for (const dir of [join(HERE, '..', 'wit'), join(HERE, '..', '..', 'wit')]) {
    if (existsSync(join(dir, 'av.wit'))) return dir;
  }
  throw new Error('no av.wit beside @ffrwd/node');
}

// StarlingMonkey pulls in wasi:http for its fetch machinery unless these are
// off, and the module would then ask for a capability it never uses.
export const DISABLED = ['http', 'fetch-event', 'random', 'clocks'];

/** Bundles `entry` and componentizes it into `out`. `aot` runs weval over
 * the JavaScript, which needs `@bytecodealliance/weval` installed. Answers
 * the component's size in bytes. */
export async function buildNode({ entry, out, aot = false }) {
  const { build } = await import('esbuild');
  const { componentize } = await import('@bytecodealliance/componentize-js');
  mkdirSync(dirname(out), { recursive: true });
  const bundle = out.replace(/\.wasm$/, '') + '.bundle.js';
  await build({
    entryPoints: [entry],
    outfile: bundle,
    bundle: true,
    format: 'esm',
    platform: 'neutral',
    target: 'es2022',
    mainFields: ['module', 'main'],
    legalComments: 'inline',
    logLevel: 'warning',
  });
  const { component } = await componentize({
    sourcePath: bundle,
    witPath: witDir(),
    worldName: 'node-module',
    disableFeatures: DISABLED,
    enableAot: aot,
  });
  writeFileSync(out, component);
  return component.length;
}
