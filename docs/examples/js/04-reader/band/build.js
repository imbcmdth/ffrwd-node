import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/band.js', out: 'build/band.wasm' });
