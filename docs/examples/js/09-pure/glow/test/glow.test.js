import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/glow.js';

function open() {
  const v = BoundStream.video('v', 0, 4, 4, 'rgba', new Rational(1, 15)).rate(new Rational(15, 1));
  return new Harness(node, '{"every":3}', [v]);
}

/** A picture lit at one pixel, which moves along the top row. */
function lit(n) {
  const pixels = new Uint8Array(4 * 4 * 4);
  pixels.fill(255, (n % 4) * 4, (n % 4) * 4 + 4);
  return pixels;
}

/** Every row of `ticks` ticks handed to `workers` instances in turn, in pts
 * order. */
function rows(ticks, workers) {
  const instances = Array.from({ length: workers }, open);
  const rows = [];
  for (let n = 0; n < ticks; n += 1) {
    const worker = instances[n % workers];
    const tick = worker.tick(n).ordinal(n).frame(0, n, lit(n));
    rows.push(...worker.process(tick).messages('glows'));
  }
  return rows.sort(([a], [b]) => a - b);
}

test('any number of workers write the same rows', () => {
  const alone = rows(10, 1);
  assert.equal(alone.length, 10);
  assert.deepEqual(rows(10, 2), alone);
  assert.deepEqual(rows(10, 3), alone);
});

test('a sighting starts every so many frames', () => {
  const ids = rows(7, 1).map(([, row]) => row);
  assert.ok(ids[2].startsWith('{"start_t":0,"id":0,'), ids[2]);
  assert.ok(ids[3].startsWith('{"start_t":0.2,"id":1,'), ids[3]);
});
