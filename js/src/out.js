import { Rational } from './time.js';

/** What one tick produced: emissions as `{ port, payload }`, a payload
 * `{ tag, val }` of `frame`, `same`, `message` or `packet`, every pts in the
 * port's time base. */
export class Emitted {
  constructor() {
    this.items = [];
    this.reports = [];
    this.finished = false;
  }

  /** The messages that left on `port`, as `[pts, text]`. */
  messages(port) {
    return this.on(port)
      .filter((payload) => payload.tag === 'message')
      .map(({ val }) => [val.pts, new TextDecoder().decode(val.data)]);
  }

  /** The payloads that left on `port`, in order. */
  on(port) {
    return this.items.filter((item) => item.port === port).map((item) => item.payload);
  }
}

/** Where a tick's emissions go. Each one is checked as it is made: the port
 * is one the shape declares and of the payload's kind, and its pts never go
 * back on that port, within a call or across calls, nor a packet's dts. */
export class Out {
  constructor(shape, timing = new Map()) {
    this.ports = shape.outputs.map((output) => ({
      name: output.name,
      kind: output.kind,
      timeBase: output.timeBase,
      lastPts: undefined,
      lastDts: undefined,
    }));
    this.clock = Rational.MICROS;
    this.emitted = new Emitted();
    this.timing = timing;
  }

  begin(clock) {
    this.clock = clock;
  }

  take() {
    const emitted = this.emitted;
    this.emitted = new Emitted();
    return emitted;
  }

  port(name, kinds) {
    const port = this.ports.find((candidate) => candidate.name === name);
    if (port === undefined) throw new Error(`\`${name}\` is not an output of this node`);
    if (!kinds.includes(port.kind)) throw new Error(`\`${name}\` is a ${port.kind} output`);
    return port;
  }

  stamp(name, kinds, pts) {
    const port = this.port(name, kinds);
    if (port.lastPts !== undefined && pts < port.lastPts) {
      throw new Error(`\`${name}\` would go back from pts ${port.lastPts} to ${pts}; a port's pts never decrease`);
    }
    port.lastPts = pts;
  }

  push(port, tag, val) {
    this.emitted.items.push({ port, payload: { tag, val } });
  }

  /** The time base `port` is counted in: its own, or the clock's. */
  timeBase(port) {
    const found = this.ports.find((candidate) => candidate.name === port);
    if (found === undefined) throw new Error(`\`${port}\` is not an output of this node`);
    return found.timeBase ?? this.clock;
  }

  /** `seconds` as a pts on `port`. */
  pts(port, seconds) {
    return this.timeBase(port).pts(seconds);
  }

  /** The last pts that left on `port`, which the next may not be under. */
  last(port) {
    return this.ports.find((candidate) => candidate.name === port)?.lastPts;
  }

  /** New bytes on a video or audio port: one picture tightly packed, or a
   * run of interleaved samples. */
  frame(port, pts, duration, data) {
    this.stamp(port, ['video', 'audio'], pts);
    this.push(port, 'frame', { pts, duration, data });
  }

  /** Frame `index` of stream `id` leaving on `port` uncopied, at `pts`. */
  same(port, pts, duration, id, index) {
    const input = this.timing.get(id);
    if (input !== undefined) {
      throw new Error(
        `a frame of \`${input}\` cannot leave on \`${port}\`: \`${input}\` is read for its timing alone, ` +
          'and the host carries none of its bytes',
      );
    }
    this.stamp(port, ['video', 'audio'], pts);
    this.push(port, 'same', { pts, duration, id, index });
  }

  /** `frame` of stream `id` leaving on `port` unchanged at its own pts and
   * duration. */
  pass(port, id, frame) {
    this.same(port, frame.pts, frame.duration, id, frame.index);
  }

  /** One message on a data port: bytes, or a string sent as UTF-8. */
  message(port, pts, data) {
    this.stamp(port, ['data'], pts);
    const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
    this.push(port, 'message', { pts, data: bytes });
  }

  /** `row` as one JSON message on `port` at `pts`. */
  row(port, pts, row) {
    this.message(port, pts, JSON.stringify(row));
  }

  /** Each of `rows` as a message on `port` at `pts`. */
  rows(port, pts, rows) {
    for (const row of rows) this.row(port, pts, row);
  }

  /** `cue` on `port`, stamped at its start. */
  cue(port, cue) {
    this.row(port, this.pts(port, cue.start_t), cue);
  }

  /** A progress mark: nothing more will leave on `port` stamped before
   * `pts`. Never delivered. */
  progress(port, pts) {
    this.message(port, pts, new Uint8Array(0));
  }

  /** One packet on a packets port, in decode order: its dts never
   * decreases. */
  packet(port, packet) {
    const found = this.port(port, ['packets']);
    if (packet.dts !== undefined) {
      if (found.lastDts !== undefined && packet.dts < found.lastDts) {
        throw new Error(`\`${port}\` would go back from dts ${found.lastDts} to ${packet.dts}; packets keep decode order`);
      }
      found.lastDts = packet.dts;
    }
    this.push(port, 'packet', packet);
  }

  /** One row for the run's rows output, as a sink writes them. */
  report(row) {
    this.emitted.reports.push(JSON.stringify(row));
  }

  /** Nothing more will leave: the host makes the last call and ends every
   * output. A node on an input clock ends with it and need not say so. */
  finish() {
    this.emitted.finished = true;
  }
}
