import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/cutin.js', out: 'build/cutin.wasm' });
