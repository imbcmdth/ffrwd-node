import { defineNode, Output, Shape } from '@ffrwd/node';

/** Waits `micros` microseconds. A JavaScript call has nothing that blocks,
 * so it watches the clock until the time has passed. */
function sleep(micros) {
  const until = performance.now() + micros / 1000;
  while (performance.now() < until) continue;
}

export const node = defineNode({
  name: 'beat',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},' +
    '"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},' +
    '"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false}',

  shape({ width, height }) {
    return new Shape()
      .selfClocked()
      .output(Output.video('video').size(width, height).pixelFormat('rgba').row(0))
      .relationRow(JSON.stringify({ width, height }))
      .bounded(false);
  },

  init(params) {
    const every = Math.round(params.every * 1e6);
    const pixels = params.width * params.height;
    let next = 0;
    return {
      process(tick, out) {
        const now = tick.pts();
        if (now < next) sleep(next - now);
        const wall = Math.floor(Date.now() / 1000);
        const grey = (wall % 8) * 32;
        const frame = new Uint8Array(pixels * 4).fill(grey);
        for (let at = 3; at < frame.length; at += 4) frame[at] = 255;
        out.frame('video', next, every, frame);
        next += every;
      },
    };
  },
});
