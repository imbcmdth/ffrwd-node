import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Cue, Cues, parse, schemaOf, SPAN, Spans } from '@ffrwd/node';

test('a schema from a row description', () => {
  const schema = JSON.parse(
    schemaOf({ ...SPAN, x: 'integer', label: 'string', hidden: null, vector: ['number'], at: { t: 'number' } }),
  );
  const { properties, required } = schema;
  assert.equal(properties.start_t.type, 'number');
  assert.equal(properties.id.type, 'integer');
  assert.equal(properties.x.type, 'integer');
  assert.equal(properties.label.type, 'string');
  assert.deepEqual(properties.hidden, {});
  assert.deepEqual(properties.vector, { type: 'array', items: { type: 'number' } });
  assert.deepEqual(properties.at.required, ['t']);
  assert.ok(required.includes('start_t') && !required.includes('hidden'));
});

test('a schema is written as the Rust SDK writes it, keys sorted', () => {
  assert.equal(
    schemaOf({ ...SPAN, x: 'integer', y: 'integer', w: 'integer', h: 'integer' }),
    '{"properties":{"h":{"type":"integer"},"id":{"type":"integer"},"start_t":{"type":"number"},' +
      '"w":{"type":"integer"},"x":{"type":"integer"},"y":{"type":"integer"}},' +
      '"required":["h","id","start_t","w","x","y"],"type":"object"}',
  );
  assert.equal(
    schemaOf({ vector: ['number'], hidden: null, at: { t: 'number' } }),
    '{"properties":{"at":{"properties":{"t":{"type":"number"}},"required":["t"],"type":"object"},' +
      '"hidden":{},"vector":{"items":{"type":"number"},"type":"array"}},' +
      '"required":["at","vector"],"type":"object"}',
  );
});

test('rows parse and say which did not', () => {
  assert.deepEqual(parse('{"x":3}'), { x: 3 });
  assert.throws(() => parse('{"start_t":soon}'), /soon/);
});

test('a span runs while its key is seen', () => {
  const spans = new Spans();
  const starts = [];
  [true, true, false, true, true].forEach((seen, n) => {
    spans.tick(n);
    if (seen) starts.push(spans.see('mark').start_t);
  });
  assert.deepEqual(starts, [0, 0, 3, 3]);
});

test('a gap keeps the span', () => {
  const spans = new Spans().gap(2);
  const starts = [];
  [true, false, false, true, false, false, false, true].forEach((seen, n) => {
    spans.tick(n * 0.5);
    if (seen) starts.push(spans.see('one').start_t);
  });
  assert.deepEqual(starts, [0, 0, 3.5]);
});

test('the longest span splits', () => {
  const spans = new Spans().longest(3);
  const seen = [];
  for (let n = 0; n < 7; n++) {
    spans.tick(n);
    const span = spans.see('one');
    seen.push([span.start_t, span.id, span.age]);
  }
  assert.deepEqual(seen, [
    [0, 0, 0],
    [0, 0, 1],
    [0, 0, 2],
    [3, 1, 0],
    [3, 1, 1],
    [3, 1, 2],
    [6, 2, 0],
  ]);
});

test('keys have spans of their own', () => {
  const spans = new Spans();
  spans.tick(0);
  assert.equal(spans.see('a').start_t, 0);
  spans.tick(1);
  assert.equal(spans.see('a').start_t, 0);
  assert.equal(spans.see('b').start_t, 1);
  assert.equal(spans.open(), 2);
  spans.tick(2);
  assert.equal(spans.open(), 2);
  spans.see('b');
  spans.tick(3);
  assert.equal(spans.open(), 1);
});

test('spans starting on one tick write one start_t and their own ids', () => {
  const spans = new Spans();
  spans.tick(2.5);
  const rows = ['a', 'b'].map((key) => JSON.stringify({ ...spans.see(key), label: key }));
  assert.deepEqual(rows, ['{"start_t":2.5,"id":0,"label":"a"}', '{"start_t":2.5,"id":1,"label":"b"}']);
});

test('cues show while they cover the time', () => {
  const cues = new Cues();
  cues.add(new Cue(2, 4, 'second'));
  cues.add(new Cue(0, 3, 'first'));
  const at = (t) => cues.at(t).map((cue) => cue.text);
  assert.deepEqual(at(2.5), ['first', 'second']);
  assert.deepEqual(at(3), ['second']);
  assert.deepEqual(at(4), []);
  cues.dropEnded(3);
  assert.equal(cues.size, 1);
});
