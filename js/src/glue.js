// Between the host's records, as componentize-js hands and takes them, and
// the adapter's: s64 and u64 as BigInt there and Number here, list<u32> as a
// Uint32Array there, a rational as a plain record there and a `Rational`
// here.

import { rational } from './time.js';
import { BoundStream, StreamInfo } from './types.js';

const toNumber = (value) => (value === undefined || value === null ? undefined : Number(value));
const toBig = (value) => (value === undefined || value === null ? undefined : BigInt(value));
const record = (value) => (value === undefined ? undefined : { num: value.num, den: value.den });

function hint(value) {
  return { rate: rational(value?.rate) };
}

export function hostBinding(binding) {
  return { input: binding.input, streams: binding.streams.map(hint) };
}

function streamInfo(value) {
  return new StreamInfo(rational(value.timeBase), {
    index: value.index,
    kind: value.kind,
    codec: value.codec,
    duration: value.duration ?? undefined,
    tags: value.tags.map(([key, tagged]) => [key, tagged]),
  });
}

function codedStream(value) {
  const format = value.format;
  let coded = format;
  if (format.tag === 'video') {
    coded = { tag: 'video', val: { ...format.val, sampleAspectRatio: rational(format.val.sampleAspectRatio) } };
  }
  return { ...value, timeBase: rational(value.timeBase), format: coded };
}

function format(value) {
  if (value === undefined || value === null || value.tag === 'like') return undefined;
  if (value.tag === 'packets') return { tag: 'packets', val: codedStream(value.val) };
  return value;
}

export function hostBoundStream(value) {
  const stream = new BoundStream(value.port, value.id, undefined);
  stream.info = streamInfo(value.info);
  stream.format = format(value.format);
  stream.rendition = { ...value.rendition, bandwidth: toNumber(value.rendition.bandwidth) };
  stream.row = value.row ?? undefined;
  stream.decodeDelay = value.decodeDelay;
  stream.latency = value.latency ?? undefined;
  stream.hint = hint(value.hint);
  return stream;
}

function feed(value) {
  const start = value.start;
  return {
    start: { tags: start.tags, firstPts: Number(start.firstPts), at: Number(start.at), known: Number(start.known) },
    ends: toNumber(value.ends),
  };
}

function packet(value) {
  return {
    pts: Number(value.pts),
    dts: toNumber(value.dts),
    duration: toNumber(value.duration),
    keyframe: value.keyframe,
    data: value.data,
  };
}

/** The host's borrowed `tick`, read as the adapter's records. */
export class HostTick {
  constructor(tick) {
    this.tick = tick;
  }

  pts() {
    return Number(this.tick.pts());
  }

  ordinal() {
    return Number(this.tick.ordinal());
  }

  timeBase() {
    return rational(this.tick.timeBase());
  }

  last() {
    return this.tick.last();
  }

  streams(port) {
    return [...this.tick.streams(port)];
  }

  info(id) {
    return streamInfo(this.tick.info(id));
  }

  feed(id) {
    const found = this.tick.feed(id);
    return found === undefined || found === null ? undefined : feed(found);
  }

  endedFeeds(id) {
    return this.tick.endedFeeds(id).map(feed);
  }

  frames(id) {
    return this.tick.frames(id).map((frame) => ({
      pts: Number(frame.pts),
      index: frame.index,
      duration: toNumber(frame.duration),
      rows: frame.rows,
    }));
  }

  fetch(id, index) {
    return this.tick.fetch(id, index);
  }

  messages(id) {
    return this.tick.messages(id).map((message) => ({ pts: Number(message.pts), data: message.data }));
  }

  packets(id) {
    return this.tick.packets(id).map(packet);
  }

  earlierRows(id) {
    return this.tick.earlierRows(id).map((timed) => ({ pts: Number(timed.pts), rows: timed.rows }));
  }
}

const u32s = (values) => Uint32Array.from(values);

function hostVideo(value) {
  return { width: value.width, height: value.height, pixFmt: value.pixFmt, color: value.color };
}

function hostCoded(value) {
  let coded = value.format;
  if (coded.tag === 'video') {
    coded = { tag: 'video', val: { ...coded.val, sampleAspectRatio: record(coded.val.sampleAspectRatio) } };
  }
  return {
    codec: value.codec,
    timeBase: record(value.timeBase),
    format: coded,
    extradata: value.extradata ?? new Uint8Array(0),
    profile: value.profile,
    level: value.level,
  };
}

function hostOutputFormat(output) {
  if (output.like !== undefined) {
    const { port, pixelFormat, sampleFormat } = output.like;
    return { tag: 'like', val: { port: port ?? '', pixelFormat, sampleFormat } };
  }
  const value = output.format;
  if (value === undefined) return undefined;
  switch (value.tag) {
    case 'video':
      return { tag: 'video', val: hostVideo(value.val) };
    case 'packets':
      return { tag: 'packets', val: hostCoded(value.val) };
    default:
      return value;
  }
}

function hostInput(input) {
  const { accepts } = input;
  return {
    name: input.name,
    kind: input.kind,
    required: input.required,
    many: input.many,
    pairing: input.pairing,
    rows: input.rows,
    window: input.window,
    stride: input.stride,
    accepts: {
      pixelFormats: accepts.pixelFormats,
      sampleFormats: accepts.sampleFormats,
      sampleRates: u32s(accepts.sampleRates),
      channelCounts: u32s(accepts.channelCounts),
      codecs: accepts.codecs,
      wants: accepts.wants,
      like: accepts.like,
    },
    schema: input.schema,
  };
}

function hostOutput(output) {
  return {
    name: output.name,
    kind: output.kind,
    format: hostOutputFormat(output),
    timeBase: record(output.timeBase),
    latency: output.latency,
    schema: output.schema,
    row: output.row,
  };
}

function hostClock(clock) {
  if (clock === undefined) return { tag: 'self-clocked' };
  if (clock.tag === 'rate') return { tag: 'rate', val: record(clock.val) };
  return clock;
}

/** A resolved shape as the host takes it. */
export function hostShape(shape) {
  return {
    inputs: shape.inputs.map(hostInput),
    outputs: shape.outputs.map(hostOutput),
    clock: hostClock(shape.clock),
    pure: shape.pure,
    oneToOne: shape.oneToOne,
    bounded: shape.bounded,
    relation: shape.relation,
  };
}

function hostPayload({ tag, val }) {
  switch (tag) {
    case 'frame':
      return { tag, val: { pts: BigInt(val.pts), duration: toBig(val.duration), data: val.data } };
    case 'same':
      return { tag, val: { pts: BigInt(val.pts), duration: toBig(val.duration), id: val.id, index: val.index } };
    case 'message':
      return { tag, val: { pts: BigInt(val.pts), data: val.data } };
    default:
      return {
        tag,
        val: {
          pts: BigInt(val.pts),
          dts: toBig(val.dts),
          duration: toBig(val.duration),
          keyframe: Boolean(val.keyframe),
          data: val.data,
        },
      };
  }
}

/** What one tick produced, as the host takes it. */
export function hostEmitted(emitted) {
  return {
    items: emitted.items.map((item) => ({ port: item.port, payload: hostPayload(item.payload) })),
    rows: emitted.reports,
    finished: emitted.finished,
  };
}
