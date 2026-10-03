import { defineNode, Input, Output, Shape } from '@ffrwd/node';

const STILL = { start_t: 'number', end_t: 'number' };

/** How far apart two pictures' luma planes are: the mean difference of a
 * pixel. */
function difference(a, b) {
  let total = 0;
  for (let at = 0; at < a.length; at += 1) total += Math.abs(a[at] - b[at]);
  return total / Math.max(a.length, 1);
}

export const node = defineNode({
  name: 'still',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},' +
    '"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},' +
    '"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false}',

  shape({ longest }, bound) {
    const frame = bound.rateOf('v')?.duration(1) ?? 1;
    return new Shape()
      .input(Input.video('v').clock().window(2, 1).pixelFormats(['yuv420p']))
      .output(Output.rows('stills').latency(longest + frame).schema(STILL));
  },

  init({ shortest, longest, tolerance }, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    // The bytes of a picture's luma plane, which come first in yuv420p.
    const luma = video.width * video.height;
    // Where the stretch the picture is still in began: its pts and seconds.
    let open;

    /** Writes the open stretch as ending at `end_t`, if it lasted long enough. */
    function close(end_t, out) {
      if (open === undefined) return;
      const [pts, start_t] = open;
      open = undefined;
      if (end_t - start_t >= shortest) out.row('stills', pts, { start_t, end_t });
    }

    return {
      process(tick, out) {
        const seconds = (pts) => tick.timeBase().seconds(pts);
        const frames = tick.frames(v.id);
        if (frames.length !== 2) {
          const last = tick.frame(v.id);
          return close(last === undefined ? tick.seconds() : seconds(last.pts), out);
        }
        const [before, after] = frames;
        const moved = difference(
          tick.fetch(v.id, before.index).subarray(0, luma),
          tick.fetch(v.id, after.index).subarray(0, luma),
        );
        if (moved > tolerance) return close(seconds(after.pts), out);
        open ??= [before.pts, seconds(before.pts)];
        const [, start_t] = open;
        if (seconds(after.pts) - start_t >= longest) {
          close(seconds(after.pts), out);
          open = [after.pts, seconds(after.pts)];
        }
      },
    };
  },
});
