import { CUE, Cue, defineNode, Input, Output, Shape } from '@ffrwd/node';

/** How much of the band `cue` shows at `t`: rising over `fade` seconds
 * before it starts, whole while it runs, falling over `fade` after it ends. */
function opacity(cue, t, fade) {
  if (fade === 0) return cue.covers(t) ? 1 : 0;
  const rising = (t - (cue.start_t - fade)) / fade;
  const falling = (cue.end_t + fade - t) / fade;
  return Math.min(Math.max(Math.min(rising, falling), 0), 1);
}

export const node = defineNode({
  name: 'band',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},' +
    '"additionalProperties":false}',

  shape({ fade }) {
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(Input.rows('cues').interval().latency(5).ahead(fade).state().schema(CUE))
      .output(Output.like('v'))
      .pure()
      .oneToOne();
  },

  init({ fade }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const { width, height } = video;
    let cues = [];
    return {
      fold(row) {
        const { start_t, end_t, text } = row.row();
        cues.push(new Cue(start_t, end_t, text));
      },
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const t = tick.timeBase().seconds(frame.pts);
        cues = cues.filter((cue) => cue.end_t + fade > t);
        const shown = Math.max(0, ...cues.map((cue) => opacity(cue, t, fade)));
        if (shown === 0) return out.pass('v', v.id, frame);
        const pixels = tick.fetch(v.id, frame.index);
        const keep = 1 - 0.6 * shown;
        const top = Math.floor((height * 4) / 5);
        for (let at = top * width * 4; at < pixels.length; at += 4) {
          for (let channel = at; channel < at + 3; channel += 1) pixels[channel] = Math.round(pixels[channel] * keep);
        }
        out.frame('v', frame.pts, frame.duration, pixels);
      },
    };
  },
});
