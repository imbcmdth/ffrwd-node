import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/glow.js', out: 'build/glow.wasm' });
