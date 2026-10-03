import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/tally.js', out: 'build/tally.wasm' });
