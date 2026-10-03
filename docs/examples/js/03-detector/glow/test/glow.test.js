import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/glow.js';

/** The `start_t` of every row of four lit ticks handed to `workers`
 * instances in turn, in pts order. */
function starts(workers) {
  const open = () => new Harness(node, '', [BoundStream.video('v', 0, 2, 2, 'rgba', new Rational(1, 10))]);
  const instances = Array.from({ length: workers }, open);
  const rows = [];
  for (let n = 0; n < 4; n += 1) {
    const worker = instances[n % workers];
    const tick = worker.tick(n).ordinal(n).frame(0, n, new Uint8Array(16).fill(255));
    rows.push(...worker.process(tick).messages('glows'));
  }
  rows.sort(([a], [b]) => a - b);
  return rows.map(([, row]) => row.split(',')[0]);
}

test('spans kept across ticks split with the workers', () => {
  assert.deepEqual(starts(1), Array(4).fill('{"start_t":0'));
  assert.deepEqual(starts(2), ['{"start_t":0', '{"start_t":0.1', '{"start_t":0', '{"start_t":0.1']);
});
