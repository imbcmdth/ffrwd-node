import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/boxmask.js', out: 'build/boxmask.wasm' });
