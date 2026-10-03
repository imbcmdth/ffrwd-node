// The records the host hands a node, as the adapter keeps them: field for
// field the WIT's in camelCase, a variant as `{ tag, val }`, every pts a
// Number, every time base a `Rational`.

import { Rational } from './time.js';

/** The stream a bound input reads, as the host knows it. */
export class StreamInfo {
  constructor(timeBase, fields = {}) {
    this.index = fields.index ?? 0;
    this.kind = fields.kind ?? '';
    this.codec = fields.codec ?? '';
    this.duration = fields.duration;
    this.tags = fields.tags ?? [];
    this.timeBase = timeBase;
  }

  /** The value of tag `name`, if the stream carries it. */
  tag(name) {
    return this.tags.find(([key]) => key === name)?.[1];
  }
}

/** One stream bound to an input port at `init`. */
export class BoundStream {
  constructor(port, id, timeBase) {
    this.port = port;
    this.id = id;
    this.info = new StreamInfo(timeBase);
    this.format = undefined;
    this.rendition = {};
    this.row = undefined;
    this.decodeDelay = 0;
    this.latency = undefined;
    this.hint = { rate: undefined };
  }

  /** The stream at `rate`, as the compiler told `shape`. */
  rate(rate) {
    this.hint = { rate };
    return this;
  }

  /** A video stream of `width` x `height` in `pixFmt`. */
  static video(port, id, width, height, pixFmt, timeBase) {
    const stream = new BoundStream(port, id, timeBase);
    stream.info.kind = 'video';
    stream.format = { tag: 'video', val: { width, height, pixFmt, color: undefined } };
    return stream;
  }

  /** An audio stream counted in samples, its hint at that rate. */
  static audio(port, id, sampleRate, channels, sampleFmt) {
    const stream = new BoundStream(port, id, new Rational(1, sampleRate)).rate(new Rational(sampleRate, 1));
    stream.info.kind = 'audio';
    stream.format = { tag: 'audio', val: { sampleRate, channels, sampleFmt, channelLayout: undefined } };
    return stream;
  }

  /** A data stream of JSON rows. */
  static rows(port, id, timeBase) {
    const stream = new BoundStream(port, id, timeBase);
    stream.info.kind = 'data';
    stream.info.codec = 'json';
    stream.format = { tag: 'data', val: 'json' };
    return stream;
  }

  /** The stream's pictures, when it is a video stream. */
  videoFormat() {
    return this.format?.tag === 'video' ? this.format.val : undefined;
  }

  /** The stream's samples, when it is an audio stream. */
  audioFormat() {
    return this.format?.tag === 'audio' ? this.format.val : undefined;
  }
}
