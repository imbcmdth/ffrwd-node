import { defineNode, Output, Rational, Shape } from '@ffrwd/node';

const COLOURS = [
  [192, 192, 192, 255],
  [192, 192, 0, 255],
  [0, 192, 192, 255],
  [0, 192, 0, 255],
  [192, 0, 192, 255],
  [192, 0, 0, 255],
  [0, 0, 192, 255],
];

/** Seven bars, and a white line crossing them once a second. */
function draw(width, height, t) {
  const line = Math.trunc((t % 1) * width);
  const row = new Uint8Array(width * 4);
  for (let x = 0; x < width; x += 1) {
    row.set(x === line ? [255, 255, 255, 255] : COLOURS[Math.floor((x * COLOURS.length) / width)], x * 4);
  }
  const canvas = new Uint8Array(row.length * height);
  for (let y = 0; y < height; y += 1) canvas.set(row, y * row.length);
  return canvas;
}

export const node = defineNode({
  name: 'bars',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},' +
    '"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},' +
    '"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},' +
    '"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false}',

  shape({ width, height, fps, seconds }) {
    return new Shape()
      .rate(Rational.approximate(fps, 1001))
      .output(Output.video('video').size(width, height).pixelFormat('rgba').row(0))
      .relationRow(JSON.stringify({ width, height }))
      .bounded(seconds !== undefined)
      .pure();
  },

  init({ width, height, seconds }) {
    return {
      process(tick, out) {
        if (seconds !== undefined && tick.seconds() >= seconds) {
          out.finish();
          return;
        }
        out.frame('video', tick.pts(), 1, draw(width, height, tick.seconds()));
      },
    };
  },
});
