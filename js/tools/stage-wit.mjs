// Copies the repo's wit/av.wit into the package before it is packed, so a
// packed copy carries the world it builds against.

import { copyFileSync, mkdirSync } from 'node:fs';

mkdirSync('wit', { recursive: true });
copyFileSync('../wit/av.wit', 'wit/av.wit');
