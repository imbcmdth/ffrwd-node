import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Anchor, Bound, BoundStream, Input, Output, Rational, Shape } from '@ffrwd/node';

const r = (num, den) => new Rational(num, den);

const filter = () => new Shape().input(Input.video('v').clock().pixelFormats(['rgba'])).output(Output.like('v'));

function refused(shape, names, ...said) {
  assert.throws(
    () => shape.resolve(new Bound(names)),
    (error) => said.every((part) => error.message.includes(part)),
  );
}

test('the clock comes from the input marked', () => {
  const shape = filter().resolve(new Bound(['v']));
  assert.deepEqual(shape.clock, { tag: 'input', val: 'v' });
  assert.equal(shape.outputs[0].kind, 'video');
  assert.equal(shape.outputs[0].like.port, 'v');
});

test('a pixel format alone follows the clock', () => {
  const shape = new Shape()
    .input(Input.video('v').clock())
    .output(Output.video('mask').pixelFormat('gray'))
    .resolve(new Bound(['v']));
  assert.equal(shape.outputs[0].like.port, 'v');
  assert.equal(shape.outputs[0].like.pixelFormat, 'gray');
});

test('an output following an unbound input is left out', () => {
  const shape = new Shape()
    .input(Input.video('v').clock())
    .input(Input.audio('a').optional())
    .output(Output.like('v'))
    .output(Output.like('a'))
    .resolve(new Bound(['v']));
  assert.deepEqual(
    shape.outputs.map((output) => output.name),
    ['v'],
  );
});

test('refusals name the port', () => {
  refused(new Shape().input(Input.video('v')), ['v'], 'no clock');
  refused(new Shape().input(Input.video('v').clock().optional()), ['v'], '`v`');
  refused(filter().input(Input.rows('boxes').hold()), ['v', 'boxes'], '`boxes`');
  refused(filter().input(Input.video('w').interval()), ['v', 'w'], '`w`');
  refused(filter().input(Input.rows('boxes').ignoreRows()), ['v', 'boxes'], '`boxes`');
  refused(new Shape().input(Input.audio('a').clock().window(4, 5)), ['a'], 'stride');
  refused(
    new Shape().input(Input.video('v').many().hold()).rateOf('v').output(Output.video('out').following('v')),
    ['v'],
    'many',
  );
  refused(
    new Shape()
      .rate(r(30, 1))
      .input(Input.video('v'))
      .output(Output.video('out').size(64, 64).pixelFormat('rgba')),
    ['v'],
    'lockstep',
  );
});

test('a generator gives its own format', () => {
  const ticker = () =>
    new Shape()
      .rate(r(30, 1))
      .output(Output.video('video').size(1280, 720).pixelFormat('rgba').row(0))
      .relationRow('{}')
      .bounded(false);
  const shape = ticker().resolve(new Bound());
  assert.deepEqual(shape.outputs[0].format, {
    tag: 'video',
    val: { width: 1280, height: 720, pixFmt: 'rgba', color: undefined },
  });
  refused(new Shape().rate(r(30, 1)).output(Output.video('video')), [], 'give it a format');
  refused(new Shape().rate(r(30, 1)).output(Output.video('video').size(8, 8)), [], 'no pixel format');
});

test('hold and interval fields chain', () => {
  const feed = Input.video('feed').optional().hold().lead(0.5).portParam('port');
  assert.equal(feed.spec.pairing.tag, 'hold');
  assert.deepEqual(feed.spec.pairing.val.anchor, Anchor.firstFrame);
  assert.equal(feed.spec.pairing.val.lead, 0.5);
  assert.equal(feed.spec.pairing.val.portParam, 'port');

  const words = Input.rows('words').latency(2).ahead(0.5).state();
  assert.deepEqual(words.spec.pairing, {
    tag: 'interval',
    val: { latency: 2, ahead: 0.5, anchor: Anchor.sharedClock, group: undefined },
  });
  assert.equal(words.spec.rows, 'state');
});

test('a timing input is a frame kind', () => {
  const mask = new Shape().input(Input.video('v').clock().timing()).output(Output.video('mask').pixelFormat('gray'));
  assert.equal(mask.resolve(new Bound(['v'])).inputs[0].accepts.wants, 'timing');
  const sound = new Shape().input(Input.audio('a').clock()).input(Input.audio('b').timing().like('a'));
  sound.resolve(new Bound(['a', 'b']));
  refused(filter().input(Input.rows('boxes').interval().timing()), ['v', 'boxes'], '`boxes`', 'timing');
});

test('a data input takes an anchor and a hold group', () => {
  const follow = Input.rows('d').anchor(Anchor.firstFrame);
  assert.deepEqual(follow.spec.pairing.val.anchor, Anchor.firstFrame);
  const feed = () => Input.video('feed').optional().group('ad');
  assert.equal(feed().spec.pairing.val.group, 'ad');
  const beside = () => Input.rows('cues').latency(1).group('ad');
  assert.equal(beside().spec.pairing.tag, 'interval');
  assert.equal(beside().spec.pairing.val.group, 'ad');

  filter().input(feed()).input(beside()).resolve(new Bound(['v', 'feed', 'cues']));
  refused(
    filter().input(Input.video('feed').optional().hold()).input(beside()),
    ['v', 'feed', 'cues'],
    '`cues`',
    '`ad`',
  );
  refused(
    filter().input(feed()).input(beside().anchor(Anchor.tagged('smart_timed'))),
    ['v', 'feed', 'cues'],
    '`cues`',
    'shared clock',
  );
});

test('a held frame input keeps its anchor and group', () => {
  const feed = Input.video('feed').anchor(Anchor.tagged('smart_timed')).group('ad');
  assert.equal(feed.spec.pairing.tag, 'hold');
  assert.deepEqual(feed.spec.pairing.val.anchor, Anchor.tagged('smart_timed'));
  assert.equal(feed.spec.pairing.val.group, 'ad');
});

test('bound says how many streams and at what rate', () => {
  const bound = new Bound(['v']).rate('v', r(30000, 1001)).bind('inputs', [r(25, 1), undefined]);
  assert.ok(bound.has('v') && bound.has('inputs') && !bound.has('a'));
  assert.deepEqual([bound.count('v'), bound.count('inputs'), bound.count('a')], [1, 2, 0]);
  assert.deepEqual(bound.rateOf('v'), r(30000, 1001));
  assert.deepEqual(bound.rateOf('inputs'), r(25, 1));
  assert.equal(bound.streams('inputs')[1].rate, undefined);
  assert.equal(bound.rateOf('a'), undefined);
  assert.deepEqual(
    bound.inputs().map((binding) => binding.input),
    ['v', 'inputs'],
  );
  assert.equal(new Bound().rate('a', r(48000, 1)).count('a'), 1);
});

test('bound streams carry the hints the shape was asked with', () => {
  const tb = r(1, 15360);
  const bound = Bound.of([
    BoundStream.video('v', 0, 2, 2, 'rgba', tb).rate(r(30000, 1001)),
    BoundStream.audio('a', 1, 48000, 2, 'f32'),
    BoundStream.video('inputs', 2, 2, 2, 'rgba', tb).rate(r(25, 1)),
    BoundStream.video('inputs', 3, 2, 2, 'rgba', tb),
    BoundStream.rows('d', 4, tb),
  ]);
  const asked = new Bound(['v', 'a'])
    .rate('v', r(30000, 1001))
    .rate('a', r(48000, 1))
    .bind('inputs', [r(25, 1), undefined])
    .bind('d', [undefined]);
  assert.deepEqual(bound, asked);
});
