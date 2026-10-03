import assert from 'node:assert/strict';
import { test } from 'node:test';

import { BoundStream, Rational } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

import { node as dim } from '../examples/dim.js';

const open = (params) => new Harness(dim, params, [BoundStream.video('v', 0, 2, 1, 'rgba', new Rational(1, 30))]);

test('a quarter left of every colour', () => {
  const node = open('{"amount":0.75}');
  const emitted = node.process(node.tick(0).frame(0, 0, Uint8Array.of(200, 100, 0, 255, 8, 4, 2, 9)));
  const [payload] = emitted.on('v');
  assert.equal(payload.tag, 'frame');
  assert.deepEqual([...payload.val.data], [50, 25, 0, 255, 2, 1, 0, 9]);
});

test('nothing to dim passes the picture', () => {
  const node = open('{"amount":0}');
  const [payload] = node.process(node.tick(3).frame(0, 3, new Uint8Array(8))).on('v');
  assert.equal(payload.tag, 'same');
  assert.deepEqual([payload.val.pts, payload.val.index], [3, 0]);
});

test('the amount changes while it runs', () => {
  const node = open('');
  node.setParams('{"amount":1}');
  const [payload] = node.process(node.tick(0).frame(0, 0, new Uint8Array(8).fill(255))).on('v');
  assert.deepEqual([...payload.val.data], [0, 0, 0, 255, 0, 0, 0, 255]);
  assert.throws(() => node.setParams('{"amount":2}'), /at most 1/);
});
