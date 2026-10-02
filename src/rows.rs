//! Rows: typed in and out, a schema from a row type, per-tick spans named by
//! when they started, and cues.

use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

/// One JSON row read as a `T`.
pub fn parse<T: DeserializeOwned>(row: &str) -> Result<T, String> {
    serde_json::from_str(row).map_err(|err| format!("the row {row} does not read: {err}"))
}

/// The JSON schema of a row type, from what `T::default()` serializes to: a
/// float is `number`, an integer `integer`, then `string`, `boolean`,
/// `array` and nested `object`s, each required. A field that serializes to
/// null, as an `Option` does, takes any type and is not required; one that
/// does not serialize at all is not in the schema. Other fields are allowed,
/// as a reader naming only the fields it reads wants.
pub fn schema_of<T: Default + Serialize>() -> String {
    let value = serde_json::to_value(T::default()).unwrap_or(Value::Null);
    schema_for(&value).to_string()
}

fn schema_for(value: &Value) -> Value {
    let mut schema = Map::new();
    match value {
        Value::Null => {}
        Value::Bool(_) => {
            schema.insert("type".into(), "boolean".into());
        }
        Value::Number(number) => {
            let kind = if number.is_f64() { "number" } else { "integer" };
            schema.insert("type".into(), kind.into());
        }
        Value::String(_) => {
            schema.insert("type".into(), "string".into());
        }
        Value::Array(items) => {
            schema.insert("type".into(), "array".into());
            if let Some(first) = items.first() {
                schema.insert("items".into(), schema_for(first));
            }
        }
        Value::Object(fields) => {
            schema.insert("type".into(), "object".into());
            let properties: Map<String, Value> = fields
                .iter()
                .map(|(name, field)| (name.clone(), schema_for(field)))
                .collect();
            let required: Vec<Value> = fields
                .iter()
                .filter(|(_, field)| !field.is_null())
                .map(|(name, _)| Value::String(name.clone()))
                .collect();
            schema.insert("properties".into(), Value::Object(properties));
            schema.insert("required".into(), Value::Array(required));
        }
    }
    Value::Object(schema)
}

/// A caption: text from `start_t` to `end_t`, in seconds. What a query's
/// `cue[]` is.
#[derive(Clone, Debug, Default, PartialEq, Serialize, Deserialize)]
pub struct Cue {
    pub start_t: f64,
    pub end_t: f64,
    pub text: String,
}

impl Cue {
    pub fn new(start_t: f64, end_t: f64, text: impl Into<String>) -> Cue {
        Cue {
            start_t,
            end_t,
            text: text.into(),
        }
    }

    /// Whether the cue is showing at `t` seconds: from its start up to, not
    /// including, its end.
    pub fn covers(&self, t: f64) -> bool {
        self.start_t <= t && t < self.end_t
    }
}

/// Cues kept from the tick they arrive on until they end: what a node that
/// shows cues holds between ticks.
#[derive(Clone, Debug, Default)]
pub struct Cues {
    held: Vec<Cue>,
}

impl Cues {
    pub fn new() -> Cues {
        Cues::default()
    }

    /// Keeps `cue`, in start order.
    pub fn add(&mut self, cue: Cue) {
        let at = self
            .held
            .partition_point(|held| held.start_t <= cue.start_t);
        self.held.insert(at, cue);
    }

    /// The cues showing at `t` seconds, oldest first.
    pub fn at(&self, t: f64) -> impl Iterator<Item = &Cue> {
        self.held.iter().filter(move |cue| cue.covers(t))
    }

    /// Forgets every cue that ended by `t` seconds.
    pub fn drop_ended(&mut self, t: f64) {
        self.held.retain(|cue| cue.end_t > t);
    }

    pub fn len(&self) -> usize {
        self.held.len()
    }

    pub fn is_empty(&self) -> bool {
        self.held.is_empty()
    }
}

/// One span a key is seen across, named by when it started.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Span {
    /// The seconds of the tick it was first seen on: its id in the rows.
    pub start_t: f64,
    /// How many spans began before this one.
    pub number: u64,
    /// How many ticks old it is: 0 on the tick it starts.
    pub age: u64,
}

struct Open<K> {
    key: K,
    span: Span,
    started: u64,
    seen: u64,
}

/// Which span a sighting belongs to, for a node writing a row per tick while
/// something lasts, each row carrying the `start_t` of its span. Call
/// [`Spans::tick`] once a tick, then [`Spans::see`] for each thing seen on
/// it.
///
/// A span ends when its key goes unseen for more than `gap` ticks (0 by
/// default: one tick unseen ends it), or once it is `longest` ticks old; the
/// next sighting starts a new one.
pub struct Spans<K> {
    open: Vec<Open<K>>,
    gap: u64,
    longest: Option<u64>,
    now: Option<(u64, f64)>,
    started: u64,
}

impl<K: PartialEq> Default for Spans<K> {
    fn default() -> Spans<K> {
        Spans::new()
    }
}

impl<K: PartialEq> Spans<K> {
    pub fn new() -> Spans<K> {
        Spans {
            open: Vec::new(),
            gap: 0,
            longest: None,
            now: None,
            started: 0,
        }
    }

    /// How many ticks in a row a span's key may go unseen and the span still
    /// go on.
    pub fn gap(mut self, ticks: u64) -> Spans<K> {
        self.gap = ticks;
        self
    }

    /// The most ticks one span lasts; the tick after its last starts a new
    /// span for a key still seen.
    pub fn longest(mut self, ticks: u64) -> Spans<K> {
        self.longest = Some(ticks.max(1));
        self
    }

    /// Starts the tick at `t` seconds, ending the spans that ran out.
    pub fn tick(&mut self, t: f64) {
        let tick = self.now.map_or(0, |(tick, _)| tick + 1);
        self.now = Some((tick, t));
        let (gap, longest) = (self.gap, self.longest);
        self.open.retain(|open| {
            let unseen = tick - open.seen - 1;
            let age = tick - open.started;
            unseen <= gap && longest.is_none_or(|longest| age < longest)
        });
    }

    /// A sighting of `key` on this tick: the span it belongs to, started
    /// here when none is open for it.
    pub fn see(&mut self, key: K) -> Span {
        let (tick, t) = self.now.unwrap_or((0, 0.0));
        if self.now.is_none() {
            self.now = Some((0, 0.0));
        }
        if let Some(open) = self.open.iter_mut().find(|open| open.key == key) {
            open.seen = tick;
            open.span.age = tick - open.started;
            return open.span;
        }
        let span = Span {
            start_t: t,
            number: self.started,
            age: 0,
        };
        self.started += 1;
        self.open.push(Open {
            key,
            span,
            started: tick,
            seen: tick,
        });
        span
    }

    /// How many spans are open.
    pub fn open(&self) -> usize {
        self.open.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Debug, Default, Serialize, Deserialize)]
    struct Spot {
        start_t: f64,
        id: u64,
        x: u32,
        label: String,
        hidden: Option<bool>,
        vector: Vec<f32>,
    }

    #[test]
    fn a_schema_from_a_row_type() {
        let schema: Value = serde_json::from_str(&schema_of::<Spot>()).unwrap();
        let properties = &schema["properties"];
        assert_eq!(properties["start_t"]["type"], "number");
        assert_eq!(properties["id"]["type"], "integer");
        assert_eq!(properties["x"]["type"], "integer");
        assert_eq!(properties["label"]["type"], "string");
        assert_eq!(properties["hidden"], serde_json::json!({}));
        assert_eq!(properties["vector"]["type"], "array");
        let required: Vec<&str> = schema["required"]
            .as_array()
            .unwrap()
            .iter()
            .map(|name| name.as_str().unwrap())
            .collect();
        assert!(required.contains(&"start_t") && !required.contains(&"hidden"));
    }

    #[test]
    fn rows_parse_and_say_which_did_not() {
        let spot: Spot = parse(r#"{"start_t":1.5,"id":2,"x":3,"label":"a","vector":[]}"#).unwrap();
        assert_eq!((spot.start_t, spot.id, spot.x), (1.5, 2, 3));
        let err = parse::<Spot>(r#"{"start_t":"soon"}"#).unwrap_err();
        assert!(err.contains("soon"), "{err}");
    }

    #[test]
    fn a_span_runs_while_its_key_is_seen() {
        let mut spans = Spans::new();
        let mut starts = Vec::new();
        for (n, seen) in [true, true, false, true, true].into_iter().enumerate() {
            spans.tick(n as f64);
            if seen {
                starts.push(spans.see("mark").start_t);
            }
        }
        assert_eq!(starts, [0.0, 0.0, 3.0, 3.0]);
    }

    #[test]
    fn a_gap_keeps_the_span() {
        let mut spans = Spans::new().gap(2);
        let mut starts = Vec::new();
        for (n, seen) in [true, false, false, true, false, false, false, true]
            .into_iter()
            .enumerate()
        {
            spans.tick(n as f64 * 0.5);
            if seen {
                starts.push(spans.see(()).start_t);
            }
        }
        assert_eq!(starts, [0.0, 0.0, 3.5]);
    }

    #[test]
    fn the_longest_span_splits() {
        let mut spans = Spans::new().longest(3);
        let mut seen = Vec::new();
        for n in 0..7 {
            spans.tick(n as f64);
            let span = spans.see(());
            seen.push((span.start_t, span.number, span.age));
        }
        assert_eq!(
            seen,
            [
                (0.0, 0, 0),
                (0.0, 0, 1),
                (0.0, 0, 2),
                (3.0, 1, 0),
                (3.0, 1, 1),
                (3.0, 1, 2),
                (6.0, 2, 0)
            ]
        );
    }

    #[test]
    fn keys_have_spans_of_their_own() {
        let mut spans = Spans::new();
        spans.tick(0.0);
        assert_eq!(spans.see("a").start_t, 0.0);
        spans.tick(1.0);
        assert_eq!(spans.see("a").start_t, 0.0);
        assert_eq!(spans.see("b").start_t, 1.0);
        assert_eq!(spans.open(), 2);
        spans.tick(2.0);
        assert_eq!(spans.open(), 2);
        spans.see("b");
        spans.tick(3.0);
        assert_eq!(spans.open(), 1);
    }

    #[test]
    fn cues_show_while_they_cover_the_time() {
        let mut cues = Cues::new();
        cues.add(Cue::new(2.0, 4.0, "second"));
        cues.add(Cue::new(0.0, 3.0, "first"));
        let at = |cues: &Cues, t| cues.at(t).map(|cue| cue.text.clone()).collect::<Vec<_>>();
        assert_eq!(at(&cues, 2.5), ["first", "second"]);
        assert_eq!(at(&cues, 3.0), ["second"]);
        assert!(at(&cues, 4.0).is_empty());
        cues.drop_ended(3.0);
        assert_eq!(cues.len(), 1);
    }
}
