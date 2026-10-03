import { Anchor, defineNode, Input, Output, parse, Shape } from '@ffrwd/node';

/** The tag a feeder puts on its stream to say its pts are programme time. */
const TIMED = 'smart_timed';

/** One change in what the host says of the feed, or a note the feeder
 * wrote beside its picture. */
const PRESENCE = { event: 'string', t: 'number', at: 'number', text: null };

/** What a feeder writes beside its picture. */
const NOTE = { text: 'string' };

export const node = defineNode({
  name: 'cutin',
  version: '0.1.0',
  paramsSchema:
    '{"type":"object","properties":{"port":{"type":"integer","minimum":1,"maximum":65535,"default":9000},' +
    '"lead":{"type":"number","minimum":0,"maximum":60,"default":0.5},' +
    '"linger":{"type":"number","minimum":0,"maximum":60,"default":0},' +
    '"timeout":{"type":"number","minimum":0,"maximum":60,"default":1}},"additionalProperties":false}',

  shape({ lead, linger, timeout }) {
    const feed = Input.video('feed')
      .optional()
      .hold()
      .anchor(Anchor.tagged(TIMED))
      .lead(lead)
      .group('cam')
      .portParam('port')
      .like('v')
      .pixelFormats(['rgba']);
    if (linger > 0) feed.linger(linger);
    if (timeout > 0) feed.timeout(timeout);
    return new Shape()
      .input(Input.video('v').clock().pixelFormats(['rgba']))
      .input(feed)
      .input(Input.rows('notes').optional().interval().group('cam').schema(NOTE))
      .output(Output.like('v'))
      .output(Output.rows('presence').schema(PRESENCE))
      .pure()
      .oneToOne();
  },

  init(_, init) {
    const v = init.stream('v');
    const video = v.videoFormat();
    if (video === undefined) throw new Error('`v` is a video input');
    const width = video.width;
    const feed = init.optional('feed')?.id;
    const notes = init.optional('notes')?.id;
    // One frame of the programme, in its time base.
    const frameStep = v.hint.rate === undefined ? 1 : Math.max(v.info.timeBase.pts(v.hint.rate.duration(1)), 1);
    return {
      process(tick, out) {
        const frame = tick.frame(v.id);
        if (frame === undefined) return;
        const clock = tick.timeBase();
        const [pts, step] = [frame.pts, Math.max(frame.duration ?? frameStep, 1)];
        const say = (event, at) =>
          out.row('presence', pts, { event, t: clock.seconds(pts), at: clock.seconds(at), text: null });
        let countdown;
        if (feed !== undefined) {
          const current = tick.feed(feed);
          if (current !== undefined) {
            const start = current.start;
            if (start.known === pts && start.known < start.at) say('coming', start.at);
            if (pts <= start.at && start.at < pts + step) say('on', start.at);
            if (pts < start.at) countdown = (start.at - pts) / (start.at - start.known);
          }
          for (const ended of tick.endedFeeds(feed)) {
            if (ended.ends !== undefined && ended.ends < pts && pts - step <= ended.ends) {
              say('off', ended.ends + step);
            }
          }
        }
        if (notes !== undefined) {
          const base = tick.info(notes).timeBase;
          for (const message of tick.messages(notes)) {
            const at = Math.max(base.rescale(message.pts, clock), pts);
            const { text } = parse(new TextDecoder().decode(message.data));
            out.row('presence', at, { event: 'note', t: clock.seconds(pts), at: clock.seconds(at), text });
          }
        }
        const shown = feed === undefined ? undefined : tick.frame(feed);
        if (shown !== undefined) return out.same('v', pts, frame.duration, feed, shown.index);
        if (countdown === undefined) return out.pass('v', v.id, frame);
        const pixels = tick.fetch(v.id, frame.index);
        const bar = Math.trunc(width * countdown);
        const rows = Math.floor(pixels.length / (width * 4));
        for (let row = Math.max(rows - 8, 0); row < rows; row += 1) {
          for (let x = 0; x < bar; x += 1) pixels.set([220, 40, 40, 255], (row * width + x) * 4);
        }
        out.frame('v', pts, frame.duration, pixels);
      },
    };
  },
});
