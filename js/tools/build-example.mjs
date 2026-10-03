import { buildNode } from '../src/build.js';

const bytes = await buildNode({ entry: 'examples/dim.js', out: 'build/dim.wasm' });
console.log(`build/dim.wasm  ${(bytes / 1024 / 1024).toFixed(2)} MiB`);
