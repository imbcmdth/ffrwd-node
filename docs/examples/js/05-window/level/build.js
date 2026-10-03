import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/level.js', out: 'build/level.wasm' });
