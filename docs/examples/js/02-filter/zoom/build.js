import { buildNode } from '@ffrwd/node/build';

await buildNode({ entry: 'src/zoom.js', out: 'build/zoom.wasm' });
