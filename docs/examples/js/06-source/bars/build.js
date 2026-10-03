import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/bars.js', out: 'build/bars.wasm' });
