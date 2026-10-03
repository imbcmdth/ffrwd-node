// Rows: a schema from a row's description, per-tick spans named by when they
// started, and cues.

/** One JSON row read as an object. */
export function parse(row) {
  try {
    return JSON.parse(row);
  } catch (error) {
    throw new Error(`the row ${row} does not read: ${error.message}`);
  }
}

/** The JSON schema of a row from a description of its fields. Each field is
 * named by its JSON type, `'number'`, `'integer'`, `'string'` or
 * `'boolean'`; an array of one description is an array of those; an object
 * is a nested row; `null` takes any type and is not required. Every other
 * field is required, and fields the description leaves out are allowed, as
 * a reader naming only the fields it reads wants.
 *
 *     schemaOf({ ...SPAN, x: 'integer', label: 'string', hidden: null })
 */
export function schemaOf(description) {
  return JSON.stringify(schemaFor(description));
}

// Keys in sorted order at every level, as serde_json writes its maps, so a
// schema reads the same byte for byte from every SDK.
function schemaFor(description) {
  if (description === null || description === undefined) return {};
  if (typeof description === 'string') return { type: description };
  if (Array.isArray(description)) {
    return description.length > 0 ? { items: schemaFor(description[0]), type: 'array' } : { type: 'array' };
  }
  const properties = {};
  const required = [];
  for (const name of Object.keys(description).sort()) {
    const field = description[name];
    properties[name] = schemaFor(field);
    if (field !== null && field !== undefined) required.push(name);
  }
  return { properties, required, type: 'object' };
}

/** The fields a span writes into a row, for `schemaOf`. */
export const SPAN = Object.freeze({ start_t: 'number', id: 'integer' });

/** A caption: text from `start_t` to `end_t`, in seconds. What a query's
 * `cue[]` is. */
export class Cue {
  constructor(start_t, end_t, text) {
    this.start_t = start_t;
    this.end_t = end_t;
    this.text = text;
  }

  /** Whether the cue is showing at `t` seconds: from its start up to, not
   * including, its end. */
  covers(t) {
    return this.start_t <= t && t < this.end_t;
  }
}

/** The description of a cue's row, for `schemaOf`. */
export const CUE = Object.freeze({ start_t: 'number', end_t: 'number', text: 'string' });

/** Cues kept from the tick they arrive on until they end. */
export class Cues {
  constructor() {
    this.held = [];
  }

  /** Keeps `cue`, in start order. */
  add(cue) {
    let at = 0;
    while (at < this.held.length && this.held[at].start_t <= cue.start_t) at += 1;
    this.held.splice(at, 0, cue);
  }

  /** The cues showing at `t` seconds, oldest first. */
  at(t) {
    return this.held.filter((cue) => cue.covers(t));
  }

  /** Forgets every cue that ended by `t` seconds. */
  dropEnded(t) {
    this.held = this.held.filter((cue) => cue.end_t > t);
  }

  get size() {
    return this.held.length;
  }
}

/** One span a key is seen across: `start_t`, the seconds of the tick it was
 * first seen on, and `id`, how many spans began before it. Spread into a row
 * (`{ ...span, x }`) it writes both, and `age`, how many ticks old it is,
 * stays out. */
export class Span {
  constructor(start_t, id, age = 0) {
    this.start_t = start_t;
    this.id = id;
    Object.defineProperty(this, 'age', { value: age, enumerable: false, writable: true });
  }
}

/** Which span a sighting belongs to, for a node writing a row per tick while
 * something lasts. Call `tick` once a tick, then `see` for each thing seen.
 *
 * A span ends when its key goes unseen for more than `gap` ticks (0 by
 * default), or once it is `longest` ticks old; the next sighting starts a new
 * one. Keys are compared with `===`. */
export class Spans {
  constructor() {
    this.opened = [];
    this.gapTicks = 0;
    this.longestTicks = undefined;
    this.now = undefined;
    this.started = 0;
  }

  /** How many ticks in a row a key may go unseen and its span go on. */
  gap(ticks) {
    this.gapTicks = ticks;
    return this;
  }

  /** The most ticks one span lasts. */
  longest(ticks) {
    this.longestTicks = Math.max(1, ticks);
    return this;
  }

  /** Starts the tick at `t` seconds, ending the spans that ran out. */
  tick(t) {
    const tick = this.now === undefined ? 0 : this.now.tick + 1;
    this.now = { tick, t };
    this.opened = this.opened.filter((open) => {
      const unseen = tick - open.seen - 1;
      const age = tick - open.started;
      return unseen <= this.gapTicks && (this.longestTicks === undefined || age < this.longestTicks);
    });
  }

  /** A sighting of `key` on this tick: the span it belongs to, started here
   * when none is open for it. */
  see(key) {
    if (this.now === undefined) this.now = { tick: 0, t: 0 };
    const { tick, t } = this.now;
    const open = this.opened.find((candidate) => candidate.key === key);
    if (open !== undefined) {
      open.seen = tick;
      return new Span(open.span.start_t, open.span.id, tick - open.started);
    }
    const span = new Span(t, this.started);
    this.started += 1;
    this.opened.push({ key, span, started: tick, seen: tick });
    return span;
  }

  /** How many spans are open. */
  open() {
    return this.opened.length;
  }
}
