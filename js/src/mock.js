// A node's calls on the host, for its unit tests: a `Harness` runs the same
// call sequence a module does, on a `MockTick` built by hand.

import { Runner } from './node.js';
import { Bound } from './shape.js';
import { Rational } from './time.js';

/** One tick's inputs, built by hand. An id it was not told of, or an index
 * past a stream's frames, throws, as the host stops the run. */
export class MockTick {
  /** A tick at `pts` of a clock counted in `timeBase`, the run's first. */
  constructor(pts, timeBase) {
    this.at = pts;
    this.number = 0;
    this.clock = timeBase;
    this.final = false;
    this.bound = [];
    this.infos = new Map();
    this.frameData = new Map();
    this.messageData = new Map();
    this.packetData = new Map();
    this.feeds = new Map();
    this.endedData = new Map();
    this.earlierData = new Map();
  }

  /** The tick's number in the run: what a worker handed every other tick
   * sees on its own. */
  ordinal(ordinal) {
    if (ordinal === undefined) return this.number;
    this.number = ordinal;
    return this;
  }

  /** `stream` bound on its port. */
  bind(stream) {
    this.bound.push([stream.port, stream.id]);
    this.infos.set(stream.id, stream.info);
    return this;
  }

  /** The instance's final call. */
  last() {
    this.final = true;
    return this;
  }

  /** A frame of `data` at `pts` on stream `id`, after the ones already
   * there, with a duration and rows riding it when given. */
  frame(id, pts, data, { duration, rows = [] } = {}) {
    const frames = list(this.frameData, id);
    frames.push({ frame: { pts, index: frames.length, duration, rows: [...rows] }, data });
    return this;
  }

  /** A message on data stream `id`: bytes, or a string sent as UTF-8. */
  message(id, pts, data) {
    const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
    list(this.messageData, id).push({ pts, data: bytes });
    return this;
  }

  /** `row` as a JSON message on data stream `id`. */
  row(id, pts, row) {
    return this.message(id, pts, JSON.stringify(row));
  }

  packet(id, packet) {
    list(this.packetData, id).push(packet);
    return this;
  }

  /** Hold input `id`'s feed as it stands on this tick. */
  feed(id, feed) {
    this.feeds.set(id, feed);
    return this;
  }

  /** A feed of hold input `id` that ended since the instance's previous
   * call, after the ones already there. */
  ended(id, feed) {
    list(this.endedData, id).push(feed);
    return this;
  }

  /** Rows a state input received on a tick this instance did not process. */
  earlier(id, pts, rows) {
    list(this.earlierData, id).push({ pts, rows: [...rows] });
    return this;
  }

  source() {
    const known = (id) => {
      if (!this.infos.has(id)) throw new Error(`stream ${id} is not bound on this tick`);
    };
    const get = (map, id) => {
      known(id);
      return map.get(id) ?? [];
    };
    return {
      pts: () => this.at,
      ordinal: () => this.number,
      timeBase: () => this.clock,
      last: () => this.final,
      streams: (port) => this.bound.filter(([name]) => name === port).map(([, id]) => id),
      info: (id) => {
        known(id);
        return this.infos.get(id);
      },
      feed: (id) => {
        known(id);
        return this.feeds.get(id);
      },
      endedFeeds: (id) => [...get(this.endedData, id)],
      frames: (id) => get(this.frameData, id).map(({ frame }) => ({ ...frame, rows: [...frame.rows] })),
      fetch: (id, index) => {
        const found = get(this.frameData, id)[index];
        if (found === undefined) throw new Error(`stream ${id} has no frame ${index} on this tick`);
        return found.data.slice();
      },
      messages: (id) => [...get(this.messageData, id)],
      packets: (id) => [...get(this.packetData, id)],
      earlierRows: (id) => [...get(this.earlierData, id)],
    };
  }
}

function list(map, id) {
  if (!map.has(id)) map.set(id, []);
  return map.get(id);
}

/** A node opened on the host: `shape` and `init` as the host calls them,
 * every output latched, and ticks that come with the bound streams in place,
 * numbered from 0 on. `node` is what `defineNode` answered, or the
 * definition handed to it. */
export class Harness {
  constructor(node, params, bound) {
    const shape = Runner.shape(node, params, Bound.of(bound));
    const latched = shape.outputs.map((output) => output.name);
    this.runner = Runner.init(node, bound, latched, params);
    this.bound = bound;
    this.next = 0;
    const clock = this.runner.shape().clock;
    if (clock?.tag === 'input') this.clockBase = bound.find((stream) => stream.port === clock.val)?.info.timeBase;
    else if (clock?.tag === 'rate') this.clockBase = clock.val.inverse();
    else if (clock?.tag === 'self-clocked') this.clockBase = Rational.MICROS;
  }

  /** The clock's time base, for a node whose clock is another input's rate. */
  clock(timeBase) {
    this.clockBase = timeBase;
    return this;
  }

  /** A tick at `pts` on the clock, every bound stream in place, numbered one
   * past the last this harness processed. */
  tick(pts) {
    if (this.clockBase === undefined) {
      throw new Error("the clock's time base is the rate of an input; give it with `clock`");
    }
    const tick = new MockTick(pts, this.clockBase).ordinal(this.next);
    for (const stream of this.bound) tick.bind(stream);
    return tick;
  }

  /** One `process` call. */
  process(tick) {
    this.next = tick.ordinal() + 1;
    return this.runner.process(tick.source());
  }

  setParams(params) {
    this.runner.setParams(params);
  }

  /** The instance `init` answered. */
  node() {
    return this.runner.instance;
  }

  /** The instance's shape, as the adapter resolved it at `init`. */
  shape() {
    return this.runner.shape();
  }
}
