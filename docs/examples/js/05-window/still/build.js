import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/still.js', out: 'build/still.wasm' });
