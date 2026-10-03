// A call's params read against the node's JSON schema: checked, and the
// schema's defaults filled in.

/** The schema of a node that takes no params. */
export const NO_PARAMS = '{"type":"object","properties":{},"additionalProperties":false}';

/** `params` read against `schema`: an empty string is `{}`, a param set to
 * null is a param not set, every param is checked against the schema, and
 * the schema's defaults fill in what is not set. Answers the object.
 *
 * The keywords checked are `type`, `enum`, `const`, `minimum`, `maximum`,
 * `exclusiveMinimum`, `exclusiveMaximum`, `minLength`, `maxLength`,
 * `minItems`, `maxItems`, `items`, `properties`, `required` and
 * `additionalProperties`; the compiler checks the call against the whole
 * schema before a node sees it. */
export function readParams(schema, params) {
  let parsedSchema;
  try {
    parsedSchema = JSON.parse(schema);
  } catch (error) {
    throw new Error(`the params schema is not JSON: ${error.message}`);
  }
  const text = (params ?? '').trim();
  let value = {};
  if (text !== '') {
    try {
      value = JSON.parse(text);
    } catch (error) {
      throw new Error(`the params are not JSON: ${error.message}`);
    }
  }
  if (!isObject(value)) throw new Error(`the params are a JSON object, not ${text}`);
  for (const key of Object.keys(value)) {
    if (value[key] === null) delete value[key];
  }
  check(value, parsedSchema, '');
  const properties = parsedSchema.properties;
  if (isObject(properties)) {
    for (const [name, property] of Object.entries(properties)) {
      if (isObject(property) && 'default' in property && !(name in value)) {
        value[name] = JSON.parse(JSON.stringify(property.default));
      }
    }
  }
  return value;
}

function isObject(value) {
  return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function named(path) {
  return path === '' ? 'the params' : `\`${path}\``;
}

function join(path, name) {
  return path === '' ? name : `${path}.${name}`;
}

function shown(value) {
  return JSON.stringify(value);
}

function counted(n, what) {
  return n === 1 ? `1 ${what}` : `${n} ${what}s`;
}

function typeMatches(value, kind) {
  switch (kind) {
    case 'null':
      return value === null;
    case 'boolean':
      return typeof value === 'boolean';
    case 'string':
      return typeof value === 'string';
    case 'array':
      return Array.isArray(value);
    case 'object':
      return isObject(value);
    case 'number':
      return typeof value === 'number';
    case 'integer':
      return Number.isInteger(value);
    default:
      return true;
  }
}

export function deepEqual(a, b) {
  if (a === b) return true;
  if (Array.isArray(a)) {
    return Array.isArray(b) && a.length === b.length && a.every((item, n) => deepEqual(item, b[n]));
  }
  if (isObject(a) && isObject(b)) {
    const keys = Object.keys(a);
    return keys.length === Object.keys(b).length && keys.every((key) => key in b && deepEqual(a[key], b[key]));
  }
  return false;
}

function check(value, schema, path) {
  if (!isObject(schema)) return;
  if ('type' in schema) {
    const kinds = typeof schema.type === 'string'
      ? [schema.type]
      : Array.isArray(schema.type) ? schema.type.filter((kind) => typeof kind === 'string') : [];
    if (kinds.length > 0 && !kinds.some((kind) => typeMatches(value, kind))) {
      throw new Error(`${named(path)} is ${kinds.join(' or ')}, not ${shown(value)}`);
    }
  }
  if (Array.isArray(schema.enum) && !schema.enum.some((option) => deepEqual(option, value))) {
    const listed = schema.enum.map(shown).join(', ');
    throw new Error(`${named(path)} is one of ${listed}, not ${shown(value)}`);
  }
  if ('const' in schema && !deepEqual(schema.const, value)) {
    throw new Error(`${named(path)} is ${shown(schema.const)}, not ${shown(value)}`);
  }
  if (typeof value === 'number') {
    const bound = (key) => (typeof schema[key] === 'number' ? schema[key] : undefined);
    const [min, max] = [bound('minimum'), bound('maximum')];
    const [above, below] = [bound('exclusiveMinimum'), bound('exclusiveMaximum')];
    if (min !== undefined && value < min) throw new Error(`${named(path)} is at least ${min}, not ${shown(value)}`);
    if (max !== undefined && value > max) throw new Error(`${named(path)} is at most ${max}, not ${shown(value)}`);
    if (above !== undefined && value <= above) {
      throw new Error(`${named(path)} is more than ${above}, not ${shown(value)}`);
    }
    if (below !== undefined && value >= below) {
      throw new Error(`${named(path)} is less than ${below}, not ${shown(value)}`);
    }
  }
  const count = (key) => (Number.isInteger(schema[key]) && schema[key] >= 0 ? schema[key] : undefined);
  if (typeof value === 'string') {
    const chars = [...value].length;
    const [min, max] = [count('minLength'), count('maxLength')];
    if (min !== undefined && chars < min) throw new Error(`${named(path)} is at least ${counted(min, 'character')}`);
    if (max !== undefined && chars > max) throw new Error(`${named(path)} is at most ${counted(max, 'character')}`);
  }
  if (Array.isArray(value)) {
    const [min, max] = [count('minItems'), count('maxItems')];
    if (min !== undefined && value.length < min) {
      throw new Error(`${named(path)} holds at least ${counted(min, 'item')}`);
    }
    if (max !== undefined && value.length > max) {
      throw new Error(`${named(path)} holds at most ${counted(max, 'item')}`);
    }
    if ('items' in schema) {
      value.forEach((item, n) => check(item, schema.items, `${path === '' ? 'params' : path}[${n}]`));
    }
  }
  if (isObject(value)) {
    const properties = isObject(schema.properties) ? schema.properties : undefined;
    if (Array.isArray(schema.required)) {
      for (const name of schema.required) {
        if (typeof name === 'string' && !(name in value)) throw new Error(`${named(join(path, name))} is required`);
      }
    }
    const closed = schema.additionalProperties === false;
    for (const [name, field] of Object.entries(value)) {
      if (properties !== undefined && Object.hasOwn(properties, name)) {
        check(field, properties[name], join(path, name));
      } else if (closed) {
        const known = properties === undefined ? [] : Object.keys(properties).map((key) => `\`${key}\``);
        const takes = known.length === 0 ? 'takes none' : `takes ${known.join(', ')}`;
        const whose = path === '' ? `the node ${takes}` : `${named(path)} ${takes}`;
        throw new Error(`${named(join(path, name))} is not a param here; ${whose}`);
      }
    }
  }
}
