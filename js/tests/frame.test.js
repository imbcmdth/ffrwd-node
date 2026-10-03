import assert from 'node:assert/strict';
import { test } from 'node:test';

import { Filter, IMAGENET, planes, Rect, Rgba, tensor, tensors } from '@ffrwd/node/frame';

const EIGHT_BITS = { mean: [0, 0, 0], std: [1 / 255, 1 / 255, 1 / 255] };

/** ffrwd-frame's own test pictures: three sawtooth ramps with a flat block
 * over the lower right, alpha varying. */
function edge(width, height) {
  const pixels = new Uint8Array(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      let rgb = [(x * 7 + y * 11) % 256, (x * 5 + y * 3) % 256, (x * 2 + y * 13) % 256];
      if (3 * x >= width && 3 * y >= height) rgb = [250, 8, 130];
      pixels.set([...rgb, 255 - ((x + y) % 256)], (y * width + x) * 4);
    }
  }
  return pixels;
}

/** And noise: splitmix64 from 0x5EED, one draw a pixel, its low four bytes. */
function noise(width, height) {
  const pixels = new Uint8Array(width * height * 4);
  const mask = (1n << 64n) - 1n;
  let state = 0x5eedn;
  for (let at = 0; at < pixels.length; at += 4) {
    state = (state + 0x9e3779b97f4a7c15n) & mask;
    let z = state;
    z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & mask;
    z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & mask;
    z ^= z >> 31n;
    for (let n = 0; n < 4; n += 1) pixels[at + n] = Number((z >> BigInt(8 * n)) & 255n);
  }
  return pixels;
}

function digest(bytes) {
  let value = 0xcbf29ce484222325n;
  for (const byte of bytes) value = BigInt.asUintN(64, (value ^ BigInt(byte)) * 0x100000001b3n);
  return `0x${value.toString(16).padStart(16, '0')}`;
}

/** Planes of plain eight-bit values back to interleaved bytes. */
function eight(floats, width, height) {
  const plane = width * height;
  const out = new Uint8Array(plane * 3);
  for (let at = 0; at < plane; at += 1) {
    for (let c = 0; c < 3; c += 1) out[at * 3 + c] = Math.min(Math.max(Math.round(floats[c * plane + at]), 0), 255);
  }
  return out;
}

const picture = { edge, noise };

test('a frame is four bytes a pixel and nothing else', () => {
  const frame = new Rgba(new Uint8Array(8 * 5 * 4), 8, 5);
  assert.deepEqual([frame.width, frame.height], [8, 5]);
  assert.throws(() => new Rgba(new Uint8Array(8 * 5 * 4 - 1), 8, 5));
  assert.throws(() => new Rgba(new Uint8Array(8 * 5 * 4 + 4), 8, 5), {
    message: 'an rgba frame of 8x5 is 160 bytes, not 164',
  });
  assert.throws(() => new Rgba(new Uint8Array(8 * 5 * 3), 8, 5));
});

test('a box grows a tenth of itself on every side, floored and clamped to the frame', () => {
  const rect = Rect.padded(300, 100, 100, 200, 0.1, 1280, 720);
  assert.deepEqual([rect.x0, rect.y0, rect.x1, rect.y1], [290, 80, 410, 320]);
  assert.deepEqual([rect.width(), rect.height()], [120, 240]);
  assert.deepEqual(Rect.padded(14, 14, 33, 33, 0.1, 640, 480), new Rect(10, 10, 50, 50));
  assert.deepEqual(Rect.padded(5, 4, 100, 200, 0.1, 1280, 720), new Rect(0, 0, 115, 224));
  assert.deepEqual(Rect.padded(1200, 600, 100, 200, 0.1, 1280, 720), new Rect(1190, 580, 1280, 720));
  assert.deepEqual(Rect.padded(-50, -50, 400, 400, 0.1, 320, 240), Rect.whole(320, 240));
});

test('a box with nothing on the frame is no rect at all', () => {
  assert.equal(Rect.padded(2000, 100, 50, 50, 0.1, 1280, 720), undefined);
  assert.equal(Rect.padded(-500, 100, 50, 50, 0.1, 1280, 720), undefined);
  assert.equal(Rect.padded(100, 2000, 50, 50, 0.1, 1280, 720), undefined);
  assert.equal(Rect.padded(100, 100, 0, 50, 0.1, 1280, 720), undefined);
  assert.equal(Rect.padded(100, 100, 50, 0, 0.1, 1280, 720), undefined);
});

test("the resize is ffrwd-frame's to the byte", () => {
  // What ffrwd-frame 0.1.1's `planes` answers for each, as eight-bit bytes.
  const cases = [
    ['edge', 9, 7, null, 32, 24, '0x2b81603c6e9b7af2'],
    ['noise', 9, 7, null, 32, 24, '0x89c99096ab2298ac'],
    ['edge', 500, 380, null, 224, 224, '0x3f0ef4e1e3120a7d'],
    ['noise', 500, 380, null, 224, 224, '0xb6ef2710b0b018d8'],
    ['edge', 9, 7, null, 9, 7, '0x67bff18552e376de'],
    ['noise', 320, 240, new Rect(80, 60, 187, 140), 320, 240, '0x817c929422ae445b'],
    ['noise', 320, 240, null, 160, 240, '0xe1ec2af94805ba44'],
    ['edge', 320, 240, null, 320, 80, '0xee245d22a607658f'],
    ['edge', 64, 48, null, 200, 20, '0x10d6a057a703d7db'],
    ['noise', 40, 30, new Rect(30, 20, 99, 99), 16, 16, '0xc4e1a45b3de0dbc7'],
  ];
  for (const [kind, sw, sh, rect, w, h, want] of cases) {
    const frame = new Rgba(picture[kind](sw, sh), sw, sh);
    const got = planes(frame, rect ?? Rect.whole(sw, sh), w, h, Filter.Bilinear, EIGHT_BITS);
    assert.equal(digest(eight(got, w, h)), want, `${kind} ${sw}x${sh} into ${w}x${h}`);
  }
});

test("the tensor is ffrwd-frame's to the bit", () => {
  const frame = new Rgba(noise(97, 61), 97, 61);
  const padded = Rect.padded(20, 10, 30, 25, 0.1, 97, 61);
  const one = tensor(frame, padded, 24, 24, Filter.Bilinear, IMAGENET);
  assert.equal(one.length, 3 * 24 * 24 * 4);
  assert.equal(digest(one), '0x5411c2aba83d4522');
  const two = tensors(frame, [Rect.whole(97, 61), padded], 16, 12, Filter.Bilinear, IMAGENET);
  assert.equal(digest(two), '0xf5a3beedbb307eed');
  assert.deepEqual(tensors(frame, [], 16, 12, Filter.Bilinear, IMAGENET), new Uint8Array(0));
});

test('a crop with no pixels is black', () => {
  const frame = new Rgba(edge(9, 7), 9, 7);
  const out = planes(frame, new Rect(5, 5, 5, 7), 4, 4, Filter.Bilinear, EIGHT_BITS);
  assert.ok(out.every((value) => value === 0));
});

test('the alpha byte reaches nothing', () => {
  const opaque = edge(24, 24);
  const varying = opaque.map((value, at) => (at % 4 === 3 ? at % 256 : value));
  const rect = Rect.whole(24, 24);
  assert.deepEqual(
    tensor(new Rgba(opaque, 24, 24), rect, 10, 10, Filter.Bilinear, IMAGENET),
    tensor(new Rgba(varying, 24, 24), rect, 10, 10, Filter.Bilinear, IMAGENET),
  );
});
