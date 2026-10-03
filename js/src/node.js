import { hostBinding, hostBoundStream, hostEmitted, hostShape, HostTick } from './glue.js';
import { Out } from './out.js';
import { deepEqual, NO_PARAMS, readParams } from './params.js';
import { parse } from './rows.js';
import { Bound } from './shape.js';
import { Tick } from './tick.js';

const DEFINITION = Symbol.for('ffrwd.node.definition');

/** What `init` is handed: the streams bound to the node's inputs and the
 * outputs the query reads. */
export class Init {
  constructor(streams, latched, shape) {
    this.boundStreams = streams;
    this.latchedPorts = latched;
    this.resolved = shape;
  }

  /** Every bound stream: ports in the shape's order, a port's streams in the
   * order the query named them. */
  all() {
    return this.boundStreams;
  }

  /** The streams bound to input `port`. */
  streams(port) {
    return this.boundStreams.filter((stream) => stream.port === port);
  }

  /** The one stream bound to input `port`, or undefined when the call left
   * it out. */
  optional(port) {
    return this.boundStreams.find((stream) => stream.port === port);
  }

  /** The one stream bound to required input `port`. */
  stream(port) {
    const stream = this.optional(port);
    if (stream === undefined) throw new Error(`no stream is bound to \`${port}\``);
    return stream;
  }

  /** Whether the query reads output `port`; one it does not may be left
   * unmade. */
  latched(port) {
    return this.latchedPorts.includes(port);
  }

  /** The instance's shape, resolved for what the call binds. */
  shape() {
    return this.resolved;
  }
}

/** One row arriving on a state input: its port and stream, when it arrived
 * in the stream's time base and in seconds, and the row's JSON. */
export class StateRow {
  constructor(port, id, pts, seconds, json) {
    this.port = port;
    this.id = id;
    this.pts = pts;
    this.seconds = seconds;
    this.json = json;
  }

  /** The row, parsed. */
  row() {
    try {
      return parse(this.json);
    } catch (error) {
      throw new Error(`on \`${this.port}\`: ${error.message}`);
    }
  }
}

function definitionOf(node) {
  return node[DEFINITION] ?? node;
}

function filled(definition) {
  for (const field of ['name', 'version', 'shape', 'init']) {
    if (definition[field] === undefined) throw new Error(`a node definition has no \`${field}\``);
  }
  return {
    paramsSchema: NO_PARAMS,
    rowsSchema: '',
    rowsLanguage: [],
    ...definition,
  };
}

/** The call sequence around a node, the same in a module and in a test:
 * params read against the schema, the shape resolved, state rows folded,
 * emissions checked. */
export class Runner {
  static describe(node) {
    const definition = filled(definitionOf(node));
    return {
      name: definition.name,
      version: definition.version,
      paramsSchema: definition.paramsSchema,
      rowsSchema: definition.rowsSchema,
      rowsLanguage: [...definition.rowsLanguage],
    };
  }

  /** The shape for `params` and the inputs `bound` binds, as the host is
   * handed it. `bound` is a `Bound`, or the names of the inputs bound. */
  static shape(node, params, bound) {
    const definition = filled(definitionOf(node));
    const read = readParams(definition.paramsSchema, params);
    const hints = bound instanceof Bound ? bound : new Bound(bound);
    return definition.shape(read, hints).resolve(hints);
  }

  /** Opens an instance on `bound`. */
  static init(node, bound, latched, params) {
    const definition = filled(definitionOf(node));
    const read = readParams(definition.paramsSchema, params);
    const hints = Bound.of(bound);
    const shape = definition.shape(read, hints).resolve(hints);
    const ports = new Map(bound.map((stream) => [stream.id, stream.port]));
    const timing = new Map(
      bound
        .filter((stream) => shape.findInput(stream.port)?.accepts.wants === 'timing')
        .map((stream) => [stream.id, stream.port]),
    );
    const state = bound
      .filter((stream) => shape.findInput(stream.port)?.rows === 'state')
      .map((stream) => [stream.port, stream.id]);
    const instance = definition.init(read, new Init(bound, latched, shape));
    if (instance === null || typeof instance !== 'object' || typeof instance.process !== 'function') {
      throw new Error(`${definition.name}'s init answers an object with a \`process\``);
    }
    return new Runner(definition, instance, shape, read, ports, state, timing);
  }

  constructor(definition, instance, shape, params, ports, state, timing) {
    this.definition = definition;
    this.instance = instance;
    this.resolved = shape;
    this.params = params;
    this.ports = ports;
    this.state = state;
    this.timing = timing;
    this.out = new Out(shape, timing);
  }

  /** New params between ticks; equal ones are taken without asking the
   * node, and a node with no `setParams` refuses the rest. */
  setParams(params) {
    const read = readParams(this.definition.paramsSchema, params);
    if (deepEqual(read, this.params)) return;
    if (typeof this.instance.setParams !== 'function') {
      throw new Error(`${this.definition.name} cannot change its params while it runs`);
    }
    this.instance.setParams(read);
    this.params = read;
  }

  /** One tick read from `source`. */
  process(source) {
    this.out.take();
    const tick = new Tick(source, this.ports, new Set(this.timing.keys()));
    this.out.begin(tick.timeBase());
    this.fold(tick);
    let failed;
    try {
      this.instance.process(tick, this.out);
    } catch (error) {
      failed = error;
    }
    if (tick.refused !== undefined) {
      throw new Error(
        `${this.definition.name} fetched a frame of \`${tick.port(tick.refused)}\`, which it reads for its ` +
          'timing alone: the host carries none of its bytes',
      );
    }
    if (failed !== undefined) throw failed;
    return this.out.take();
  }

  fold(tick) {
    const fold = (row) => {
      if (typeof this.instance.fold !== 'function') {
        throw new Error(
          `${this.definition.name} declares \`${row.port}\` as state, so it folds that input's rows: give it a \`fold\``,
        );
      }
      this.instance.fold(row);
    };
    for (const [port, id] of this.state) {
      const timeBase = tick.info(id).timeBase;
      for (const timed of tick.earlierRows(id)) {
        for (const json of timed.rows) fold(new StateRow(port, id, timed.pts, timeBase.seconds(timed.pts), json));
      }
    }
    for (const [port, id] of this.state) {
      const timeBase = tick.info(id).timeBase;
      const arrived = [];
      for (const message of tick.messages(id)) {
        let json;
        try {
          json = new TextDecoder('utf-8', { fatal: true }).decode(message.data);
        } catch {
          throw new Error(`a row on \`${port}\` is not utf-8`);
        }
        arrived.push([message.pts, json]);
      }
      for (const frame of tick.frames(id)) {
        for (const json of frame.rows) arrived.push([frame.pts, json]);
      }
      for (const [pts, json] of arrived) fold(new StateRow(port, id, pts, timeBase.seconds(pts), json));
    }
  }

  /** The instance's shape, resolved. */
  shape() {
    return this.resolved;
  }
}

function message(error) {
  return error instanceof Error ? error.message : String(error);
}

/** The `node` export of `ffrwd:av/node@0.19.1` for a node defined as
 *
 *     {
 *       name, version,
 *       paramsSchema,          // JSON schema of the params; none by default
 *       rowsSchema,            // of one row `out.report` writes
 *       rowsLanguage,          // ordered param names
 *       shape(params, bound),  // a Shape
 *       init(params, init),    // the instance: { process(tick, out),
 *                              //   setParams(params)?, fold(row)? }
 *     }
 *
 * A module exports what this answers as `node`. Anything a call throws ends
 * the run with its message. */
export function defineNode(definition) {
  const node = filled(definition);
  let runner;
  const guarded = (call) => {
    try {
      return call();
    } catch (error) {
      throw message(error);
    }
  };
  return {
    [DEFINITION]: node,
    describe() {
      const meta = Runner.describe(node);
      return {
        ...meta,
        pixelFormats: [],
        sampleFormats: [],
        sampleRates: new Uint32Array(0),
        channelCounts: new Uint32Array(0),
      };
    },
    shape(params, bound) {
      return guarded(() => hostShape(Runner.shape(node, params, Bound.from(bound.map(hostBinding)))));
    },
    init(bound, latched, params) {
      guarded(() => {
        runner = undefined;
        runner = Runner.init(node, bound.map(hostBoundStream), latched, params);
      });
    },
    setParams(params) {
      guarded(() => {
        if (runner === undefined) throw new Error(`${node.name} was called before init`);
        runner.setParams(params);
      });
    },
    process(tick) {
      return guarded(() => {
        if (runner === undefined) throw new Error(`${node.name} was called before init`);
        return hostEmitted(runner.process(new HostTick(tick)));
      });
    },
  };
}
