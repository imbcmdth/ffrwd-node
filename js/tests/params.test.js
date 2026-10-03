import assert from 'node:assert/strict';
import { test } from 'node:test';

import { NO_PARAMS, readParams } from '@ffrwd/node';

const SCHEMA = JSON.stringify({
  type: 'object',
  properties: {
    every: { type: 'integer', minimum: 1, default: 30 },
    amount: { type: 'number', minimum: 0, maximum: 1, default: 0.5 },
    text: { type: 'string', minLength: 1 },
    mode: { enum: ['over', 'under'], default: 'over' },
    fps: { type: ['number', 'null'] },
  },
  required: ['text'],
  additionalProperties: false,
});

test('defaults fill what the call left out', () => {
  assert.deepEqual(readParams(SCHEMA, '{"text":"hi"}'), { text: 'hi', every: 30, amount: 0.5, mode: 'over' });
});

test('a whole float reads as an integer', () => {
  assert.equal(readParams(SCHEMA, '{"text":"hi","every":15.0}').every, 15);
  assert.throws(() => readParams(SCHEMA, '{"text":"hi","every":1.5}'), /`every` is integer/);
});

test('null is not set', () => {
  const params = readParams(SCHEMA, '{"text":"hi","fps":null,"every":null}');
  assert.equal(params.fps, undefined);
  assert.equal(params.every, 30);
});

test('refusals name the param', () => {
  const cases = [
    ['{"text":"hi","every":0}', '`every` is at least 1'],
    ['{"text":"hi","amount":2}', '`amount` is at most 1'],
    ['{"text":""}', '`text` is at least 1 character'],
    ['{"text":"hi","mode":"sideways"}', '`mode` is one of'],
    ['{"text":"hi","colour":"red"}', '`colour` is not a param here'],
    ['{}', '`text` is required'],
    ['[1]', 'a JSON object'],
    ['{"text":7}', '`text` is string'],
  ];
  for (const [params, said] of cases) {
    assert.throws(() => readParams(SCHEMA, params), (error) => error.message.includes(said), params);
  }
});

test('no params reads nothing', () => {
  assert.deepEqual(readParams(NO_PARAMS, ''), {});
  assert.deepEqual(readParams(NO_PARAMS, ' {} '), {});
  assert.throws(() => readParams(NO_PARAMS, '{"x":1}'), /the node takes none/);
});
