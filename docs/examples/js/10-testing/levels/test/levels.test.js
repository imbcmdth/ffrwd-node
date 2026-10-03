import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node } from '../src/levels.js';

function open(params) {
  const v = BoundStream.video('v', 0, 2, 1, 'rgba', new Rational(1, 25));
  return new Harness(node, params, [v]);
}

test('black and white reach the ends', () => {
  const levels = open('');
  const tick = levels.tick(0).frame(0, 0, Uint8Array.of(16, 16, 16, 255, 235, 126, 235, 255));
  const emitted = levels.process(tick);
  const [payload] = emitted.on('v');
  assert.equal(payload.tag, 'frame');
  assert.deepEqual([...payload.val.data], [0, 0, 0, 255, 255, 128, 255, 255]);
});

test('the full range passes the frame on', () => {
  const levels = open('{"black":0,"white":255}');
  const emitted = levels.process(levels.tick(0).frame(0, 0, new Uint8Array(8)));
  assert.deepEqual(emitted.on('v').map((payload) => payload.tag), ['same']);
});

test('params change between ticks', () => {
  const levels = open('');
  levels.setParams('{"black":0,"white":255}');
  const emitted = levels.process(levels.tick(0).frame(0, 0, new Uint8Array(8)));
  assert.deepEqual(emitted.on('v').map((payload) => payload.tag), ['same']);
});

test('black over white is refused', () => {
  assert.throws(() => open('{"black":200,"white":100}'), /`black` under `white`/);
  assert.throws(() => open('{"black":-1}'));
});
