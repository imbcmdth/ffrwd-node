import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Bound, BoundStream, defineNode, Input, Output, Rational, Runner, schemaOf, Shape } from '@ffrwd/node';
import { Harness } from '@ffrwd/node/mock';

const r = (num, den) => new Rational(num, den);
const picture = () => BoundStream.video('v', 0, 2, 2, 'rgba', r(1, 15));
const pixels = () => new Uint8Array(16);

const folding = defineNode({
  name: 'folding',
  version: '0.0.0',
  paramsSchema: '{"type":"object","properties":{"label":{"type":"string","default":"a"}},"additionalProperties":false}',
  shape: () =>
    new Shape()
      .input(Input.video('v').clock())
      .input(Input.rows('notes').interval().state())
      .output(Output.rows('seen')),
  init(_, init) {
    const v = init.stream('v').id;
    const folded = [];
    return {
      folded,
      fold(row) {
        folded.push([row.port, row.pts, JSON.stringify(row.row().note)]);
      },
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame === undefined) throw new Error('no frame');
        out.row('seen', frame.pts, folded.length);
      },
    };
  },
});

function harness() {
  const tb = r(1, 10);
  return new Harness(folding, '', [BoundStream.video('v', 0, 2, 2, 'rgba', tb), BoundStream.rows('notes', 1, tb)]);
}

test('earlier rows fold before the tick\'s own', () => {
  const node = harness();
  const tick = node
    .tick(5)
    .frame(0, 5, pixels())
    .earlier(1, 1, ['{"note":1}', '{"note":2}'])
    .earlier(1, 3, ['{"note":3}'])
    .message(1, 5, '{"note":4}');
  const emitted = node.process(tick);
  assert.deepEqual(
    node.node().folded.map(([, pts, note]) => [pts, note]),
    [
      [1, '1'],
      [1, '2'],
      [3, '3'],
      [5, '4'],
    ],
  );
  assert.deepEqual(emitted.messages('seen'), [[5, '4']]);
});

test('params in force are taken and others refused', () => {
  const node = harness();
  node.setParams('{"label":"a"}');
  node.setParams('');
  assert.throws(() => node.setParams('{"label":"b"}'), /cannot change its params/);
});

test('describe is the definition', () => {
  const meta = Runner.describe(folding);
  assert.deepEqual([meta.name, meta.version], ['folding', '0.0.0']);
  assert.ok(meta.paramsSchema.includes('label') && meta.rowsSchema === '');
});

test('a shape is resolved for the host', () => {
  assert.equal(Runner.shape(folding, '', ['v']).clockInput(), 'v');
  assert.throws(() => Runner.shape(folding, '{"label":3}', []), /`label` is string/);
});

const EVERY =
  '{"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":3}},"additionalProperties":false}';

const counter = defineNode({
  name: 'counter',
  version: '0.0.0',
  paramsSchema: EVERY,
  shape: () =>
    new Shape()
      .input(Input.video('v').clock())
      .output(Output.rows('ids').schema({ id: 'integer' }))
      .pure(),
  init({ every }, init) {
    const v = init.stream('v').id;
    return {
      process(tick, out) {
        const frame = tick.frame(v);
        if (frame !== undefined) out.row('ids', frame.pts, { id: Math.floor(tick.ordinal() / every) });
      },
    };
  },
});

test('ordinals number a split run as one', () => {
  const whole = new Harness(counter, '', [picture()]);
  const one = [];
  for (let pts = 0; pts < 9; pts++) one.push(...whole.process(whole.tick(pts).frame(0, pts, pixels())).messages('ids'));
  const workers = [new Harness(counter, '', [picture()]), new Harness(counter, '', [picture()])];
  const split = [];
  for (let pts = 0; pts < 9; pts++) {
    const worker = workers[pts % 2];
    split.push(...worker.process(worker.tick(pts).ordinal(pts).frame(0, pts, pixels())).messages('ids'));
  }
  assert.deepEqual(split, one);
  assert.equal(one[2][1], '{"id":0}');
  assert.equal(one[3][1], '{"id":1}');
});

test('a harness numbers its ticks from 0', () => {
  const node = new Harness(counter, '{"every":1}', [picture()]);
  for (const [pts, expected] of [
    [0, 0],
    [1, 1],
    [5, 2],
  ]) {
    assert.equal(node.process(node.tick(pts).frame(0, pts, pixels())).messages('ids')[0][1], `{"id":${expected}}`);
  }
  node.process(node.tick(9).ordinal(40).frame(0, 9, pixels()));
  assert.equal(node.process(node.tick(10).frame(0, 10, pixels())).messages('ids')[0][1], '{"id":41}');
});

const mask = defineNode({
  name: 'mask',
  version: '0.0.0',
  paramsSchema:
    '{"type":"object","properties":{"fetch":{"type":"boolean"},"pass":{"type":"boolean"}},"additionalProperties":false}',
  shape: () =>
    new Shape()
      .input(Input.video('v').clock().timing())
      .output(Output.video('mask').pixelFormat('gray'))
      .output(Output.like('v'))
      .pure(),
  init(params, init) {
    const v = init.stream('v');
    const { width, height } = v.videoFormat();
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        if (params.fetch) tick.fetch(v.id, frame.index);
        if (params.pass) out.pass('v', v.id, frame);
        out.frame('mask', frame.pts, frame.duration, new Uint8Array(width * height).fill(255));
      },
    };
  },
});

test('a timing input hands times and size and no bytes', () => {
  const plain = new Harness(mask, '', [picture()]);
  assert.equal(plain.shape().inputs[0].accepts.wants, 'timing');
  assert.equal(plain.process(plain.tick(4).frame(0, 4, new Uint8Array(0))).on('mask').length, 1);
  for (const params of ['{"fetch":true}', '{"pass":true}']) {
    const node = new Harness(mask, params, [picture()]);
    assert.throws(
      () => node.process(node.tick(0).frame(0, 0, new Uint8Array(0))),
      (error) => error.message.includes('`v`') && error.message.includes('timing alone'),
    );
  }
});

const SECONDS =
  '{"type":"object","properties":{"seconds":{"type":"number","exclusiveMinimum":0,"default":2}},"additionalProperties":false}';
const idle = () => ({ process() {} });

const hear = defineNode({
  name: 'hear',
  version: '0.0.0',
  paramsSchema: SECONDS,
  shape({ seconds }, bound) {
    const rate = bound.rateOf('a');
    if (rate === undefined) throw new Error('`a` is bound at a rate the compiler cannot know');
    const window = rate.count(seconds);
    return new Shape().input(Input.audio('a').clock().window(window, window)).output(Output.rows('heard'));
  },
  init: idle,
});

const clips = defineNode({
  name: 'clips',
  version: '0.0.0',
  paramsSchema: SECONDS,
  shape({ seconds }, bound) {
    const frame = bound.rateOf('v')?.duration(1) ?? 0.5;
    return new Shape().input(Input.video('v').clock()).output(Output.rows('clips').latency(seconds + frame));
  },
  init: idle,
});

test('a window in seconds is counted at the bound rate', () => {
  assert.equal(Runner.shape(hear, '', new Bound(['a']).rate('a', r(48000, 1))).inputs[0].window, 96000);
  assert.throws(() => Runner.shape(hear, '', ['a']), /`a`/);
  const opened = new Harness(hear, '{"seconds":30}', [BoundStream.audio('a', 0, 16000, 1, 'f32')]);
  assert.equal(opened.shape().inputs[0].window, 480000);
});

test('a latency a frame past a cap is exact where the rate is known', () => {
  const latency = (bound) => Runner.shape(clips, '{"seconds":10}', bound).outputs[0].latency;
  assert.equal(latency(new Bound(['v']).rate('v', r(2, 1))), 10.5);
  assert.equal(latency(new Bound(['v']).rate('v', r(25, 1))), 10 + 1 / 25);
  assert.equal(latency(new Bound(['v'])), 10.5);
});

test('init shapes the node as the plan did', () => {
  const at25 = [picture().rate(r(25, 1))];
  const planned = Runner.shape(clips, '{"seconds":10}', Bound.of(at25));
  const opened = new Harness(clips, '{"seconds":10}', at25);
  assert.deepEqual(opened.shape(), planned);
  assert.equal(opened.shape().outputs[0].latency, 10 + 1 / 25);
});

const tile = defineNode({
  name: 'tile',
  version: '0.0.0',
  shape(_, bound) {
    const cells = Math.max(1, bound.count('v'));
    return new Shape()
      .input(Input.video('v').many().hold())
      .rate(bound.rateOf('v') ?? r(30, 1))
      .output(Output.video('grid').size(64 * cells, 48).pixelFormat('rgba'));
  },
  init: idle,
});

test('a many port says how many streams it takes', () => {
  const shape = Runner.shape(tile, '', new Bound().bind('v', [r(25, 1), undefined, r(30, 1)]));
  assert.equal(shape.outputs[0].format.val.width, 192);
  assert.deepEqual(shape.clock, { tag: 'rate', val: r(25, 1) });
});

const present = defineNode({
  name: 'present',
  version: '0.0.0',
  shape: () =>
    new Shape()
      .input(Input.video('v').clock())
      .input(Input.video('feed').optional().hold().lead(0.5))
      .output(Output.rows('presence').schema(schemaOf({ coming: 'integer', ended: 'integer' })))
      .pure(),
  init(_, init) {
    const feed = init.optional('feed')?.id;
    return {
      process(tick, out) {
        if (feed === undefined) return;
        for (const ended of tick.endedFeeds(feed)) {
          out.row('presence', tick.pts(), { coming: ended.start.known, ended: ended.ends });
        }
      },
    };
  },
});

test('ended feeds say when a start was known and when it went', () => {
  const feed = BoundStream.video('feed', 1, 2, 2, 'rgba', r(1, 15));
  const node = new Harness(present, '', [picture(), feed]);
  const gone = { start: { tags: [], firstPts: 0, at: 20, known: 12 }, ends: 31 };
  assert.deepEqual(node.process(node.tick(30).frame(0, 30, new Uint8Array(0))).messages('presence'), []);
  const after = node.tick(33).frame(0, 33, new Uint8Array(0)).ended(1, gone);
  assert.deepEqual(node.process(after).messages('presence'), [[33, '{"coming":12,"ended":31}']]);
});

test('the export speaks the host\'s records', () => {
  const meta = counter.describe();
  assert.equal(meta.name, 'counter');
  assert.ok(meta.sampleRates instanceof Uint32Array);
  const shape = counter.shape('', [{ input: 'v', streams: [{ rate: { num: 30, den: 1 } }] }]);
  assert.deepEqual(shape.clock, { tag: 'input', val: 'v' });
  assert.ok(shape.inputs[0].accepts.sampleRates instanceof Uint32Array);
  assert.deepEqual(shape.outputs[0].format, { tag: 'data', val: 'json' });
  assert.throws(() => counter.shape('{"every":0}', []), (thrown) => typeof thrown === 'string' && /at least 1/.test(thrown));
  assert.throws(() => counter.process({}), (thrown) => thrown === 'counter was called before init');

  counter.init(
    [
      {
        port: 'v',
        id: 7,
        info: { index: 0, kind: 'video', codec: 'rawvideo', duration: undefined, tags: [], timeBase: { num: 1, den: 30 } },
        format: { tag: 'video', val: { width: 2, height: 2, pixFmt: 'rgba', color: undefined } },
        rendition: { bandwidth: 5n },
        row: undefined,
        decodeDelay: 0,
        latency: undefined,
        hint: { rate: { num: 30, den: 1 } },
      },
    ],
    ['ids'],
    '{"every":2}',
  );
  const tick = {
    pts: () => 4n,
    ordinal: () => 4n,
    timeBase: () => ({ num: 1, den: 30 }),
    last: () => false,
    streams: () => [7],
    info: () => ({ index: 0, kind: 'video', codec: 'rawvideo', tags: [], timeBase: { num: 1, den: 30 } }),
    frames: () => [{ pts: 4n, index: 0, duration: 1n, rows: [] }],
    messages: () => [],
    earlierRows: () => [],
  };
  const emitted = counter.process(tick);
  assert.deepEqual(emitted.items[0].port, 'ids');
  assert.equal(emitted.items[0].payload.tag, 'message');
  assert.equal(emitted.items[0].payload.val.pts, 4n);
  assert.equal(new TextDecoder().decode(emitted.items[0].payload.val.data), '{"id":2}');
  assert.deepEqual([emitted.rows, emitted.finished], [[], false]);
});
