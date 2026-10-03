#pragma once

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "fields.hpp"
#include "result.hpp"
#include "time.hpp"
#include "types.hpp"

namespace ffrwd {

/// What a port carries.
enum class Kind {
    Video,
    Audio,
    /// Messages; for codec "json", one row each.
    Data,
    Packets,
};

inline bool carries_frames(Kind kind) { return kind == Kind::Video || kind == Kind::Audio; }

/// What a node does with the rows that arrive on an input.
enum class RowsUse {
    Ignore,
    /// Read while handling the tick they arrive with, and kept nowhere.
    PerFrame,
    /// Folded into state later ticks depend on: they reach the node's
    /// `fold`, earlier ticks' first on a worker that did not process them.
    State,
};

/// How much of a stream an input needs.
enum class Wants {
    All,
    /// Packets: keyframes alone.
    Keyframes,
    /// Packets: the first of each stream.
    First,
    /// Frames: their times and the stream's info, never their bytes.
    Timing,
};

/// What maps a stream's pts onto the clock: a hold input's, or a data input's
/// paired by interval.
struct Anchor {
    enum class Kind {
        /// The source's pts are on the clock's epoch: a timed feeder, or a
        /// lateral's rows.
        SharedClock,
        /// Held: scheduled `lead` seconds ahead of the clock once the host
        /// holds that much of the source. By interval: the first message is
        /// placed at the tick it arrives on, and every later one keeps that
        /// offset.
        FirstFrame,
        /// `SharedClock` for a feed whose tags carry `tag` set to 1,
        /// `FirstFrame` otherwise.
        Tagged,
    };

    Kind kind = Kind::SharedClock;
    std::string tag;

    static Anchor shared_clock() { return {Kind::SharedClock, {}}; }
    static Anchor first_frame() { return {Kind::FirstFrame, {}}; }
    static Anchor tagged(std::string tag) { return {Kind::Tagged, std::move(tag)}; }

    friend bool operator==(const Anchor&, const Anchor&) = default;
};

/// The clock's pts exactly: same source, one frame per tick.
struct Lockstep {
    friend bool operator==(const Lockstep&, const Lockstep&) = default;
};

/// A frame input paired by time: the newest frame at or before the tick.
struct Hold {
    Anchor anchor = Anchor::first_frame();
    double lead = 0.0;
    std::optional<double> linger;
    std::optional<double> timeout;
    std::optional<std::string> group;
    std::optional<std::string> port_param;

    friend bool operator==(const Hold&, const Hold&) = default;
};

/// A message input paired by time: every message stamped in the tick's
/// interval, `ahead` seconds past it included.
struct Interval {
    std::optional<double> latency;
    double ahead = 0.0;
    Anchor anchor = Anchor::shared_clock();
    /// The hold group whose connection the stream arrives on, with that
    /// group's offset; its anchor is then the shared clock.
    std::optional<std::string> group;

    friend bool operator==(const Interval&, const Interval&) = default;
};

/// As it arrives, unpaired.
struct Arrival {
    friend bool operator==(const Arrival&, const Arrival&) = default;
};

using Pairing = std::variant<Lockstep, Hold, Interval, Arrival>;

/// What an input accepts; an empty list accepts anything of the kind.
struct Accepts {
    std::vector<std::string> pixel_formats;
    std::vector<std::string> sample_formats;
    std::vector<std::uint32_t> sample_rates;
    std::vector<std::uint32_t> channel_counts;
    std::vector<std::string> codecs;
    Wants wants = Wants::All;
    /// Conformed by the host to this input's size, or rate and layout.
    std::optional<std::string> like;
};

/// One input port, field for field the WIT's `input-port`.
struct InputPort {
    std::string name;
    Kind kind = Kind::Video;
    bool required = true;
    bool many = false;
    Pairing pairing = Lockstep{};
    RowsUse rows = RowsUse::Ignore;
    std::uint32_t window = 1;
    std::uint32_t stride = 1;
    Accepts accepts;
    /// Data inputs: the JSON schema of the rows read here.
    std::optional<std::string> schema;
    /// Whether this input is the clock.
    bool clock = false;
};

/// An output whose format is another port's, with one field overridden.
struct Like {
    /// The input it follows; none is the clock input.
    std::optional<std::string> port;
    std::optional<std::string> pixel_format;
    std::optional<std::string> sample_format;
};

/// One output port, field for field the WIT's `output-port`.
struct OutputPort {
    std::string name;
    Kind kind = Kind::Video;
    /// A format of its own; with neither this nor `like`, the clock input's.
    std::optional<Format> format;
    /// The format of an input, overridden.
    std::optional<Like> like;
    /// None is the clock's.
    std::optional<Rational> time_base;
    /// How far behind the end of its tick's interval a stamp may fall, in
    /// seconds.
    double latency = 0.0;
    /// Data outputs: the JSON schema of the rows written here.
    std::optional<std::string> schema;
    /// A source's relation row this output belongs to.
    std::optional<std::uint32_t> row;
    /// Named after the input it follows, and of its kind.
    bool named_like = false;
};

/// An input port, built the way a query reads it:
/// `Input::video("v").clock().pixel_formats({"rgba"})`.
class Input {
public:
    static Input video(std::string name) { return Input(std::move(name), Kind::Video); }
    static Input audio(std::string name) { return Input(std::move(name), Kind::Audio); }
    /// A data input of JSON rows, read per frame unless told otherwise.
    static Input rows(std::string name) { return Input(std::move(name), Kind::Data); }
    static Input packets(std::string name) { return Input(std::move(name), Kind::Packets); }

    /// The clock: the frames each call sees. Lockstep, single and required.
    template <class Self>
    Self&& clock(this Self&& self) {
        self.port_.clock = true;
        self.port_.pairing = Lockstep{};
        return std::forward<Self>(self);
    }

    /// The call may leave it out.
    template <class Self>
    Self&& optional(this Self&& self) {
        self.port_.required = false;
        return std::forward<Self>(self);
    }

    /// Any number of streams bound to this one port.
    template <class Self>
    Self&& many(this Self&& self) {
        self.port_.many = true;
        return std::forward<Self>(self);
    }

    /// The frames (video) or samples (audio) a clock call sees, and how many
    /// it consumes: 15, 1 is a sliding window of fifteen frames, and a stride
    /// equal to the window is a tumbling one.
    template <class Self>
    Self&& window(this Self&& self, std::uint32_t window, std::uint32_t stride) {
        self.port_.window = window;
        self.port_.stride = stride;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& lockstep(this Self&& self) {
        self.port_.pairing = Lockstep{};
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& arrival(this Self&& self) {
        self.port_.pairing = Arrival{};
        return std::forward<Self>(self);
    }

    /// Paired by holding the newest frame: anchored on its first frame, no
    /// lead, no linger, no timeout until the methods below say otherwise.
    template <class Self>
    Self&& hold(this Self&& self) {
        self.held();
        return std::forward<Self>(self);
    }

    /// What maps the stream's pts onto the clock, held or by interval.
    template <class Self>
    Self&& anchor(this Self&& self, Anchor anchor) {
        self.timed();
        if (auto* hold = std::get_if<Hold>(&self.port_.pairing)) hold->anchor = anchor;
        if (auto* interval = std::get_if<Interval>(&self.port_.pairing)) interval->anchor = anchor;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& lead(this Self&& self, double seconds) {
        self.held().lead = seconds;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& linger(this Self&& self, double seconds) {
        self.held().linger = seconds;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& timeout(this Self&& self, double seconds) {
        self.held().timeout = seconds;
        return std::forward<Self>(self);
    }

    /// A hold group: held inputs of one group arrive on one connection from
    /// one source, with one offset. A data input naming a hold group arrives
    /// on that connection, paired by interval.
    template <class Self>
    Self&& group(this Self&& self, std::string group) {
        self.timed();
        if (auto* hold = std::get_if<Hold>(&self.port_.pairing)) hold->group = group;
        if (auto* interval = std::get_if<Interval>(&self.port_.pairing)) interval->group = group;
        return std::forward<Self>(self);
    }

    /// The param the host writes this input's loopback port into, or reads
    /// the port to listen on from.
    template <class Self>
    Self&& port_param(this Self&& self, std::string param) {
        self.held().port_param = std::move(param);
        return std::forward<Self>(self);
    }

    /// Paired by interval: every message stamped in the tick's interval,
    /// settled once the producer's progress has passed it.
    template <class Self>
    Self&& interval(this Self&& self) {
        self.intervalled();
        return std::forward<Self>(self);
    }

    /// The most this interval input waits past the interval's end, in
    /// seconds.
    template <class Self>
    Self&& latency(this Self&& self, double seconds) {
        self.intervalled().latency = seconds;
        return std::forward<Self>(self);
    }

    /// Seconds past the interval's end whose messages come with it.
    template <class Self>
    Self&& ahead(this Self&& self, double seconds) {
        self.intervalled().ahead = seconds;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& per_frame(this Self&& self) {
        self.port_.rows = RowsUse::PerFrame;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& state(this Self&& self) {
        self.port_.rows = RowsUse::State;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& ignore_rows(this Self&& self) {
        self.port_.rows = RowsUse::Ignore;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& pixel_formats(this Self&& self, std::vector<std::string> formats) {
        self.port_.accepts.pixel_formats = std::move(formats);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& sample_formats(this Self&& self, std::vector<std::string> formats) {
        self.port_.accepts.sample_formats = std::move(formats);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& sample_rates(this Self&& self, std::vector<std::uint32_t> rates) {
        self.port_.accepts.sample_rates = std::move(rates);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& channel_counts(this Self&& self, std::vector<std::uint32_t> counts) {
        self.port_.accepts.channel_counts = std::move(counts);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& codecs(this Self&& self, std::vector<std::string> codecs) {
        self.port_.accepts.codecs = std::move(codecs);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& wants(this Self&& self, Wants wants) {
        self.port_.accepts.wants = wants;
        return std::forward<Self>(self);
    }

    /// Read for its frames' times and its stream's info alone: the compiler
    /// hands the stream in whatever format its source has cheapest, and the
    /// host carries no pixels or samples for it. `fetch` on it, or passing
    /// one of its frames on, is refused.
    template <class Self>
    Self&& timing(this Self&& self) {
        self.port_.accepts.wants = Wants::Timing;
        return std::forward<Self>(self);
    }

    /// Conformed to input `port`'s size (video), or rate and layout (audio).
    template <class Self>
    Self&& like(this Self&& self, std::string port) {
        self.port_.accepts.like = std::move(port);
        return std::forward<Self>(self);
    }

    /// The rows read here are `T`s: the schema `schema_of<T>()` writes.
    template <class T, class Self>
    Self&& schema(this Self&& self) {
        self.port_.schema = schema_of<T>();
        return std::forward<Self>(self);
    }

    /// The rows read here, as a JSON schema written out.
    template <class Self>
    Self&& schema_json(this Self&& self, std::string schema) {
        self.port_.schema = std::move(schema);
        return std::forward<Self>(self);
    }

    const InputPort& port() const& { return port_; }
    InputPort port() && { return std::move(port_); }

private:
    Input(std::string name, Kind kind) {
        port_.name = std::move(name);
        port_.kind = kind;
        port_.rows = kind == Kind::Data ? RowsUse::PerFrame : RowsUse::Ignore;
    }

    Hold& held() {
        if (!std::holds_alternative<Hold>(port_.pairing)) port_.pairing = Hold{};
        return std::get<Hold>(port_.pairing);
    }

    Interval& intervalled() {
        if (!std::holds_alternative<Interval>(port_.pairing)) port_.pairing = Interval{};
        return std::get<Interval>(port_.pairing);
    }

    void timed() {
        if (std::holds_alternative<Hold>(port_.pairing) ||
            std::holds_alternative<Interval>(port_.pairing))
            return;
        if (carries_frames(port_.kind)) {
            held();
        } else {
            intervalled();
        }
    }

    InputPort port_;
};

/// An output port, built the way a query reads it:
/// `Output::video("mask").pixel_format("gray")`, `Output::like("v")`.
class Output {
public:
    /// A video output: the clock input's format until `following`, `size`
    /// or `pixel_format` say otherwise.
    static Output video(std::string name) { return Output(std::move(name), Kind::Video); }
    static Output audio(std::string name) { return Output(std::move(name), Kind::Audio); }
    /// A data output of JSON rows.
    static Output rows(std::string name) {
        Output output(std::move(name), Kind::Data);
        output.port_.format = DataFormat{"json"};
        return output;
    }
    static Output packets(std::string name) { return Output(std::move(name), Kind::Packets); }

    /// An output named after input `port`, of its kind and in its format: a
    /// filter's `v` out for its `v` in.
    static Output like(std::string port) {
        Output output(port, Kind::Video);
        output.port_.named_like = true;
        output.port_.like = Like{std::move(port), std::nullopt, std::nullopt};
        return output;
    }

    /// In input `port`'s format.
    template <class Self>
    Self&& following(this Self&& self, std::string port) {
        self.liked().port = std::move(port);
        self.port_.format.reset();
        return std::forward<Self>(self);
    }

    /// In this pixel format: of the input it follows, the clock input when it
    /// follows none, or of the size `size` gave.
    template <class Self>
    Self&& pixel_format(this Self&& self, std::string pix_fmt) {
        if (auto* video = self.template own<VideoFormat>()) {
            video->pix_fmt = std::move(pix_fmt);
        } else {
            self.liked().pixel_format = std::move(pix_fmt);
        }
        return std::forward<Self>(self);
    }

    /// In this sample format, of the input it follows or the clock input.
    template <class Self>
    Self&& sample_format(this Self&& self, std::string sample_fmt) {
        if (auto* audio = self.template own<AudioFormat>()) {
            audio->sample_fmt = std::move(sample_fmt);
        } else {
            self.liked().sample_format = std::move(sample_fmt);
        }
        return std::forward<Self>(self);
    }

    /// Pictures of `width` x `height`, in the pixel format `pixel_format`
    /// names.
    template <class Self>
    Self&& size(this Self&& self, std::uint32_t width, std::uint32_t height) {
        std::string pix_fmt;
        if (self.port_.like && self.port_.like->pixel_format) pix_fmt = *self.port_.like->pixel_format;
        self.port_.like.reset();
        self.port_.format = VideoFormat{width, height, std::move(pix_fmt), std::nullopt};
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& video_format(this Self&& self, VideoFormat format) {
        self.port_.like.reset();
        self.port_.format = std::move(format);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& audio_format(this Self&& self, AudioFormat format) {
        self.port_.like.reset();
        self.port_.format = std::move(format);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& coded(this Self&& self, CodedStream coded) {
        self.port_.like.reset();
        self.port_.format = std::move(coded);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& time_base(this Self&& self, Rational time_base) {
        self.port_.time_base = time_base;
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& latency(this Self&& self, double seconds) {
        self.port_.latency = seconds;
        return std::forward<Self>(self);
    }

    /// The rows written here are `T`s.
    template <class T, class Self>
    Self&& schema(this Self&& self) {
        self.port_.schema = schema_of<T>();
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& schema_json(this Self&& self, std::string schema) {
        self.port_.schema = std::move(schema);
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& row(this Self&& self, std::uint32_t row) {
        self.port_.row = row;
        return std::forward<Self>(self);
    }

    const OutputPort& port() const& { return port_; }
    OutputPort port() && { return std::move(port_); }

private:
    Output(std::string name, Kind kind) {
        port_.name = std::move(name);
        port_.kind = kind;
    }

    Like& liked() {
        if (!port_.like) port_.like = Like{};
        return *port_.like;
    }

    template <class F>
    F* own() {
        return port_.format ? std::get_if<F>(&*port_.format) : nullptr;
    }

    OutputPort port_;
};

/// What drives a node's calls.
struct Clock {
    enum class Kind {
        /// The input `port`: one call per stride of its frames.
        Input,
        /// A generator, `rate` ticks a second.
        Rate,
        /// A rate clock at input `port`'s rate.
        RateOf,
        /// The node emits when it has something.
        SelfClocked,
    };

    Kind kind = Kind::SelfClocked;
    std::string port;
    Rational rate;

    static Clock input(std::string port) { return {Kind::Input, std::move(port), {}}; }
    static Clock at(Rational rate) { return {Kind::Rate, {}, rate}; }
    static Clock rate_of(std::string port) { return {Kind::RateOf, std::move(port), {}}; }
    static Clock self_clocked() { return {Kind::SelfClocked, {}, {}}; }

    friend bool operator==(const Clock&, const Clock&) = default;
};

/// One input a call binds: its streams, in the order the call names them.
struct Binding {
    std::string input;
    std::vector<StreamHint> streams;

    friend bool operator==(const Binding&, const Binding&) = default;
};

/// The inputs a call binds, each with what the compiler knows of its
/// streams: how many a many port takes, and their rates.
///
/// At `init` the node is shaped again from the streams bound there, each
/// carrying the hint the compiler's `shape` was asked with, so the
/// instance's shape is the one the plan was made from.
class Bound {
public:
    Bound() = default;
    /// `names` bound, one stream each, at rates unknown.
    Bound(std::initializer_list<std::string_view> names);
    explicit Bound(std::vector<Binding> inputs) : inputs_(std::move(inputs)) {}
    /// Names alone, one stream each at a rate unknown; a name written again
    /// is one stream more.
    static Bound names(const std::vector<std::string>& names);

    /// The streams `init` binds, each with its hint.
    static Bound of(const std::vector<BoundStream>& streams);

    /// `port` bound to streams at these rates, none where unknown, in place
    /// of whatever it was bound to.
    template <class Self>
    Self&& bind(this Self&& self, std::string_view port, std::vector<std::optional<Rational>> rates) {
        self.rebind(port, rates);
        return std::forward<Self>(self);
    }

    /// Every stream of `port` at `rate`; one stream when it was not bound.
    template <class Self>
    Self&& rate(this Self&& self, std::string_view port, Rational rate) {
        self.set_rate(port, rate);
        return std::forward<Self>(self);
    }

    /// Every input the call binds, in its order.
    const std::vector<Binding>& inputs() const { return inputs_; }

    /// Whether the call binds input `port`.
    bool has(std::string_view port) const { return find(port) != nullptr; }

    /// The streams bound to `port`; none when the call leaves it out.
    const std::vector<StreamHint>& streams(std::string_view port) const;

    /// How many streams the call binds to `port`.
    std::size_t count(std::string_view port) const { return streams(port).size(); }

    /// The rate of `port`'s first stream, the only one of a single port: the
    /// clock's, when `port` is the clock input or a `rate_of` clock's.
    std::optional<Rational> rate_of(std::string_view port) const;

    friend bool operator==(const Bound&, const Bound&) = default;

private:
    const Binding* find(std::string_view port) const;
    Binding* find(std::string_view port);
    void rebind(std::string_view port, const std::vector<std::optional<Rational>>& rates);
    void set_rate(std::string_view port, Rational rate);

    std::vector<Binding> inputs_;
};

/// A node's ports and clock as the host is handed them, field for field
/// the WIT's `node-shape`: what `Shape::resolve` answers.
struct NodeShape {
    std::vector<InputPort> inputs;
    std::vector<OutputPort> outputs;
    std::optional<Clock> clock;
    bool pure = false;
    bool one_to_one = false;
    bool bounded = true;
    std::vector<std::string> relation;

    const InputPort* find_input(std::string_view name) const;
    const OutputPort* find_output(std::string_view name) const;

    /// The clock input's name, when an input is the clock.
    std::optional<std::string_view> clock_input() const;
};

/// A node's ports and clock for one call's params, built the way a query
/// reads them:
///
///     Shape()
///         .input(Input::video("v").clock().pixel_formats({"rgba"}))
///         .output(Output::rows("spots").schema<Spot>())
///         .pure()
///
/// No ports, impure, not one-to-one and bounded until told otherwise.
class Shape {
public:
    template <class Self>
    Self&& input(this Self&& self, Input input) {
        self.shape_.inputs.push_back(std::move(input).port());
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& output(this Self&& self, Output output) {
        self.shape_.outputs.push_back(std::move(output).port());
        return std::forward<Self>(self);
    }

    /// Ticks `rate` times a second.
    template <class Self>
    Self&& rate(this Self&& self, Rational rate) {
        self.shape_.clock = Clock::at(rate);
        return std::forward<Self>(self);
    }

    /// Ticks at input `port`'s rate, which the compiler reads off its first
    /// stream.
    template <class Self>
    Self&& rate_of(this Self&& self, std::string port) {
        self.shape_.clock = Clock::rate_of(std::move(port));
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& self_clocked(this Self&& self) {
        self.shape_.clock = Clock::self_clocked();
        return std::forward<Self>(self);
    }

    /// Every call depends only on what it was handed, so the host may spread
    /// the node over workers.
    template <class Self>
    Self&& pure(this Self&& self) {
        self.shape_.pure = true;
        return std::forward<Self>(self);
    }

    /// One frame out per frame in on the clock's outputs, each at its tick's
    /// pts.
    template <class Self>
    Self&& one_to_one(this Self&& self) {
        self.shape_.one_to_one = true;
        return std::forward<Self>(self);
    }

    /// Whether a rate or self-clocked node ends by itself; one that does not
    /// is planned as live.
    template <class Self>
    Self&& bounded(this Self&& self, bool bounded) {
        self.shape_.bounded = bounded;
        return std::forward<Self>(self);
    }

    /// One relation row of a source read in FROM, as a JSON object.
    template <class Self>
    Self&& relation_row(this Self&& self, std::string row) {
        self.shape_.relation.push_back(std::move(row));
        return std::forward<Self>(self);
    }

    /// The ports as built, before `resolve`.
    const NodeShape& built() const { return shape_; }

    /// The shape the host is handed for a call binding `bound`: the clock
    /// settled, outputs following an unbound input left out, and every rule
    /// the host would refuse it by checked here first, with the port named.
    Result<NodeShape> resolve(const Bound& bound) const;

private:
    NodeShape shape_;
};

}  // namespace ffrwd
