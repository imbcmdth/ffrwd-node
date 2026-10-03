import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Rational } from '@ffrwd/node';

const r = (num, den) => new Rational(num, den);

test('seconds and back', () => {
  const tb = r(1, 15360);
  assert.equal(tb.seconds(15360), 1);
  assert.equal(tb.pts(1), 15360);
  assert.equal(tb.pts(tb.seconds(1024)), 1024);
  const ntsc = r(1001, 30000);
  assert.equal(ntsc.pts(ntsc.seconds(12345)), 12345);
});

test('rescale rounds to nearest, away from zero', () => {
  assert.equal(r(1, 15360).rescale(15360, r(1, 48000)), 48000);
  assert.equal(r(1, 15360).rescale(1024, r(1, 15)), 1);
  assert.equal(r(1, 2).rescale(1, r(1, 1)), 1);
  assert.equal(r(1, 2).rescale(-1, r(1, 1)), -1);
  assert.equal(r(1, 3).rescale(1, r(1, 1)), 0);
  assert.equal(r(1001, 30000).rescale(30, Rational.MICROS), 1_001_000);
});

test('a rate is a time base inverted', () => {
  assert.deepEqual(r(30, 1).inverse(), r(1, 30));
  assert.equal(r(30000, 1001).inverse().seconds(30), 1.001);
});

test('seconds count frames and samples at a rate', () => {
  assert.equal(r(48000, 1).count(2), 96000);
  assert.equal(r(16000, 1).count(30), 480000);
  assert.equal(r(30000, 1001).count(1), 30);
  assert.equal(r(30, 1).count(0.1), 3);
  assert.equal(r(25, 1).count(0), 0);
  assert.equal(r(30000, 1001).duration(1), 1001 / 30000);
  assert.equal(r(2, 1).duration(1), 0.5);
  const ntsc = r(30000, 1001);
  assert.equal(ntsc.count(ntsc.duration(300)), 300);
});

test('approximate finds the fraction a param meant', () => {
  assert.deepEqual(Rational.approximate(30, 1001), r(30, 1));
  assert.deepEqual(Rational.approximate(29.97, 1001), r(2997, 100));
  assert.deepEqual(Rational.approximate(0.5, 1001), r(1, 2));
  assert.deepEqual(Rational.approximate(30000 / 1001, 1001), r(30000, 1001));
  assert.deepEqual(Rational.approximate(-2.5, 10), r(-5, 2));
  assert.deepEqual(Rational.approximate(Math.PI, 1000), r(355, 113));
});
