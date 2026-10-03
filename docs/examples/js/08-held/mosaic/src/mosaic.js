import { Anchor, defineNode, Input, Output, Shape } from '@ffrwd/node';
import { Filter, planes, Rect, Rgba } from '@ffrwd/node/frame';

/** What `planes` divides by to hand back eight-bit values unchanged. */
const EIGHT_BITS = { mean: [0, 0, 0], std: [1 / 255, 1 / 255, 1 / 255] };

const DOWN = [48, 48, 48, 255];

/** Every pixel of `cell` of a `width`-wide canvas set to `colour`. */
function fill(canvas, width, cell, colour) {
  const row = new Uint8Array((cell.x1 - cell.x0) * 4);
  for (let at = 0; at < row.length; at += 4) row.set(colour, at);
  for (let y = cell.y0; y < cell.y1; y += 1) canvas.set(row, (y * width + cell.x0) * 4);
}

/** `pixels`, a `tile`'s picture, resized into its cell of a `width`-wide
 * canvas. */
function put(canvas, width, tile, pixels) {
  const picture = new Rgba(pixels, tile.width, tile.height);
  const { cell } = tile;
  const [w, h] = [cell.width(), cell.height()];
  const whole = Rect.whole(tile.width, tile.height);
  const rgb = planes(picture, whole, w, h, Filter.Bilinear, EIGHT_BITS);
  for (let y = 0; y < h; y += 1) {
    for (let x = 0; x < w; x += 1) {
      const at = ((cell.y0 + y) * width + cell.x0 + x) * 4;
      for (let channel = 0; channel < 3; channel += 1) {
        const value = rgb[channel * w * h + y * w + x];
        canvas[at + channel] = Math.min(Math.max(Math.round(value), 0), 255);
      }
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
        cell: new Rect(
          Math.floor((column * width) / columns),
          Math.floor((row * height) / rows),
          Math.floor(((column + 1) * width) / columns),
          Math.floor(((row + 1) * height) / rows),
        ),
      };
    });
    const whole = Rect.whole(width, height);
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
