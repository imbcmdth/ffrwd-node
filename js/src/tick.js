import { parse } from './rows.js';

/** One call's inputs, held by the host for exactly that call. Streams are
 * named by the ids `init` gave them; an id it did not give, or an index past
 * a stream's frames, is a fault that stops the run.
 *
 * It reads from a source: the host's tick in a module, a `MockTick` in a
 * test. */
export class Tick {
  constructor(source, ports, timing) {
    this.source = source;
    this.ports = ports;
    this.timing = timing;
    this.refused = undefined;
  }

  /** The tick's time in `timeBase()`: an input clock's first frame this
   * call, a rate clock's tick number, a self-clocked node's microseconds
   * since its first call. */
  pts() {
    return this.source.pts();
  }

  /** The tick's number in the run, from 0, counted over every instance of
   * the node. A node that numbers things by frame counts with it and stays
   * pure. */
  ordinal() {
    return this.source.ordinal();
  }

  /** The clock input's time base, the inverse of a rate clock's rate, or
   * microseconds. */
  timeBase() {
    return this.source.timeBase();
  }

  /** The tick's time in seconds. */
  seconds() {
    return this.timeBase().seconds(this.pts());
  }

  /** Whether this is the instance's final call, which happens exactly once. */
  last() {
    return this.source.last();
  }

  /** The streams bound to input `port`, in `init`'s order. */
  streams(port) {
    return this.source.streams(port);
  }

  /** The first stream bound to input `port`: the only one of a single port,
   * undefined for an optional port the call left out. */
  stream(port) {
    return this.streams(port)[0];
  }

  /** The stream as the host knows it this tick, time base included. */
  info(id) {
    return this.source.info(id);
  }

  /** A hold input's feed as it stands this tick; undefined while nothing
   * shows. */
  feed(id) {
    return this.source.feed(id);
  }

  /** A hold input's feeds that ended since this instance's previous call,
   * oldest first, each with `ends` set to the last tick it showed on. */
  endedFeeds(id) {
    return this.source.endedFeeds(id);
  }

  /** The frames this tick hands on stream `id`, oldest first. */
  frames(id) {
    return this.source.frames(id);
  }

  /** The newest frame this tick hands on stream `id`: the only one a window
   * of one, a hold input or an audio input hands. */
  frame(id) {
    return this.frames(id).at(-1);
  }

  /** Frame `index`'s bytes, copied on demand. An input read for its timing
   * alone has none: the call gets an empty array, and ends the run with the
   * port named once it returns, where the host would fault. */
  fetch(id, index) {
    if (this.timing.has(id)) {
      this.refused ??= id;
      return new Uint8Array(0);
    }
    return this.source.fetch(id, index);
  }

  /** The messages this tick hands on data stream `id`, in pts order. */
  messages(id) {
    return this.source.messages(id);
  }

  /** The packets this tick hands on packets stream `id`, in decode order. */
  packets(id) {
    return this.source.packets(id);
  }

  /** The rows of the ticks this instance did not process, on a state input.
   * They are folded for the node, so it rarely reads them itself. */
  earlierRows(id) {
    return this.source.earlierRows(id);
  }

  /** Every row this tick hands on stream `id`, each parsed: a data stream's
   * messages, or the rows riding a frame stream's frames. */
  rows(id) {
    try {
      const messages = this.messages(id);
      if (messages.length > 0) return messages.map((message) => parse(new TextDecoder().decode(message.data)));
      return this.frames(id).flatMap((frame) => frame.rows.map(parse));
    } catch (error) {
      throw new Error(`on \`${this.port(id)}\`: ${error.message}`);
    }
  }

  /** The input port stream `id` is bound to. */
  port(id) {
    return this.ports.get(id) ?? '?';
  }
}
