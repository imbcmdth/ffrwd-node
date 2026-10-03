import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Bound, Cue, Input, Out, Output, Rational, Shape } from '@ffrwd/node';

function out() {
  const shape = new Shape()
    .input(Input.video('v').clock())
    .input(Input.packets('coded'))
    .output(Output.like('v'))
    .output(Output.rows('spots').timeBase(new Rational(1, 1000)))
    .output(Output.packets('p').following('coded'))
    .resolve(new Bound(['v', 'coded']));
  const made = new Out(shape);
  made.begin(new Rational(1, 15));
  return made;
}

test('a port never goes back', () => {
  const made = out();
  made.same('v', 2, 1, 0, 0);
  made.same('v', 2, 1, 0, 0);
  assert.throws(() => made.same('v', 1, 1, 0, 0), /from pts 2 to 1/);
  made.take();
  assert.throws(() => made.frame('v', 1, undefined, new Uint8Array(0)));
  made.row('spots', 0, 1);
  assert.equal(made.last('spots'), 0);
});

test('ports and kinds are checked', () => {
  const made = out();
  assert.throws(() => made.row('nowhere', 0, 1), /not an output/);
  assert.throws(() => made.row('v', 0, 1), /video output/);
  assert.throws(() => made.frame('spots', 0, undefined, new Uint8Array(0)));
});

test('packets keep decode order', () => {
  const made = out();
  const packet = (pts, dts) => ({ pts, dts, duration: undefined, keyframe: false, data: new Uint8Array(0) });
  made.packet('p', packet(3, undefined));
  made.packet('p', packet(3, 0));
  made.packet('p', packet(1, 1));
  assert.throws(() => made.packet('p', packet(2, 0)), /decode order/);
});

test('seconds land in the port time base', () => {
  const made = out();
  assert.equal(made.pts('v', 2), 30);
  assert.equal(made.pts('spots', 2), 2000);
  made.cue('spots', new Cue(1.5, 2, 'hi'));
  assert.deepEqual(made.take().messages('spots'), [[1500, '{"start_t":1.5,"end_t":2,"text":"hi"}']]);
});
