import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/invert.js', out: 'build/invert.wasm' });
