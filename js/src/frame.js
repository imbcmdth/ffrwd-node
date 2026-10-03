// The crop and Pillow's bilinear resize that the Rust crate ffrwd-frame
// gives a Rust module, with its names: a frame, a rect of it, and the planes
// or the tensor a vision model reads. The resize is ffrwd-frame's to the
// byte: the same fixed point, across first and then down, each pass rounding
// back into eight bits, a pass skipped when its axis keeps its size.

/** A frame as the wire carries it: 4 bytes a pixel, row-major, no padding.
 * The alpha byte is read by nothing here. */
export class Rgba {
  /** Throws unless `data` is exactly `width * height * 4` bytes. */
  constructor(data, width, height) {
    const want = width * height * 4;
    if (data.length !== want) {
      throw new Error(`an rgba frame of ${width}x${height} is ${want} bytes, not ${data.length}`);
    }
    this.data = data;
    this.width = width;
    this.height = height;
  }
}

/** A half-open rectangle in frame pixels: exclusive on the right and bottom. */
export class Rect {
  constructor(x0, y0, x1, y1) {
    this.x0 = x0;
    this.y0 = y0;
    this.x1 = x1;
    this.y1 = y1;
  }

  /** The whole frame. */
  static whole(width, height) {
    return new Rect(0, 0, width, height);
  }

  /** The crop a detector's box names: `x`, `y`, `w` and `h` in frame pixels,
   * widened by `pad` of the box's own width and height on every side, each
   * edge floored to a whole pixel and clamped to the frame. Undefined when
   * nothing of it lands on the frame. */
  static padded(x, y, w, h, pad, width, height) {
    const [pw, ph] = [w * pad, h * pad];
    const floorClamp = (value, limit) => Math.min(Math.max(Math.floor(value), 0), limit);
    const rect = new Rect(
      floorClamp(x - pw, width),
      floorClamp(y - ph, height),
      floorClamp(x + w + pw, width),
      floorClamp(y + h + ph, height),
    );
    return rect.x1 > rect.x0 && rect.y1 > rect.y0 ? rect : undefined;
  }

  /** How many pixels across. */
  width() {
    return Math.max(this.x1 - this.x0, 0);
  }

  /** How many pixels down. */
  height() {
    return Math.max(this.y1 - this.y0, 0);
  }
}

/** Pillow's resampling filters. Bilinear is Pillow's BILINEAR, antialiased
 * when downscaling. */
export const Filter = Object.freeze({ Bilinear: 'bilinear' });

/** What the usual export normalizes with: ImageNet's mean and standard
 * deviation, over red, green and blue in 0..1. */
export const IMAGENET = Object.freeze({ mean: [0.485, 0.456, 0.406], std: [0.229, 0.224, 0.225] });

/** `rect` of `frame` resized to `width` x `height` (stretched, no aspect
 * padding), then scaled to 0..1 and normalized by `norm`, a `{ mean, std }`
 * of three each: planar RGB, `[3, height, width]`, the red plane first. */
export function planes(frame, rect, width, height, filter, norm) {
  return toPlanes(resizedRgb(frame, rect, width, height, filter), width, height, norm);
}

/** The same as the little-endian fp32 bytes of a `[1, 3, height, width]`
 * tensor. */
export function tensor(frame, rect, width, height, filter, norm) {
  return tensors(frame, [rect], width, height, filter, norm);
}

/** Several crops of one frame as one `[n, 3, height, width]` tensor's bytes,
 * in the order the rects were given. */
export function tensors(frame, rects, width, height, filter, norm) {
  const size = width * height * 3;
  const bytes = new Uint8Array(rects.length * size * 4);
  const view = new DataView(bytes.buffer);
  rects.forEach((rect, n) => {
    const floats = planes(frame, rect, width, height, filter, norm);
    for (let at = 0; at < size; at += 1) view.setFloat32((n * size + at) * 4, floats[at], true);
  });
  return bytes;
}

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
  const most = kernels.reduce((top, kernel) => kernel.weights.reduce((a, b) => Math.max(a, b), top), 0);
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

/** `rect` of the frame, brought inside it, resized to `width` x `height`:
 * eight-bit interleaved red, green and blue. Black when the crop or the
 * target has no pixels. */
function resizedRgb(frame, rect, width, height, filter) {
  if (filter !== Filter.Bilinear) throw new Error(`no filter ${filter}: Filter.Bilinear is the one there is`);
  const x1 = Math.min(rect.x1, frame.width);
  const y1 = Math.min(rect.y1, frame.height);
  const [x0, y0] = [Math.min(rect.x0, x1), Math.min(rect.y0, y1)];
  let [w, h] = [x1 - x0, y1 - y0];
  if (w === 0 || h === 0 || width === 0 || height === 0) return new Uint8Array(width * height * 3);
  let rgb = new Uint8Array(w * h * 3);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const [from, to] = [((y0 + y) * frame.width + x0 + x) * 4, (y * w + x) * 3];
      for (let channel = 0; channel < 3; channel += 1) rgb[to + channel] = frame.data[from + channel];
    }
  }
  if (w !== width) [rgb, w] = [pass(rgb, w, h, width, true), width];
  if (h !== height) rgb = pass(rgb, w, h, height, false);
  return rgb;
}

/** Interleaved eight-bit RGB as planar floats, each channel scaled to 0..1
 * and normalized, in single precision step by step as the Rust crate does. */
function toPlanes(rgb, width, height, norm) {
  const plane = width * height;
  const out = new Float32Array(plane * 3);
  const f = Math.fround;
  for (let channel = 0; channel < 3; channel += 1) {
    const [mean, std] = [f(norm.mean[channel]), f(norm.std[channel])];
    for (let at = 0; at < plane; at += 1) {
      out[channel * plane + at] = f(f(f(rgb[at * 3 + channel] / 255) - mean) / std);
    }
  }
  return out;
}
