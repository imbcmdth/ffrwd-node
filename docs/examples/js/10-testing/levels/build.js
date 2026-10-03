import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/levels.js', out: 'build/levels.wasm' });
