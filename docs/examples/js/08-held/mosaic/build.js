import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/mosaic.js', out: 'build/mosaic.wasm' });
