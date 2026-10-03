// A node's ports and clock, built the way a query reads them:
// `Input.video('v').clock()`, `Input.rows('boxes').schema(BOX)`,
// `Output.video('mask').pixelFormat('gray')`. A builder keeps what it was
// told in `spec`; `Shape.resolve` turns them into the `NodeShape` the host
// is handed.

import { schemaOf } from './rows.js';

const FRAMES = new Set(['video', 'audio']);

/** What maps a stream's pts onto the clock, held or by interval. */
export const Anchor = Object.freeze({
  sharedClock: Object.freeze({ tag: 'shared-clock' }),
  firstFrame: Object.freeze({ tag: 'first-frame' }),
  /** Shared clock for a feed whose tags carry `name` set to 1, first frame
   * otherwise. */
  tagged: (name) => ({ tag: 'tagged', val: name }),
});

function sameAnchor(a, b) {
  return a.tag === b.tag && a.val === b.val;
}

function describeSchema(schema) {
  return typeof schema === 'string' ? schema : schemaOf(schema);
}

function copy(value) {
  if (Array.isArray(value)) return value.map(copy);
  if (value !== null && typeof value === 'object' && Object.getPrototypeOf(value) === Object.prototype) {
    return Object.fromEntries(Object.entries(value).map(([key, field]) => [key, copy(field)]));
  }
  return value;
}

/** One input port. */
export class Input {
  constructor(name, kind) {
    this.spec = {
      name,
      kind,
      required: true,
      many: false,
      pairing: { tag: 'lockstep' },
      rows: kind === 'data' ? 'per-frame' : 'ignore',
      window: 1,
      stride: 1,
      accepts: {
        pixelFormats: [],
        sampleFormats: [],
        sampleRates: [],
        channelCounts: [],
        codecs: [],
        wants: 'all',
        like: undefined,
      },
      schema: undefined,
      clock: false,
    };
  }

  static video(name) {
    return new Input(name, 'video');
  }

  static audio(name) {
    return new Input(name, 'audio');
  }

  /** A data input of JSON rows, read per frame unless told otherwise. */
  static rows(name) {
    return new Input(name, 'data');
  }

  static packets(name) {
    return new Input(name, 'packets');
  }

  /** The clock: the frames each call sees. Lockstep, single and required. */
  clock() {
    this.spec.clock = true;
    this.spec.pairing = { tag: 'lockstep' };
    return this;
  }

  /** The call may leave it out. */
  optional() {
    this.spec.required = false;
    return this;
  }

  /** Any number of streams bound to this one port. */
  many() {
    this.spec.many = true;
    return this;
  }

  /** The frames (video) or samples (audio) a clock call sees, and how many
   * it consumes: 15, 1 is a sliding window of fifteen frames. */
  window(window, stride) {
    this.spec.window = window;
    this.spec.stride = stride;
    return this;
  }

  lockstep() {
    this.spec.pairing = { tag: 'lockstep' };
    return this;
  }

  arrival() {
    this.spec.pairing = { tag: 'arrival' };
    return this;
  }

  /** Paired by holding the newest frame: anchored on its first frame, no
   * lead, linger or timeout until the methods below say otherwise. */
  hold() {
    if (this.spec.pairing.tag !== 'hold') {
      this.spec.pairing = {
        tag: 'hold',
        val: {
          anchor: Anchor.firstFrame,
          lead: 0,
          linger: undefined,
          timeout: undefined,
          group: undefined,
          portParam: undefined,
        },
      };
    }
    return this;
  }

  /** Paired by interval: every message stamped in the tick's interval,
   * settled once the producer's progress has passed it. */
  interval() {
    if (this.spec.pairing.tag !== 'interval') {
      this.spec.pairing = {
        tag: 'interval',
        val: { latency: undefined, ahead: 0, anchor: Anchor.sharedClock, group: undefined },
      };
    }
    return this;
  }

  timed() {
    const tag = this.spec.pairing.tag;
    if (tag === 'hold' || tag === 'interval') return this;
    return FRAMES.has(this.spec.kind) ? this.hold() : this.interval();
  }

  /** What maps the stream's pts onto the clock, held or by interval. */
  anchor(anchor) {
    this.timed().spec.pairing.val.anchor = anchor;
    return this;
  }

  lead(seconds) {
    this.hold().spec.pairing.val.lead = seconds;
    return this;
  }

  linger(seconds) {
    this.hold().spec.pairing.val.linger = seconds;
    return this;
  }

  timeout(seconds) {
    this.hold().spec.pairing.val.timeout = seconds;
    return this;
  }

  /** A hold group: held inputs of one group arrive on one connection from
   * one source. A data input naming a hold group arrives on that
   * connection, paired by interval. */
  group(group) {
    this.timed().spec.pairing.val.group = group;
    return this;
  }

  /** The param the host writes this input's loopback port into. */
  portParam(param) {
    this.hold().spec.pairing.val.portParam = param;
    return this;
  }

  /** The most this interval input waits past the interval's end. */
  latency(seconds) {
    this.interval().spec.pairing.val.latency = seconds;
    return this;
  }

  /** Seconds past the interval's end whose messages come with it. */
  ahead(seconds) {
    this.interval().spec.pairing.val.ahead = seconds;
    return this;
  }

  perFrame() {
    this.spec.rows = 'per-frame';
    return this;
  }

  /** Rows folded into state later ticks depend on: they reach the
   * instance's `fold`. */
  state() {
    this.spec.rows = 'state';
    return this;
  }

  ignoreRows() {
    this.spec.rows = 'ignore';
    return this;
  }

  pixelFormats(formats) {
    this.spec.accepts.pixelFormats = [...formats];
    return this;
  }

  sampleFormats(formats) {
    this.spec.accepts.sampleFormats = [...formats];
    return this;
  }

  sampleRates(rates) {
    this.spec.accepts.sampleRates = [...rates];
    return this;
  }

  channelCounts(counts) {
    this.spec.accepts.channelCounts = [...counts];
    return this;
  }

  codecs(codecs) {
    this.spec.accepts.codecs = [...codecs];
    return this;
  }

  /** `'all'`, `'keyframes'`, `'first'` or `'timing'`. */
  wants(wants) {
    this.spec.accepts.wants = wants;
    return this;
  }

  /** Read for its frames' times and its stream's info alone: the host
   * carries no pixels or samples for it, and `fetch` on it is refused. */
  timing() {
    return this.wants('timing');
  }

  /** Conformed to input `port`'s size (video), or rate and layout (audio). */
  like(port) {
    this.spec.accepts.like = port;
    return this;
  }

  /** The rows read here: a description for `schemaOf`, or a schema written
   * out. */
  schema(schema) {
    this.spec.schema = describeSchema(schema);
    return this;
  }
}

function emptyLike() {
  return { port: undefined, pixelFormat: undefined, sampleFormat: undefined };
}

/** One output port. */
export class Output {
  constructor(name, kind) {
    this.spec = {
      name,
      kind,
      format: undefined,
      like: undefined,
      timeBase: undefined,
      latency: 0,
      schema: undefined,
      row: undefined,
      namedLike: false,
    };
  }

  /** A video output: the clock input's format until `following`, `size` or
   * `pixelFormat` say otherwise. */
  static video(name) {
    return new Output(name, 'video');
  }

  static audio(name) {
    return new Output(name, 'audio');
  }

  /** A data output of JSON rows. */
  static rows(name) {
    const output = new Output(name, 'data');
    output.spec.format = { tag: 'data', val: 'json' };
    return output;
  }

  static packets(name) {
    return new Output(name, 'packets');
  }

  /** An output named after input `port`, of its kind and in its format: a
   * filter's `v` out for its `v` in. */
  static like(port) {
    const output = new Output(port, 'video');
    output.spec.namedLike = true;
    output.spec.like = { ...emptyLike(), port };
    return output;
  }

  /** In input `port`'s format. */
  following(port) {
    this.spec.like ??= emptyLike();
    this.spec.like.port = port;
    this.spec.format = undefined;
    return this;
  }

  /** In this pixel format: of the input it follows, the clock input when it
   * follows none, or of the size `size` gave. */
  pixelFormat(pixFmt) {
    if (this.spec.format?.tag === 'video') {
      this.spec.format.val.pixFmt = pixFmt;
    } else {
      this.spec.like ??= emptyLike();
      this.spec.like.pixelFormat = pixFmt;
    }
    return this;
  }

  /** In this sample format, of the input it follows or the clock input. */
  sampleFormat(sampleFmt) {
    if (this.spec.format?.tag === 'audio') {
      this.spec.format.val.sampleFmt = sampleFmt;
    } else {
      this.spec.like ??= emptyLike();
      this.spec.like.sampleFormat = sampleFmt;
    }
    return this;
  }

  /** Pictures of `width` x `height`, in the pixel format `pixelFormat`
   * names. */
  size(width, height) {
    const pixFmt = this.spec.like?.pixelFormat ?? '';
    this.spec.like = undefined;
    this.spec.format = { tag: 'video', val: { width, height, pixFmt, color: undefined } };
    return this;
  }

  videoFormat(format) {
    this.spec.like = undefined;
    this.spec.format = { tag: 'video', val: format };
    return this;
  }

  audioFormat(format) {
    this.spec.like = undefined;
    this.spec.format = { tag: 'audio', val: format };
    return this;
  }

  coded(coded) {
    this.spec.like = undefined;
    this.spec.format = { tag: 'packets', val: coded };
    return this;
  }

  /** The time base its pts count; the clock's until this is given. */
  timeBase(timeBase) {
    this.spec.timeBase = timeBase;
    return this;
  }

  /** How far behind the end of its tick's interval a stamp may fall. */
  latency(seconds) {
    this.spec.latency = seconds;
    return this;
  }

  /** The rows written here: a description for `schemaOf`, or a schema
   * written out. */
  schema(schema) {
    this.spec.schema = describeSchema(schema);
    return this;
  }

  /** A source's relation row this output belongs to. */
  row(row) {
    this.spec.row = row;
    return this;
  }
}

/** A node's ports and clock for one call's params, as the host is handed
 * them: what `Shape.resolve` answers. */
export class NodeShape {
  constructor(spec) {
    Object.assign(this, spec);
  }

  findInput(name) {
    return this.inputs.find((input) => input.name === name);
  }

  findOutput(name) {
    return this.outputs.find((output) => output.name === name);
  }

  /** The clock input's name, when an input is the clock. */
  clockInput() {
    return this.clock?.tag === 'input' ? this.clock.val : undefined;
  }
}

/** A node's ports and clock: no ports, impure, not one-to-one, bounded. */
export class Shape {
  constructor() {
    this.spec = {
      inputs: [],
      outputs: [],
      clock: undefined,
      pure: false,
      oneToOne: false,
      bounded: true,
      relation: [],
    };
  }

  input(input) {
    this.spec.inputs.push(input);
    return this;
  }

  output(output) {
    this.spec.outputs.push(output);
    return this;
  }

  /** Ticks `rate` times a second. */
  rate(rate) {
    this.spec.clock = { tag: 'rate', val: rate };
    return this;
  }

  /** Ticks at input `port`'s rate, which the compiler reads off its first
   * stream. */
  rateOf(port) {
    this.spec.clock = { tag: 'rate-of', val: port };
    return this;
  }

  selfClocked() {
    this.spec.clock = { tag: 'self-clocked' };
    return this;
  }

  /** Every call depends only on what it was handed, so the host may spread
   * the node over workers. */
  pure() {
    this.spec.pure = true;
    return this;
  }

  /** One frame out per frame in on the clock's outputs, each at its tick's
   * pts. */
  oneToOne() {
    this.spec.oneToOne = true;
    return this;
  }

  /** Whether a rate or self-clocked node ends by itself. */
  bounded(bounded) {
    this.spec.bounded = bounded;
    return this;
  }

  /** One relation row of a source read in FROM, as a JSON object. */
  relationRow(row) {
    this.spec.relation.push(row);
    return this;
  }

  /** The shape the host is handed for a call binding `bound`: the clock
   * settled, outputs following an unbound input left out, and every rule the
   * host would refuse it by checked here first, with the port named. */
  resolve(bound) {
    const shape = new NodeShape({
      ...this.spec,
      inputs: this.spec.inputs.map((input) => copy(input.spec)),
      outputs: this.spec.outputs.map((output) => copy(output.spec)),
      relation: [...this.spec.relation],
    });
    const clocks = shape.inputs.filter((input) => input.clock).map((input) => input.name);
    if (clocks.length > 1) throw new Error(`only one input can be the clock, not ${clocks.join(' and ')}`);
    if (clocks.length === 0 && shape.clock === undefined) {
      throw new Error('the shape has no clock: mark an input `clock()`, or give a rate');
    }
    if (clocks.length === 1) {
      const [name] = clocks;
      if (shape.clock === undefined) shape.clock = { tag: 'input', val: name };
      else if (shape.clock.tag !== 'input' || shape.clock.val !== name) {
        throw new Error(`\`${name}\` is the clock, and so is the shape's rate`);
      }
    }
    unique(shape.inputs, 'inputs');
    unique(shape.outputs, 'outputs');

    const clock = shape.clockInput();
    for (const input of shape.inputs) {
      if (input.accepts.like !== undefined && !bound.has(input.accepts.like)) input.accepts.like = undefined;
    }
    const outputs = [];
    for (const output of shape.outputs) {
      const like = output.like;
      if (like !== undefined) {
        const port = like.port ?? clock;
        if (port === undefined) {
          throw new Error(
            `output \`${output.name}\` takes its format from the clock input, and the clock is not an input: ` +
              'give it `following(port)` or a format of its own',
          );
        }
        const input = shape.findInput(port);
        if (input === undefined) throw new Error(`output \`${output.name}\` follows \`${port}\`, which is not an input`);
        if (!bound.has(port)) continue;
        if (input.many) throw new Error(`output \`${output.name}\` follows \`${port}\`, which takes many streams`);
        if (output.namedLike) output.kind = input.kind;
        like.port = port;
      }
      outputs.push(output);
    }
    shape.outputs = outputs;
    check(shape, bound);
    return shape;
  }
}

function unique(ports, what) {
  const names = new Set();
  for (const port of ports) {
    if (names.has(port.name)) throw new Error(`two ${what} are named \`${port.name}\``);
    names.add(port.name);
  }
}

function check(shape, bound) {
  const clock = shape.clock;
  if (clock?.tag === 'input') {
    const input = shape.findInput(clock.val);
    if (input === undefined) throw new Error(`the clock is \`${clock.val}\`, which is not an input`);
    if (!input.required || input.many || input.pairing.tag !== 'lockstep') {
      throw new Error(`the clock \`${clock.val}\` has to be required, single and lockstep`);
    }
  } else if (clock?.tag === 'rate-of' && shape.findInput(clock.val) === undefined) {
    throw new Error(`the rate is \`${clock.val}\`'s, which is not an input`);
  } else if (clock?.tag === 'rate' && (clock.val.num <= 0 || clock.val.den <= 0)) {
    throw new Error(`a rate of ${clock.val.num}/${clock.val.den} never ticks`);
  }
  const inputClock = shape.clockInput() !== undefined;
  for (const input of shape.inputs) {
    const name = input.name;
    const frames = FRAMES.has(input.kind);
    const tag = input.pairing.tag;
    if (tag === 'lockstep' && !inputClock) {
      throw new Error(
        `\`${name}\` is lockstep, and a node without an input clock has nothing to be in step with: ` +
          'hold it, or pair it by interval',
      );
    }
    if (tag === 'hold' && !frames) throw new Error(`\`${name}\` carries messages, so it cannot be held`);
    if (tag === 'interval' && frames) throw new Error(`\`${name}\` carries frames, so it cannot pair by interval`);
    if (clock?.tag === 'self-clocked' && tag !== 'arrival') {
      throw new Error(`\`${name}\` feeds a self-clocked node, so it pairs by arrival`);
    }
    if (input.kind === 'data' && input.rows === 'ignore') {
      throw new Error(`\`${name}\` carries rows, so it cannot ignore them`);
    }
    if (input.accepts.wants === 'timing' && !frames) {
      throw new Error(`\`${name}\` carries no frames, so it cannot be read for its timing alone`);
    }
    if (tag === 'interval' && input.pairing.val.group !== undefined) {
      const { group, anchor } = input.pairing.val;
      const held = shape.inputs.some((other) => other.pairing.tag === 'hold' && other.pairing.val.group === group);
      if (!held) throw new Error(`\`${name}\` arrives on hold group \`${group}\`, and no hold input is in it`);
      if (!sameAnchor(anchor, Anchor.sharedClock)) {
        throw new Error(
          `\`${name}\` arrives on hold group \`${group}\`, whose first picture fixes its offset, ` +
            'so its anchor is the shared clock',
        );
      }
    }
    if (input.stride === 0 || input.stride > input.window) {
      throw new Error(
        `\`${name}\` has a window of ${input.window} and a stride of ${input.stride}: ` +
          'the stride runs from 1 to the window',
      );
    }
    const like = input.accepts.like;
    if (like !== undefined) {
      const other = shape.findInput(like);
      const single = other !== undefined && !other.many && other.kind === input.kind;
      if (!single || !bound.has(like)) {
        throw new Error(`\`${name}\` is conformed to \`${like}\`, which has to be a single bound input of its kind`);
      }
    }
  }
  for (const output of shape.outputs) {
    const name = output.name;
    if (output.format !== undefined && output.format.tag !== output.kind) {
      throw new Error(`output \`${name}\` is ${output.kind} with a ${output.format.tag} format`);
    }
    if (output.format?.tag === 'video' && !output.format.val.pixFmt) {
      throw new Error(`output \`${name}\` has a size and no pixel format`);
    }
    if (output.format === undefined && output.like === undefined && !inputClock) {
      throw new Error(
        `output \`${name}\` takes the clock input's format, and the clock is not an input: give it a format`,
      );
    }
    const followed = output.like?.port === undefined ? undefined : shape.findInput(output.like.port);
    if (followed !== undefined && followed.kind !== output.kind) {
      throw new Error(`output \`${name}\` is ${output.kind} and follows \`${followed.name}\`, which is ${followed.kind}`);
    }
    if (output.row !== undefined && output.row >= shape.relation.length) {
      throw new Error(`output \`${name}\` belongs to relation row ${output.row}, and there are ${shape.relation.length}`);
    }
  }
}

/** The inputs a call binds, each with what the compiler knows of its
 * streams: how many a many port takes, and their rates. */
export class Bound {
  /** `names` bound, one stream each, at rates unknown. */
  constructor(names = []) {
    this.bindings = [];
    for (const name of names) this.bind(name, [undefined]);
  }

  /** The bindings `shape` is handed, in their order. */
  static from(bindings) {
    const bound = new Bound();
    bound.bindings = bindings;
    return bound;
  }

  /** The streams `init` binds, each with its hint, as the adapter shapes
   * the node there. */
  static of(streams) {
    const bound = new Bound();
    for (const stream of streams) {
      const binding = bound.find(stream.port);
      if (binding === undefined) bound.bindings.push({ input: stream.port, streams: [{ ...stream.hint }] });
      else binding.streams.push({ ...stream.hint });
    }
    return bound;
  }

  /** `port` bound to streams at these rates, undefined where unknown, in
   * place of whatever it was bound to. */
  bind(port, rates) {
    const streams = rates.map((rate) => ({ rate }));
    const binding = this.find(port);
    if (binding === undefined) this.bindings.push({ input: port, streams });
    else binding.streams = streams;
    return this;
  }

  /** Every stream of `port` at `rate`; one stream when it was not bound. */
  rate(port, rate) {
    if (!this.has(port)) this.bind(port, [undefined]);
    for (const hint of this.find(port).streams) hint.rate = rate;
    return this;
  }

  find(port) {
    return this.bindings.find((binding) => binding.input === port);
  }

  /** Every input the call binds, in its order. */
  inputs() {
    return this.bindings;
  }

  /** Whether the call binds input `port`. */
  has(port) {
    return this.find(port) !== undefined;
  }

  /** The stream hints bound to `port`; none when the call leaves it out. */
  streams(port) {
    return this.find(port)?.streams ?? [];
  }

  /** How many streams the call binds to `port`. */
  count(port) {
    return this.streams(port).length;
  }

  /** The rate of `port`'s first stream, the only one of a single port. */
  rateOf(port) {
    return this.streams(port)[0]?.rate;
  }
}
