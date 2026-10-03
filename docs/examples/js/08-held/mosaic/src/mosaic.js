import { Anchor, defineNode, Input, Output, Shape } from '@ffrwd/node';

const DOWN = [48, 48, 48, 255];

// Pillow's bilinear resize, written out here: it works in the same fixed
// point, pass for pass, as the ffrwd-frame crate the Rust example calls, so
// both modules make the same picture to the byte.

/** For each of `size` pixels made from `from`: the first pixel it reads, and
 * its weights in fixed point with `bits` fractional bits. */
function taps(from, size) {
  const scale = from / size;
  const stretch = Math.max(scale, 1);
  const kernels = [];
  for (let at = 0; at < size; at += 1) {
    const centre = (at + 0.5) * scale;
    let first = Math.max(Math.floor(centre - stretch), 0);
    const end = Math.min(Math.ceil(centre + stretch), from);
    const weights = [];
    for (let x = first; x < end; x += 1) {
      const weight = Math.max(0, 1 - Math.abs((x - (centre - 0.5)) * (1 / stretch)));
      if (weight === 0 && weights.length === 0) first += 1;
      else weights.push(weight);
    }
    const sum = weights.reduce((total, weight) => total + weight, 0);
    while (weights.at(-1) === 0) weights.pop();
    kernels.push({ first, weights: weights.map((weight) => (sum === 0 ? weight : weight / sum)) });
  }
  const most = Math.max(0, ...kernels.flatMap((kernel) => kernel.weights));
  let bits = 0;
  while (bits < 21 && Math.round(most * 2 ** (bits + 1)) < 2 ** 15) bits += 1;
  for (const kernel of kernels) kernel.weights = kernel.weights.map((weight) => Math.round(weight * 2 ** bits));
  return { kernels, bits };
}

/** `size` pixels across (or down) made from each row (or column) of an rgb
 * picture `width` x `height`, rounded back to eight bits. */
function pass(rgb, width, height, size, across) {
  const { kernels, bits } = taps(across ? width : height, size);
  const [w, h] = across ? [size, height] : [width, size];
  const out = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const { first, weights } = kernels[across ? x : y];
      for (let channel = 0; channel < 3; channel += 1) {
        let sum = 1 << (bits - 1);
        for (let n = 0; n < weights.length; n += 1) {
          const at = across ? (y * width + first + n) * 3 : ((first + n) * width + x) * 3;
          sum += rgb[at + channel] * weights[n];
        }
        out[(y * w + x) * 3 + channel] = Math.min(Math.max(sum >> bits, 0), 255);
      }
    }
  }
  return out;
}

/** `rect` of an rgba picture `stride` pixels wide, resized to `width` x
 * `height`: across first, then down, as Pillow does. Red, green and blue. */
function resize(pixels, stride, rect, width, height) {
  let [w, h] = [rect.x1 - rect.x0, rect.y1 - rect.y0];
  let rgb = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [((rect.y0 + y) * stride + rect.x0 + x) * 4, (y * w + x) * 3];
      for (let channel = 0; channel < 3; channel += 1) rgb[to + channel] = pixels[from + channel];
    }
  }
  if (w !== width) [rgb, w] = [pass(rgb, w, h, width, true), width];
  if (h !== height) [rgb, h] = [pass(rgb, w, h, height, false), height];
  return rgb;
}

/** Every pixel of `cell` of a `width`-wide canvas set to `colour`. */
function fill(canvas, width, cell, colour) {
  const row = new Uint8Array((cell.x1 - cell.x0) * 4);
  for (let at = 0; at < row.length; at += 4) row.set(colour, at);
  for (let y = cell.y0; y < cell.y1; y += 1) canvas.set(row, (y * width + cell.x0) * 4);
}

/** `pixels`, a `tile`'s picture, resized into its cell of a `width`-wide
 * canvas. */
function put(canvas, width, tile, pixels) {
  const { cell } = tile;
  const [w, h] = [cell.x1 - cell.x0, cell.y1 - cell.y0];
  const whole = { x0: 0, y0: 0, x1: tile.width, y1: tile.height };
  const rgb = resize(pixels, tile.width, whole, w, h);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [(y * w + x) * 3, ((cell.y0 + y) * width + cell.x0 + x) * 4];
      for (let channel = 0; channel < 3; channel += 1) canvas[to + channel] = rgb[from + channel];
    }
  }
}

export const node = defineNode({
  name: 'mosaic',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"columns":{"type":"integer","minimum":1,"default":2},' +
    '"width":{"type":"integer","minimum":16,"default":1280},"height":{"type":"integer","minimum":16,"default":720}},' +
    '"additionalProperties":false}',

  shape({ width, height }) {
    return new Shape()
      .input(Input.video('v').many().hold().anchor(Anchor.sharedClock).pixelFormats(['rgba']))
      .output(Output.video('v').size(width, height).pixelFormat('rgba'))
      .rateOf('v')
      .pure();
  },

  init({ columns, width, height }, init) {
    const streams = init.streams('v');
    columns = Math.max(Math.min(columns, streams.length), 1);
    const rows = Math.max(Math.ceil(streams.length / columns), 1);
    const tiles = streams.map((stream, n) => {
      const video = stream.videoFormat();
      if (video === undefined) throw new Error('`v` takes pictures');
      const [column, row] = [n % columns, Math.floor(n / columns)];
      return {
        id: stream.id,
        width: video.width,
        height: video.height,
        cell: {
          x0: Math.floor((column * width) / columns),
          y0: Math.floor((row * height) / rows),
          x1: Math.floor(((column + 1) * width) / columns),
          y1: Math.floor(((row + 1) * height) / rows),
        },
      };
    });
    const whole = { x0: 0, y0: 0, x1: width, y1: height };
    return {
      process(tick, out) {
        const canvas = new Uint8Array(width * height * 4);
        fill(canvas, width, whole, [0, 0, 0, 255]);
        let shown = false;
        for (const tile of tiles) {
          const frame = tick.frame(tile.id);
          if (frame !== undefined) {
            put(canvas, width, tile, tick.fetch(tile.id, frame.index));
            shown = true;
          } else if (tick.feed(tile.id) === undefined) {
            fill(canvas, width, tile.cell, DOWN);
          }
        }
        if (!shown && tick.last()) return;
        out.frame('v', tick.pts(), 1, canvas);
      },
    };
  },
});
