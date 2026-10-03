import { CUE, Cue, defineNode, Input, Output, Shape } from '@ffrwd/node';

/** How loud `samples` are, as a cue's text: their RMS in dB of full scale. */
function loudness(samples) {
  const power = samples.reduce((sum, sample) => sum + sample * sample, 0) / Math.max(samples.length, 1);
  const db = 10 * Math.log10(power);
  return db < -90 ? 'silence' : `${db.toFixed(0)} dB`;
}

export const node = defineNode({
  name: 'level',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"window":{"type":"number","exclusiveMinimum":0,"maximum":60,"default":2},' +
    '"hop":{"type":["number","null"],"exclusiveMinimum":0,"maximum":60}},"additionalProperties":false}',

  shape(params, bound) {
    const rate = bound.rateOf('a');
    if (rate === undefined) {
      throw new Error('level counts its window in samples, and the call gives `a` no sample rate');
    }
    const window = rate.count(params.window);
    const stride = rate.count(params.hop ?? params.window);
    return new Shape()
      .input(Input.audio('a').clock().window(window, stride).sampleFormats(['f32']))
      .output(Output.rows('cues').schema(CUE))
      .pure();
  },

  init(_, init) {
    const a = init.stream('a');
    const audio = a.audioFormat();
    if (audio === undefined) throw new Error('`a` is an audio input');
    const channels = Math.max(audio.channels, 1);
    const rate = audio.sampleRate;
    return {
      process(tick, out) {
        const run = tick.frame(a.id);
        if (run === undefined) return;
        const bytes = tick.fetch(a.id, run.index);
        const samples = new Float32Array(bytes.slice(0, bytes.length - (bytes.length % 4)).buffer);
        const start = tick.timeBase().seconds(run.pts);
        const end = start + Math.floor(samples.length / channels) / rate;
        const cue = new Cue(start, end, loudness(samples));
        out.row('cues', run.pts, cue);
      },
    };
  },
});
