import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/blend.js', out: 'build/blend.wasm' });
