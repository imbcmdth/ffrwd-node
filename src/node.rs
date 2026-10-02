use std::collections::BTreeMap;

use serde::de::DeserializeOwned;
use serde_json::Value;

use crate::out::{Emitted, Out};
use crate::params::{self, NO_PARAMS};
use crate::shape::{Bound, RowsUse, Shape};
use crate::tick::{Source, Tick};
use crate::types::BoundStream;

/// Why a call failed: the message the run ends with. Anything that displays
/// converts into one, so `?` takes a `String`, a `&str` or serde's errors
/// alike.
#[derive(Debug)]
pub struct Error(String);

impl<E: std::fmt::Display> From<E> for Error {
    fn from(err: E) -> Error {
        Error(err.to_string())
    }
}

impl Error {
    pub fn message(&self) -> &str {
        &self.0
    }
}

impl From<Error> for String {
    fn from(err: Error) -> String {
        err.0
    }
}

pub type Result<T, E = Error> = std::result::Result<T, E>;

/// A node: typed inputs, typed outputs and a clock. A module implements this
/// for its own type and hands the type to [`export!`](crate::export).
///
/// The host calls `describe` and `shape` at compile time, then once per
/// instance `init`, then `process` once a tick, the last call exactly once
/// with [`Tick::last`] set. The crate reads the params against
/// `PARAMS_SCHEMA` before any of these sees them, and folds the rows of a
/// state input into the node before each `process`.
pub trait Node: Sized + 'static {
    /// The module's name, as `describe` reports it.
    const NAME: &'static str;
    const VERSION: &'static str;
    /// The JSON schema of the params: what the compiler checks a call
    /// against, and whose defaults fill in what a call leaves out.
    const PARAMS_SCHEMA: &'static str = NO_PARAMS;
    /// The JSON schema of one row [`Out::report`] writes; empty when the
    /// node writes none.
    const ROWS_SCHEMA: &'static str = "";
    /// Ordered param names: the language of the node's JSON outputs is the
    /// first of these the call sets.
    const ROWS_LANGUAGE: &'static [&'static str] = &[];

    /// The params, read from the call's JSON by serde.
    type Params: DeserializeOwned;

    /// The ports and clock for these params, and for the inputs the call
    /// binds. Called at compile time, and again by the crate at `init`.
    fn shape(params: &Self::Params, bound: &Bound) -> Result<Shape>;

    /// Opens an instance on the streams bound to its inputs.
    fn init(params: Self::Params, init: &Init) -> Result<Self>;

    /// New params between ticks, whose shape is the instance's. Params equal
    /// to the ones in force never reach it. Refuses by default.
    fn set_params(&mut self, params: Self::Params) -> Result<()> {
        let _ = params;
        Err(format!("{} cannot change its params while it runs", Self::NAME).into())
    }

    /// One row of a state input, folded into the node before the `process`
    /// that follows: the rows of ticks this instance did not process first,
    /// oldest first, then this tick's. A node declaring a state input
    /// implements it.
    fn fold(&mut self, row: StateRow) -> Result<()> {
        Err(format!(
            "{} declares `{}` as state, so it folds that input's rows: implement Node::fold",
            Self::NAME,
            row.port
        )
        .into())
    }

    /// One tick. Returning promises that no data output still has anything
    /// to send stamped before the end of the tick's interval, less the
    /// port's latency.
    fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()>;
}

/// What `init` is handed: the streams bound to the node's inputs and the
/// outputs the query reads.
pub struct Init<'a> {
    streams: &'a [BoundStream],
    latched: &'a [String],
    shape: &'a Shape,
}

impl<'a> Init<'a> {
    /// Every bound stream: ports in the shape's order, a port's streams in
    /// the order the query named them.
    pub fn all(&self) -> &'a [BoundStream] {
        self.streams
    }

    /// The streams bound to input `port`.
    pub fn streams(&self, port: &str) -> Vec<&'a BoundStream> {
        self.streams
            .iter()
            .filter(|stream| stream.port == port)
            .collect()
    }

    /// The one stream bound to input `port`, or none when the call left it
    /// out.
    pub fn optional(&self, port: &str) -> Option<&'a BoundStream> {
        self.streams.iter().find(|stream| stream.port == port)
    }

    /// The one stream bound to required input `port`.
    pub fn stream(&self, port: &str) -> Result<&'a BoundStream, String> {
        self.optional(port)
            .ok_or_else(|| format!("no stream is bound to `{port}`"))
    }

    /// Whether the query reads output `port`; one it does not may be left
    /// unmade.
    pub fn latched(&self, port: &str) -> bool {
        self.latched.iter().any(|name| name == port)
    }

    /// The instance's shape, resolved for what the call binds.
    pub fn shape(&self) -> &'a Shape {
        self.shape
    }
}

/// One row arriving on a state input.
pub struct StateRow<'a> {
    pub port: &'a str,
    pub id: u32,
    /// When it arrived, in the stream's time base.
    pub pts: i64,
    /// The same, in seconds.
    pub seconds: f64,
    /// The row: one JSON object.
    pub json: &'a str,
}

impl StateRow<'_> {
    /// The row read as a `T`.
    pub fn row<T: DeserializeOwned>(&self) -> Result<T, String> {
        crate::rows::parse(self.json).map_err(|err| format!("on `{}`: {err}", self.port))
    }
}

/// What `describe` reports. A node's format lists stay empty: its ports say
/// what they accept.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Meta {
    pub name: String,
    pub version: String,
    pub params_schema: String,
    pub rows_schema: String,
    pub rows_language: Vec<String>,
}

/// The call sequence around a [`Node`], the same in a module and in a test:
/// params read against the schema, the shape resolved, state rows folded,
/// emissions checked.
pub struct Runner<N: Node> {
    node: N,
    shape: Shape,
    params: Value,
    ports: BTreeMap<u32, String>,
    state: Vec<(String, u32)>,
    out: Out,
}

impl<N: Node> Runner<N> {
    pub fn describe() -> Meta {
        Meta {
            name: N::NAME.to_owned(),
            version: N::VERSION.to_owned(),
            params_schema: N::PARAMS_SCHEMA.to_owned(),
            rows_schema: N::ROWS_SCHEMA.to_owned(),
            rows_language: N::ROWS_LANGUAGE
                .iter()
                .map(|name| (*name).to_owned())
                .collect(),
        }
    }

    fn read(params: &str) -> Result<(N::Params, Value), String> {
        params::read(N::PARAMS_SCHEMA, params)
    }

    /// The shape for `params` and the inputs `bound` names, as the host is
    /// handed it.
    pub fn shape(params: &str, bound: &[String]) -> Result<Shape, String> {
        let (params, _) = Self::read(params)?;
        let bound = Bound(bound.to_vec());
        N::shape(&params, &bound)?.resolve(&bound)
    }

    /// Opens an instance on `bound`.
    pub fn init(
        bound: Vec<BoundStream>,
        latched: Vec<String>,
        params: &str,
    ) -> Result<Runner<N>, String> {
        let (parsed, value) = Self::read(params)?;
        let mut names: Vec<String> = Vec::new();
        for stream in &bound {
            if !names.contains(&stream.port) {
                names.push(stream.port.clone());
            }
        }
        let names = Bound(names);
        let shape = N::shape(&parsed, &names)?.resolve(&names)?;
        let ports = bound
            .iter()
            .map(|stream| (stream.id, stream.port.clone()))
            .collect();
        let state = bound
            .iter()
            .filter(|stream| {
                shape
                    .find_input(&stream.port)
                    .is_some_and(|input| input.rows == RowsUse::State)
            })
            .map(|stream| (stream.port.clone(), stream.id))
            .collect();
        let node = N::init(
            parsed,
            &Init {
                streams: &bound,
                latched: &latched,
                shape: &shape,
            },
        )?;
        let out = Out::new(&shape);
        Ok(Runner {
            node,
            shape,
            params: value,
            ports,
            state,
            out,
        })
    }

    /// New params between ticks; equal ones are taken without asking the
    /// node.
    pub fn set_params(&mut self, params: &str) -> Result<(), String> {
        let (parsed, value) = Self::read(params)?;
        if value == self.params {
            return Ok(());
        }
        self.node.set_params(parsed)?;
        self.params = value;
        Ok(())
    }

    pub(crate) fn process_source(&mut self, source: &dyn Source) -> Result<Emitted, String> {
        self.out.take();
        let tick = Tick::new(source, &self.ports);
        self.out.begin(tick.time_base());
        fold(&mut self.node, &self.state, &tick)?;
        self.node.process(&tick, &mut self.out)?;
        Ok(self.out.take())
    }

    pub fn node(&self) -> &N {
        &self.node
    }

    pub fn node_mut(&mut self) -> &mut N {
        &mut self.node
    }

    /// The instance's shape, resolved.
    pub fn resolved(&self) -> &Shape {
        &self.shape
    }
}

fn fold<N: Node>(node: &mut N, state: &[(String, u32)], tick: &Tick) -> Result<(), String> {
    for (port, id) in state {
        let time_base = tick.info(*id).time_base;
        for timed in tick.earlier_rows(*id) {
            for json in &timed.rows {
                node.fold(StateRow {
                    port,
                    id: *id,
                    pts: timed.pts,
                    seconds: time_base.seconds(timed.pts),
                    json,
                })?;
            }
        }
    }
    for (port, id) in state {
        let time_base = tick.info(*id).time_base;
        let mut arrived: Vec<(i64, String)> = Vec::new();
        for message in tick.messages(*id) {
            let json = String::from_utf8(message.data)
                .map_err(|_| format!("a row on `{port}` is not utf-8"))?;
            arrived.push((message.pts, json));
        }
        for frame in tick.frames(*id) {
            arrived.extend(frame.rows.into_iter().map(|json| (frame.pts, json)));
        }
        for (pts, json) in &arrived {
            node.fold(StateRow {
                port,
                id: *id,
                pts: *pts,
                seconds: time_base.seconds(*pts),
                json,
            })?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock::Harness;
    use crate::shape::{Input, Output};
    use crate::Rational;

    #[derive(serde::Deserialize)]
    struct Params {
        #[allow(dead_code)]
        label: String,
    }

    struct Folding {
        v: u32,
        folded: Vec<(String, i64, String)>,
    }

    impl Node for Folding {
        const NAME: &'static str = "folding";
        const VERSION: &'static str = "0.0.0";
        const PARAMS_SCHEMA: &'static str = r#"{"type":"object","properties":{"label":{"type":"string","default":"a"}},"additionalProperties":false}"#;
        type Params = Params;

        fn shape(_: &Params, _: &Bound) -> Result<Shape> {
            Ok(Shape::new()
                .input(Input::video("v").clock())
                .input(Input::rows("notes").interval().state())
                .output(Output::rows("seen")))
        }

        fn init(_: Params, init: &Init) -> Result<Folding> {
            Ok(Folding {
                v: init.stream("v")?.id,
                folded: Vec::new(),
            })
        }

        fn fold(&mut self, row: StateRow) -> Result<()> {
            let note: String = row.row::<Value>()?["note"].to_string();
            self.folded.push((row.port.to_owned(), row.pts, note));
            Ok(())
        }

        fn process(&mut self, tick: &Tick, out: &mut Out) -> Result<()> {
            let frame = tick.frame(self.v).ok_or("no frame")?;
            Ok(out.row("seen", frame.pts, &self.folded.len())?)
        }
    }

    fn harness() -> Harness<Folding> {
        let tb = Rational::new(1, 10);
        let bound = vec![
            BoundStream::video("v", 0, 2, 2, "rgba", tb),
            BoundStream::rows("notes", 1, tb),
        ];
        Harness::new("", bound).unwrap()
    }

    #[test]
    fn earlier_rows_fold_before_the_tick_s_own() {
        let mut node = harness();
        let tick = node
            .tick(5)
            .frame(0, 5, vec![0; 16])
            .earlier(1, 1, &[r#"{"note":1}"#, r#"{"note":2}"#])
            .earlier(1, 3, &[r#"{"note":3}"#])
            .message(1, 5, br#"{"note":4}"#);
        let emitted = node.process(&tick).unwrap();
        let folded: Vec<(i64, &str)> = node
            .node()
            .folded
            .iter()
            .map(|(_, pts, note)| (*pts, note.as_str()))
            .collect();
        assert_eq!(folded, [(1, "1"), (1, "2"), (3, "3"), (5, "4")]);
        assert_eq!(emitted.messages("seen"), [(5, "4".to_owned())]);
    }

    #[test]
    fn params_in_force_are_taken_and_others_refused() {
        let mut node = harness();
        node.set_params(r#"{"label":"a"}"#).unwrap();
        node.set_params("").unwrap();
        let err = node.set_params(r#"{"label":"b"}"#).unwrap_err();
        assert!(err.contains("cannot change its params"), "{err}");
    }

    #[test]
    fn describe_is_the_constants() {
        let meta = Runner::<Folding>::describe();
        assert_eq!(
            (meta.name.as_str(), meta.version.as_str()),
            ("folding", "0.0.0")
        );
        assert!(meta.params_schema.contains("label") && meta.rows_schema.is_empty());
    }

    #[test]
    fn a_shape_is_resolved_for_the_host() {
        let shape = Runner::<Folding>::shape("", &["v".to_owned()]).unwrap();
        assert_eq!(shape.clock_input(), Some("v"));
        assert!(Runner::<Folding>::shape(r#"{"label":3}"#, &[]).is_err());
    }
}
