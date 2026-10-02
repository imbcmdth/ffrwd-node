//! A node's ports and clock, built the way a query reads them:
//! `Input::video("v").clock()`, `Input::rows("boxes").schema::<Box>()`,
//! `Output::video("mask").like("v").pixel_format("gray")`.

use serde::Serialize;

use crate::rows::schema_of;
use crate::types::{AudioFormat, CodedStream, Format, VideoFormat};
use crate::Rational;

/// What a port carries.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Video,
    Audio,
    /// Messages; for codec "json", one row each.
    Data,
    Packets,
}

impl Kind {
    pub(crate) fn frames(self) -> bool {
        matches!(self, Kind::Video | Kind::Audio)
    }
}

/// What a node does with the rows that arrive on an input.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum RowsUse {
    Ignore,
    /// Read while handling the tick they arrive with, and kept nowhere.
    PerFrame,
    /// Folded into state later ticks depend on: they reach `Node::fold`,
    /// earlier ticks' first on a worker that did not process them.
    State,
}

/// How a hold input's offset between source time and clock time is fixed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Anchor {
    /// The source's pts are on the clock's epoch: a timed feeder.
    SharedClock,
    /// Scheduled `lead` seconds ahead of the clock once the host holds that
    /// much of the source: an untimed feeder.
    FirstFrame,
    /// `SharedClock` for a feed whose tags carry this name set to 1,
    /// `FirstFrame` otherwise.
    Tagged(String),
}

/// A frame input paired by time: the newest frame at or before the tick.
#[derive(Clone, Debug, PartialEq)]
pub struct Hold {
    pub anchor: Anchor,
    pub lead: f64,
    pub linger: Option<f64>,
    pub timeout: Option<f64>,
    pub group: Option<String>,
    pub port_param: Option<String>,
}

/// A message input paired by time: every message stamped in the tick's
/// interval, `ahead` seconds past it included.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Interval {
    pub latency: Option<f64>,
    pub ahead: f64,
}

#[derive(Clone, Debug, PartialEq)]
pub enum Pairing {
    /// The clock's pts exactly: same source, one frame per tick.
    Lockstep,
    Hold(Hold),
    Interval(Interval),
    /// As it arrives, unpaired.
    Arrival,
}

/// How much of a stream a packets input needs.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Wants {
    #[default]
    All,
    Keyframes,
    First,
}

/// What an input accepts; an empty list accepts anything of the kind.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Accepts {
    pub pixel_formats: Vec<String>,
    pub sample_formats: Vec<String>,
    pub sample_rates: Vec<u32>,
    pub channel_counts: Vec<u32>,
    pub codecs: Vec<String>,
    pub wants: Wants,
    /// Conformed by the host to this input's size, or rate and layout.
    pub like: Option<String>,
}

/// One input port.
#[derive(Clone, Debug, PartialEq)]
pub struct Input {
    pub name: String,
    pub kind: Kind,
    pub required: bool,
    pub many: bool,
    pub pairing: Pairing,
    pub rows: RowsUse,
    pub window: u32,
    pub stride: u32,
    pub accepts: Accepts,
    /// Data inputs: the JSON schema of the rows read here.
    pub schema: Option<String>,
    /// Whether this input is the clock.
    pub clock: bool,
}

impl Input {
    fn new(name: &str, kind: Kind) -> Input {
        Input {
            name: name.to_owned(),
            kind,
            required: true,
            many: false,
            pairing: Pairing::Lockstep,
            rows: if kind == Kind::Data {
                RowsUse::PerFrame
            } else {
                RowsUse::Ignore
            },
            window: 1,
            stride: 1,
            accepts: Accepts::default(),
            schema: None,
            clock: false,
        }
    }

    pub fn video(name: &str) -> Input {
        Input::new(name, Kind::Video)
    }

    pub fn audio(name: &str) -> Input {
        Input::new(name, Kind::Audio)
    }

    /// A data input of JSON rows, read per frame unless told otherwise.
    pub fn rows(name: &str) -> Input {
        Input::new(name, Kind::Data)
    }

    pub fn packets(name: &str) -> Input {
        Input::new(name, Kind::Packets)
    }

    /// The clock: the frames each call sees. Lockstep, single and required.
    pub fn clock(mut self) -> Input {
        self.clock = true;
        self.pairing = Pairing::Lockstep;
        self
    }

    /// The call may leave it out.
    pub fn optional(mut self) -> Input {
        self.required = false;
        self
    }

    /// Any number of streams bound to this one port.
    pub fn many(mut self) -> Input {
        self.many = true;
        self
    }

    /// The frames (video) or samples (audio) a clock call sees, and how many
    /// it consumes: 15, 1 is a sliding window of fifteen frames, and a stride
    /// equal to the window is a tumbling one.
    pub fn window(mut self, window: u32, stride: u32) -> Input {
        self.window = window;
        self.stride = stride;
        self
    }

    pub fn lockstep(mut self) -> Input {
        self.pairing = Pairing::Lockstep;
        self
    }

    pub fn arrival(mut self) -> Input {
        self.pairing = Pairing::Arrival;
        self
    }

    /// Paired by holding the newest frame: anchored on its first frame, no
    /// lead, no linger, no timeout until the methods below say otherwise.
    pub fn hold(mut self) -> Input {
        if !matches!(self.pairing, Pairing::Hold(_)) {
            self.pairing = Pairing::Hold(Hold {
                anchor: Anchor::FirstFrame,
                lead: 0.0,
                linger: None,
                timeout: None,
                group: None,
                port_param: None,
            });
        }
        self
    }

    fn held(mut self, set: impl FnOnce(&mut Hold)) -> Input {
        self = self.hold();
        if let Pairing::Hold(hold) = &mut self.pairing {
            set(hold);
        }
        self
    }

    pub fn anchor(self, anchor: Anchor) -> Input {
        self.held(|hold| hold.anchor = anchor)
    }

    pub fn lead(self, seconds: f64) -> Input {
        self.held(|hold| hold.lead = seconds)
    }

    pub fn linger(self, seconds: f64) -> Input {
        self.held(|hold| hold.linger = Some(seconds))
    }

    pub fn timeout(self, seconds: f64) -> Input {
        self.held(|hold| hold.timeout = Some(seconds))
    }

    pub fn group(self, group: &str) -> Input {
        self.held(|hold| hold.group = Some(group.to_owned()))
    }

    /// The param the host writes this input's loopback port into, or reads
    /// the port to listen on from.
    pub fn port_param(self, param: &str) -> Input {
        self.held(|hold| hold.port_param = Some(param.to_owned()))
    }

    /// Paired by interval: every message stamped in the tick's interval,
    /// settled once the producer's progress has passed it.
    pub fn interval(mut self) -> Input {
        if !matches!(self.pairing, Pairing::Interval(_)) {
            self.pairing = Pairing::Interval(Interval::default());
        }
        self
    }

    fn intervalled(mut self, set: impl FnOnce(&mut Interval)) -> Input {
        self = self.interval();
        if let Pairing::Interval(interval) = &mut self.pairing {
            set(interval);
        }
        self
    }

    /// The most this interval input waits past the interval's end, in
    /// seconds.
    pub fn latency(self, seconds: f64) -> Input {
        self.intervalled(|interval| interval.latency = Some(seconds))
    }

    /// Seconds past the interval's end whose messages come with it.
    pub fn ahead(self, seconds: f64) -> Input {
        self.intervalled(|interval| interval.ahead = seconds)
    }

    pub fn per_frame(mut self) -> Input {
        self.rows = RowsUse::PerFrame;
        self
    }

    pub fn state(mut self) -> Input {
        self.rows = RowsUse::State;
        self
    }

    pub fn ignore_rows(mut self) -> Input {
        self.rows = RowsUse::Ignore;
        self
    }

    pub fn pixel_formats(mut self, formats: &[&str]) -> Input {
        self.accepts.pixel_formats = strings(formats);
        self
    }

    pub fn sample_formats(mut self, formats: &[&str]) -> Input {
        self.accepts.sample_formats = strings(formats);
        self
    }

    pub fn sample_rates(mut self, rates: &[u32]) -> Input {
        self.accepts.sample_rates = rates.to_vec();
        self
    }

    pub fn channel_counts(mut self, counts: &[u32]) -> Input {
        self.accepts.channel_counts = counts.to_vec();
        self
    }

    pub fn codecs(mut self, codecs: &[&str]) -> Input {
        self.accepts.codecs = strings(codecs);
        self
    }

    pub fn wants(mut self, wants: Wants) -> Input {
        self.accepts.wants = wants;
        self
    }

    /// Conformed to input `port`'s size (video), or rate and layout (audio).
    pub fn like(mut self, port: &str) -> Input {
        self.accepts.like = Some(port.to_owned());
        self
    }

    /// The rows read here are `T`s: the schema `schema_of::<T>()` writes.
    pub fn schema<T: Default + Serialize>(mut self) -> Input {
        self.schema = Some(schema_of::<T>());
        self
    }

    /// The rows read here, as a JSON schema written out.
    pub fn schema_json(mut self, schema: &str) -> Input {
        self.schema = Some(schema.to_owned());
        self
    }
}

/// An output whose format is another port's, with one field overridden.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Like {
    /// The input it follows; none is the clock input.
    pub port: Option<String>,
    pub pixel_format: Option<String>,
    pub sample_format: Option<String>,
}

/// One output port.
#[derive(Clone, Debug, PartialEq)]
pub struct Output {
    pub name: String,
    pub kind: Kind,
    /// A format of its own; with neither this nor `like`, the clock input's.
    pub format: Option<Format>,
    /// The format of an input, overridden.
    pub like: Option<Like>,
    /// None is the clock's.
    pub time_base: Option<Rational>,
    /// How far behind the end of its tick's interval a stamp may fall, in
    /// seconds.
    pub latency: f64,
    /// Data outputs: the JSON schema of the rows written here.
    pub schema: Option<String>,
    /// A source's relation row this output belongs to.
    pub row: Option<u32>,
    pub(crate) named_like: bool,
}

impl Output {
    fn new(name: &str, kind: Kind) -> Output {
        Output {
            name: name.to_owned(),
            kind,
            format: None,
            like: None,
            time_base: None,
            latency: 0.0,
            schema: None,
            row: None,
            named_like: false,
        }
    }

    /// A video output: the clock input's format until `like`, `size` or
    /// `pixel_format` say otherwise.
    pub fn video(name: &str) -> Output {
        Output::new(name, Kind::Video)
    }

    pub fn audio(name: &str) -> Output {
        Output::new(name, Kind::Audio)
    }

    /// A data output of JSON rows.
    pub fn rows(name: &str) -> Output {
        let mut output = Output::new(name, Kind::Data);
        output.format = Some(Format::Data("json".to_owned()));
        output
    }

    pub fn packets(name: &str) -> Output {
        Output::new(name, Kind::Packets)
    }

    /// An output named after input `port`, of its kind and in its format: a
    /// filter's `v` out for its `v` in.
    pub fn like(port: &str) -> Output {
        let mut output = Output::new(port, Kind::Video);
        output.named_like = true;
        output.like = Some(Like {
            port: Some(port.to_owned()),
            pixel_format: None,
            sample_format: None,
        });
        output
    }

    /// In input `port`'s format.
    pub fn following(mut self, port: &str) -> Output {
        let like = self.like.get_or_insert(Like {
            port: None,
            pixel_format: None,
            sample_format: None,
        });
        like.port = Some(port.to_owned());
        self.format = None;
        self
    }

    /// In this pixel format: of the input it follows, the clock input when it
    /// follows none, or of the size `size` gave.
    pub fn pixel_format(mut self, pix_fmt: &str) -> Output {
        match &mut self.format {
            Some(Format::Video(video)) => video.pix_fmt = pix_fmt.to_owned(),
            _ => {
                let like = self.like.get_or_insert(Like {
                    port: None,
                    pixel_format: None,
                    sample_format: None,
                });
                like.pixel_format = Some(pix_fmt.to_owned());
            }
        }
        self
    }

    /// In this sample format, of the input it follows or the clock input.
    pub fn sample_format(mut self, sample_fmt: &str) -> Output {
        match &mut self.format {
            Some(Format::Audio(audio)) => audio.sample_fmt = sample_fmt.to_owned(),
            _ => {
                let like = self.like.get_or_insert(Like {
                    port: None,
                    pixel_format: None,
                    sample_format: None,
                });
                like.sample_format = Some(sample_fmt.to_owned());
            }
        }
        self
    }

    /// Pictures of `width` x `height`, in the pixel format `pixel_format`
    /// names.
    pub fn size(mut self, width: u32, height: u32) -> Output {
        let pix_fmt = self
            .like
            .take()
            .and_then(|like| like.pixel_format)
            .unwrap_or_default();
        self.format = Some(Format::Video(VideoFormat {
            width,
            height,
            pix_fmt,
            color: None,
        }));
        self
    }

    pub fn video_format(mut self, format: VideoFormat) -> Output {
        self.like = None;
        self.format = Some(Format::Video(format));
        self
    }

    pub fn audio_format(mut self, format: AudioFormat) -> Output {
        self.like = None;
        self.format = Some(Format::Audio(format));
        self
    }

    pub fn coded(mut self, coded: CodedStream) -> Output {
        self.like = None;
        self.format = Some(Format::Packets(coded));
        self
    }

    pub fn time_base(mut self, time_base: Rational) -> Output {
        self.time_base = Some(time_base);
        self
    }

    pub fn latency(mut self, seconds: f64) -> Output {
        self.latency = seconds;
        self
    }

    /// The rows written here are `T`s.
    pub fn schema<T: Default + Serialize>(mut self) -> Output {
        self.schema = Some(schema_of::<T>());
        self
    }

    pub fn schema_json(mut self, schema: &str) -> Output {
        self.schema = Some(schema.to_owned());
        self
    }

    pub fn row(mut self, row: u32) -> Output {
        self.row = Some(row);
        self
    }
}

/// What drives a node's calls.
#[derive(Clone, Debug, PartialEq)]
pub enum Clock {
    /// The input of this name: one call per stride of its frames.
    Input(String),
    /// A generator, so many ticks a second.
    Rate(Rational),
    /// A rate clock at the named input's rate.
    RateOf(String),
    /// The node emits when it has something.
    SelfClocked,
}

/// A node's ports and clock for one call's params.
#[derive(Clone, Debug, PartialEq)]
pub struct Shape {
    pub inputs: Vec<Input>,
    pub outputs: Vec<Output>,
    /// Set by an input's `clock()`, or by `rate`, `rate_of` or
    /// `self_clocked`.
    pub clock: Option<Clock>,
    pub pure: bool,
    pub one_to_one: bool,
    pub bounded: bool,
    pub relation: Vec<String>,
}

impl Default for Shape {
    fn default() -> Shape {
        Shape::new()
    }
}

impl Shape {
    /// No ports, impure, not one-to-one, bounded.
    pub fn new() -> Shape {
        Shape {
            inputs: Vec::new(),
            outputs: Vec::new(),
            clock: None,
            pure: false,
            one_to_one: false,
            bounded: true,
            relation: Vec::new(),
        }
    }

    pub fn input(mut self, input: Input) -> Shape {
        self.inputs.push(input);
        self
    }

    pub fn output(mut self, output: Output) -> Shape {
        self.outputs.push(output);
        self
    }

    /// Ticks `rate` times a second.
    pub fn rate(mut self, rate: Rational) -> Shape {
        self.clock = Some(Clock::Rate(rate));
        self
    }

    /// Ticks at input `port`'s rate, which the compiler reads off its first
    /// stream.
    pub fn rate_of(mut self, port: &str) -> Shape {
        self.clock = Some(Clock::RateOf(port.to_owned()));
        self
    }

    pub fn self_clocked(mut self) -> Shape {
        self.clock = Some(Clock::SelfClocked);
        self
    }

    /// Every call depends only on what it was handed, so the host may spread
    /// the node over workers.
    pub fn pure(mut self) -> Shape {
        self.pure = true;
        self
    }

    /// One frame out per frame in on the clock's outputs, each at its tick's
    /// pts.
    pub fn one_to_one(mut self) -> Shape {
        self.one_to_one = true;
        self
    }

    /// Whether a rate or self-clocked node ends by itself; one that does not
    /// is planned as live.
    pub fn bounded(mut self, bounded: bool) -> Shape {
        self.bounded = bounded;
        self
    }

    /// One relation row of a source read in FROM, as a JSON object.
    pub fn relation_row(mut self, row: &str) -> Shape {
        self.relation.push(row.to_owned());
        self
    }

    pub fn find_input(&self, name: &str) -> Option<&Input> {
        self.inputs.iter().find(|input| input.name == name)
    }

    pub fn find_output(&self, name: &str) -> Option<&Output> {
        self.outputs.iter().find(|output| output.name == name)
    }

    /// The clock input's name, when an input is the clock.
    pub fn clock_input(&self) -> Option<&str> {
        match &self.clock {
            Some(Clock::Input(name)) => Some(name),
            _ => None,
        }
    }

    /// The shape the host is handed for a call binding `bound`: the clock
    /// settled, outputs following an unbound input left out, and every rule
    /// the host would refuse it by checked here first, with the port named.
    pub fn resolve(mut self, bound: &Bound) -> Result<Shape, String> {
        let clocks: Vec<&str> = self
            .inputs
            .iter()
            .filter(|input| input.clock)
            .map(|input| input.name.as_str())
            .collect();
        match (clocks.as_slice(), &self.clock) {
            ([], None) => {
                return Err(
                    "the shape has no clock: mark an input `clock()`, or give a rate".to_owned(),
                )
            }
            ([], Some(_)) => {}
            ([name], None) => self.clock = Some(Clock::Input((*name).to_owned())),
            ([name], Some(Clock::Input(other))) if name == other => {}
            ([name], Some(_)) => {
                return Err(format!("`{name}` is the clock, and so is the shape's rate"))
            }
            (several, _) => {
                return Err(format!(
                    "only one input can be the clock, not {}",
                    several.join(" and ")
                ))
            }
        }

        let mut names = std::collections::HashSet::new();
        for input in &self.inputs {
            if !names.insert(input.name.as_str()) {
                return Err(format!("two inputs are named `{}`", input.name));
            }
        }
        let mut names = std::collections::HashSet::new();
        for output in &self.outputs {
            if !names.insert(output.name.as_str()) {
                return Err(format!("two outputs are named `{}`", output.name));
            }
        }

        let clock = self.clock_input().map(str::to_owned);
        for input in &mut self.inputs {
            if let Some(port) = &input.accepts.like {
                if !bound.has(port) {
                    input.accepts.like = None;
                }
            }
        }
        let mut outputs = Vec::new();
        for mut output in std::mem::take(&mut self.outputs) {
            if let Some(like) = &mut output.like {
                let port = match (&like.port, &clock) {
                    (Some(port), _) => port.clone(),
                    (None, Some(clock)) => clock.clone(),
                    (None, None) => {
                        return Err(format!(
                            "output `{}` takes its format from the clock input, and the clock is \
                             not an input: give it `following(port)` or a format of its own",
                            output.name
                        ))
                    }
                };
                let Some(input) = self.inputs.iter().find(|input| input.name == port) else {
                    return Err(format!(
                        "output `{}` follows `{port}`, which is not an input",
                        output.name
                    ));
                };
                if !bound.has(&port) {
                    continue;
                }
                if input.many {
                    return Err(format!(
                        "output `{}` follows `{port}`, which takes many streams",
                        output.name
                    ));
                }
                if output.named_like {
                    output.kind = input.kind;
                }
                like.port = Some(port);
            }
            outputs.push(output);
        }
        self.outputs = outputs;
        self.check(bound)?;
        Ok(self)
    }

    fn check(&self, bound: &Bound) -> Result<(), String> {
        match &self.clock {
            Some(Clock::Input(name)) => {
                let Some(input) = self.find_input(name) else {
                    return Err(format!("the clock is `{name}`, which is not an input"));
                };
                if !input.required || input.many || input.pairing != Pairing::Lockstep {
                    return Err(format!(
                        "the clock `{name}` has to be required, single and lockstep"
                    ));
                }
            }
            Some(Clock::RateOf(name)) if self.find_input(name).is_none() => {
                return Err(format!("the rate is `{name}`'s, which is not an input"));
            }
            Some(Clock::Rate(rate)) if rate.num <= 0 || rate.den <= 0 => {
                return Err(format!("a rate of {}/{} never ticks", rate.num, rate.den));
            }
            _ => {}
        }
        let input_clock = self.clock_input().is_some();
        for input in &self.inputs {
            let name = &input.name;
            match &input.pairing {
                Pairing::Lockstep if !input_clock => {
                    return Err(format!(
                        "`{name}` is lockstep, and a node without an input clock has nothing \
                         to be in step with: hold it, or pair it by interval"
                    ))
                }
                Pairing::Hold(_) if !input.kind.frames() => {
                    return Err(format!("`{name}` carries messages, so it cannot be held"))
                }
                Pairing::Interval(_) if input.kind.frames() => {
                    return Err(format!(
                        "`{name}` carries frames, so it cannot pair by interval"
                    ))
                }
                pairing
                    if matches!(self.clock, Some(Clock::SelfClocked))
                        && *pairing != Pairing::Arrival =>
                {
                    return Err(format!(
                        "`{name}` feeds a self-clocked node, so it pairs by arrival"
                    ))
                }
                _ => {}
            }
            if input.kind == Kind::Data && input.rows == RowsUse::Ignore {
                return Err(format!("`{name}` carries rows, so it cannot ignore them"));
            }
            if input.stride == 0 || input.stride > input.window {
                return Err(format!(
                    "`{name}` has a window of {} and a stride of {}: the stride runs from 1 to \
                     the window",
                    input.window, input.stride
                ));
            }
            if let Some(port) = &input.accepts.like {
                let single = self
                    .find_input(port)
                    .map(|other| !other.many && other.kind == input.kind)
                    .unwrap_or(false);
                if !single || !bound.has(port) {
                    return Err(format!(
                        "`{name}` is conformed to `{port}`, which has to be a single bound input \
                         of its kind"
                    ));
                }
            }
        }
        for output in &self.outputs {
            let name = &output.name;
            let format_kind = output.format.as_ref().map(|format| match format {
                Format::Video(_) => Kind::Video,
                Format::Audio(_) => Kind::Audio,
                Format::Data(_) => Kind::Data,
                Format::Packets(_) => Kind::Packets,
            });
            if let Some(kind) = format_kind {
                if kind != output.kind {
                    return Err(format!(
                        "output `{name}` is {:?} with a {kind:?} format",
                        output.kind
                    ));
                }
            }
            if let Some(Format::Video(video)) = &output.format {
                if video.pix_fmt.is_empty() {
                    return Err(format!("output `{name}` has a size and no pixel format"));
                }
            }
            if output.format.is_none() && output.like.is_none() && !input_clock {
                return Err(format!(
                    "output `{name}` takes the clock input's format, and the clock is not an \
                     input: give it a format"
                ));
            }
            if let Some(like) = &output.like {
                if let Some(input) = like.port.as_deref().and_then(|port| self.find_input(port)) {
                    if input.kind != output.kind {
                        return Err(format!(
                            "output `{name}` is {:?} and follows `{}`, which is {:?}",
                            output.kind, input.name, input.kind
                        ));
                    }
                }
            }
            if let Some(row) = output.row {
                if row as usize >= self.relation.len() {
                    return Err(format!(
                        "output `{name}` belongs to relation row {row}, and there are {}",
                        self.relation.len()
                    ));
                }
            }
        }
        Ok(())
    }
}

/// The inputs a call binds, by name.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Bound(pub Vec<String>);

impl Bound {
    pub fn new(names: &[&str]) -> Bound {
        Bound(strings(names))
    }

    /// Whether the call binds input `port`.
    pub fn has(&self, port: &str) -> bool {
        self.0.iter().any(|name| name == port)
    }
}

fn strings(values: &[&str]) -> Vec<String> {
    values.iter().map(|value| (*value).to_owned()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn filter() -> Shape {
        Shape::new()
            .input(Input::video("v").clock().pixel_formats(&["rgba"]))
            .output(Output::like("v"))
    }

    #[test]
    fn the_clock_comes_from_the_input_marked() {
        let shape = filter().resolve(&Bound::new(&["v"])).unwrap();
        assert_eq!(shape.clock, Some(Clock::Input("v".to_owned())));
        assert_eq!(shape.outputs[0].kind, Kind::Video);
        assert_eq!(
            shape.outputs[0].like.as_ref().unwrap().port.as_deref(),
            Some("v")
        );
    }

    #[test]
    fn a_pixel_format_alone_follows_the_clock() {
        let shape = Shape::new()
            .input(Input::video("v").clock())
            .output(Output::video("mask").pixel_format("gray"))
            .resolve(&Bound::new(&["v"]))
            .unwrap();
        let like = shape.outputs[0].like.as_ref().unwrap();
        assert_eq!(like.port.as_deref(), Some("v"));
        assert_eq!(like.pixel_format.as_deref(), Some("gray"));
    }

    #[test]
    fn an_output_following_an_unbound_input_is_left_out() {
        let shape = Shape::new()
            .input(Input::video("v").clock())
            .input(Input::audio("a").optional())
            .output(Output::like("v"))
            .output(Output::like("a"))
            .resolve(&Bound::new(&["v"]))
            .unwrap();
        assert_eq!(shape.outputs.len(), 1);
        assert_eq!(shape.outputs[0].name, "v");
    }

    #[test]
    fn refusals_name_the_port() {
        let no_clock = Shape::new().input(Input::video("v"));
        assert!(no_clock.resolve(&Bound::new(&["v"])).is_err());

        let optional_clock = Shape::new().input(Input::video("v").clock().optional());
        let err = optional_clock.resolve(&Bound::new(&["v"])).unwrap_err();
        assert!(err.contains("`v`"), "{err}");

        let held_rows = filter().input(Input::rows("boxes").hold());
        let err = held_rows.resolve(&Bound::new(&["v", "boxes"])).unwrap_err();
        assert!(err.contains("`boxes`"), "{err}");

        let interval_video = filter().input(Input::video("w").interval());
        assert!(interval_video.resolve(&Bound::new(&["v", "w"])).is_err());

        let ignored_rows = filter().input(Input::rows("boxes").ignore_rows());
        assert!(ignored_rows.resolve(&Bound::new(&["v", "boxes"])).is_err());

        let wide_stride = Shape::new().input(Input::audio("a").clock().window(4, 5));
        assert!(wide_stride.resolve(&Bound::new(&["a"])).is_err());

        let like_many = Shape::new()
            .input(Input::video("v").many().hold())
            .rate_of("v")
            .output(Output::video("out").following("v"));
        let err = like_many.resolve(&Bound::new(&["v"])).unwrap_err();
        assert!(err.contains("many"), "{err}");

        let generator_lockstep = Shape::new()
            .rate(Rational::new(30, 1))
            .input(Input::video("v"))
            .output(Output::video("out").size(64, 64).pixel_format("rgba"));
        assert!(generator_lockstep.resolve(&Bound::new(&["v"])).is_err());
    }

    #[test]
    fn a_generator_gives_its_own_format() {
        let ticker = Shape::new()
            .rate(Rational::new(30, 1))
            .output(
                Output::video("video")
                    .size(1280, 720)
                    .pixel_format("rgba")
                    .row(0),
            )
            .relation_row("{}")
            .bounded(false);
        let shape = ticker.clone().resolve(&Bound::default()).unwrap();
        assert!(matches!(
            shape.outputs[0].format,
            Some(Format::Video(VideoFormat {
                width: 1280,
                height: 720,
                ..
            }))
        ));
        let no_format = Shape::new()
            .rate(Rational::new(30, 1))
            .output(Output::video("video"));
        assert!(no_format.resolve(&Bound::default()).is_err());
        let no_pix_fmt = Shape::new()
            .rate(Rational::new(30, 1))
            .output(Output::video("video").size(8, 8));
        assert!(no_pix_fmt.resolve(&Bound::default()).is_err());
    }

    #[test]
    fn hold_and_interval_fields_chain() {
        let feed = Input::video("feed")
            .optional()
            .hold()
            .lead(0.5)
            .port_param("port");
        let Pairing::Hold(hold) = feed.pairing else {
            panic!("not held")
        };
        assert_eq!(hold.anchor, Anchor::FirstFrame);
        assert_eq!(hold.lead, 0.5);
        assert_eq!(hold.port_param.as_deref(), Some("port"));

        let words = Input::rows("words").latency(2.0).ahead(0.5).state();
        assert_eq!(
            words.pairing,
            Pairing::Interval(Interval {
                latency: Some(2.0),
                ahead: 0.5
            })
        );
        assert_eq!(words.rows, RowsUse::State);
    }
}
