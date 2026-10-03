import { defineNode, Input, Rational, Shape } from '@ffrwd/node';

export const node = defineNode({
  name: 'tally',
  version: '0.1.0',
  rowsSchema:
    '{"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},' +
    '"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},' +
    '"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]}',

  shape() {
    return new Shape()
      .input(Input.packets('video').optional().many().arrival())
      .input(Input.packets('audio').optional().many().arrival())
      .rate(new Rational(10, 1));
  },

  init(_, init) {
    const streams = init.all().map((stream) => {
      if (stream.format?.tag !== 'packets') throw new Error(`\`${stream.port}\` carries no packets`);
      const coded = stream.format.val;
      return {
        id: stream.id,
        timeBase: coded.timeBase,
        // One row per stream, written on the last call.
        count: { port: stream.port, codec: coded.codec, packets: 0, keyframes: 0, bytes: 0, seconds: 0 },
        first: undefined,
        last: undefined,
      };
    });
    return {
      process(tick, out) {
        for (const stream of streams) {
          for (const packet of tick.packets(stream.id)) {
            const count = stream.count;
            count.packets += 1;
            count.keyframes += packet.keyframe ? 1 : 0;
            count.bytes += packet.data.length;
            const end = packet.pts + (packet.duration ?? 0);
            stream.first = Math.min(stream.first ?? packet.pts, packet.pts);
            stream.last = Math.max(stream.last ?? end, end);
          }
        }
        if (tick.last()) {
          for (const stream of streams) {
            if (stream.first !== undefined && stream.last !== undefined) {
              stream.count.seconds = stream.timeBase.seconds(stream.last - stream.first);
            }
            out.report(stream.count);
          }
        }
      },
    };
  },
});
