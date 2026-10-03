#pragma once

#include <concepts>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fields.hpp"
#include "json.hpp"
#include "out.hpp"
#include "params.hpp"
#include "result.hpp"
#include "shape.hpp"
#include "tick.hpp"
#include "types.hpp"

namespace ffrwd {

/// What `init` is handed: the streams bound to the node's inputs and the
/// outputs the query reads.
class Init {
public:
    Init(const std::vector<BoundStream>& streams, const std::vector<std::string>& latched,
         const NodeShape& shape)
        : streams_(streams), latched_(latched), shape_(shape) {}

    /// Every bound stream: ports in the shape's order, a port's streams in
    /// the order the query named them.
    const std::vector<BoundStream>& all() const { return streams_; }

    /// The streams bound to input `port`.
    std::vector<const BoundStream*> streams(std::string_view port) const {
        std::vector<const BoundStream*> found;
        for (const BoundStream& stream : streams_)
            if (stream.port == port) found.push_back(&stream);
        return found;
    }

    /// The one stream bound to input `port`, or none when the call left it
    /// out.
    const BoundStream* optional(std::string_view port) const {
        for (const BoundStream& stream : streams_)
            if (stream.port == port) return &stream;
        return nullptr;
    }

    /// The one stream bound to required input `port`.
    Result<BoundStream> stream(std::string_view port) const {
        if (const BoundStream* found = optional(port)) return *found;
        return fail("no stream is bound to `" + std::string(port) + "`");
    }

    /// Whether the query reads output `port`; one it does not may be left
    /// unmade.
    bool latched(std::string_view port) const {
        for (const std::string& name : latched_)
            if (name == port) return true;
        return false;
    }

    /// The instance's shape, resolved for what the call binds.
    const NodeShape& shape() const { return shape_; }

private:
    const std::vector<BoundStream>& streams_;
    const std::vector<std::string>& latched_;
    const NodeShape& shape_;
};

/// One row arriving on a state input.
struct StateRow {
    std::string_view port;
    std::uint32_t id = 0;
    /// When it arrived, in the stream's time base.
    std::int64_t pts = 0;
    /// The same, in seconds.
    double seconds = 0.0;
    /// The row: one JSON object.
    std::string_view json;

    /// The row read as a `T`.
    template <class T>
    Result<T> row() const {
        auto read = parse<T>(json);
        if (!read) return fail("on `" + std::string(port) + "`: " + read.error().message);
        return read;
    }
};

/// What `describe` reports. A node's format lists stay empty: its ports say
/// what they accept.
struct Meta {
    std::string name;
    std::string version;
    std::string params_schema;
    std::string rows_schema;
    std::vector<std::string> rows_language;
};

/// A node: typed inputs, typed outputs and a clock. A module derives its own
/// type from `Node<Self, Params>` and hands it to `FFRWD_EXPORT`:
///
///     struct Dim : ffrwd::Node<Dim, DimParams> {
///         static constexpr std::string_view name = "dim";
///         static constexpr std::string_view version = "0.1.0";
///         static constexpr std::string_view params_schema = R"({...})";
///
///         static ffrwd::Result<ffrwd::Shape> shape(const DimParams&, const ffrwd::Bound&);
///         static ffrwd::Result<Dim> init(DimParams, const ffrwd::Init&);
///         ffrwd::Status process(const ffrwd::Tick&, ffrwd::Out&);
///     };
///
/// The host calls `describe` and `shape` at compile time, then once per
/// instance `init`, then `process` once a tick, the last call exactly once
/// with `Tick::last` set. The params are read against `params_schema`
/// before any of these sees them, and the rows of a state input are folded
/// into the node before each `process`.
template <class Self, class P = NoParams>
struct Node {
    /// The params, read from the call's JSON by the fields `FFRWD_FIELDS`
    /// names.
    using Params = P;

    /// The JSON schema of the params: what the compiler checks a call
    /// against, and whose defaults fill in what a call leaves out.
    static constexpr std::string_view params_schema = NO_PARAMS;
    /// The JSON schema of one row `Out::report` writes; empty when the node
    /// writes none.
    static constexpr std::string_view rows_schema = "";
    /// Ordered param names: the language of the node's JSON outputs is the
    /// first of these the call sets.
    static constexpr std::array<std::string_view, 0> rows_language{};

    /// New params between ticks, whose shape is the instance's. Params equal
    /// to the ones in force never reach it. Refuses unless the node declares
    /// its own.
    Status set_params(const P&) {
        return fail(std::string(Self::name) + " cannot change its params while it runs");
    }

    /// One row of a state input, folded into the node before the `process`
    /// that follows: the rows of ticks this instance did not process first,
    /// oldest first, then this tick's. A node declaring a state input
    /// declares its own.
    Status fold(const StateRow& row) {
        return fail(std::string(Self::name) + " declares `" + std::string(row.port) +
                    "` as state, so it folds that input's rows: give it a `fold`");
    }
};

/// What a type exported as a node provides.
template <class N>
concept NodeType = requires(N& node, const typename N::Params& params, const Bound& bound,
                            const Init& init, const Tick& tick, Out& out, const StateRow& row) {
    typename N::Params;
    { std::string_view(N::name) };
    { std::string_view(N::version) };
    { std::string_view(N::params_schema) };
    { std::string_view(N::rows_schema) };
    { N::shape(params, bound) } -> std::same_as<Result<Shape>>;
    { N::init(typename N::Params(params), init) } -> std::same_as<Result<N>>;
    { node.set_params(typename N::Params(params)) } -> std::same_as<Status>;
    { node.fold(row) } -> std::same_as<Status>;
    { node.process(tick, out) } -> std::same_as<Status>;
};

/// The call sequence around a node, the same in a module and in a test:
/// params read against the schema, the shape resolved, state rows folded,
/// emissions checked.
template <NodeType N>
class Runner {
public:
    static Meta describe() {
        Meta meta{std::string(N::name), std::string(N::version), std::string(N::params_schema),
                  std::string(N::rows_schema), {}};
        for (auto name : N::rows_language) meta.rows_language.emplace_back(name);
        return meta;
    }

    /// The shape for `params` and the inputs `bound` binds, as the host is
    /// handed it.
    static Result<NodeShape> shape(std::string_view params, const Bound& bound) {
        FFRWD_LET(read, read_params<typename N::Params>(N::params_schema, params));
        FFRWD_LET(built, N::shape(read.first, bound));
        return built.resolve(bound);
    }

    /// Opens an instance on `bound`.
    static Result<Runner> init(std::vector<BoundStream> bound, std::vector<std::string> latched,
                               std::string_view params) {
        FFRWD_LET(read, read_params<typename N::Params>(N::params_schema, params));
        Bound hints = Bound::of(bound);
        FFRWD_LET(built, N::shape(read.first, hints));
        FFRWD_LET(resolved, built.resolve(hints));
        std::map<std::uint32_t, std::string> ports;
        std::map<std::uint32_t, std::string> timing;
        std::vector<std::pair<std::string, std::uint32_t>> state;
        for (const BoundStream& stream : bound) {
            ports[stream.id] = stream.port;
            const InputPort* input = resolved.find_input(stream.port);
            if (input && input->accepts.wants == Wants::Timing) timing[stream.id] = stream.port;
            if (input && input->rows == RowsUse::State) state.emplace_back(stream.port, stream.id);
        }
        FFRWD_LET(node, N::init(std::move(read.first), Init(bound, latched, resolved)));
        Out out(resolved);
        out.timing(timing);
        std::set<std::uint32_t> timing_ids;
        for (const auto& [id, port] : timing) timing_ids.insert(id);
        return Runner(std::move(node), std::move(resolved), std::move(read.second), std::move(ports),
                      std::move(state), std::move(timing_ids), std::move(out));
    }

    /// New params between ticks; equal ones are taken without asking the
    /// node.
    Status set_params(std::string_view params) {
        FFRWD_LET(read, read_params<typename N::Params>(N::params_schema, params));
        if (read.second == params_) return {};
        FFRWD_TRY(node_.set_params(std::move(read.first)));
        params_ = std::move(read.second);
        return {};
    }

    /// One tick read from `source`.
    Result<Emitted> process(const Source& source) {
        out_.take();
        Tick tick(source, ports_, timing_);
        out_.begin(tick.time_base());
        FFRWD_TRY(fold(tick));
        Status processed = node_.process(tick, out_);
        if (auto id = tick.refused())
            return fail(std::string(N::name) + " fetched a frame of `" + std::string(tick.port(*id)) +
                        "`, which it reads for its timing alone: the host carries none of its bytes");
        FFRWD_TRY(std::move(processed));
        return out_.take();
    }

    N& node() { return node_; }
    const N& node() const { return node_; }

    /// The instance's shape, resolved.
    const NodeShape& resolved() const { return shape_; }

private:
    Runner(N node, NodeShape shape, Json params, std::map<std::uint32_t, std::string> ports,
           std::vector<std::pair<std::string, std::uint32_t>> state, std::set<std::uint32_t> timing,
           Out out)
        : node_(std::move(node)),
          shape_(std::move(shape)),
          params_(std::move(params)),
          ports_(std::move(ports)),
          state_(std::move(state)),
          timing_(std::move(timing)),
          out_(std::move(out)) {}

    Status fold(const Tick& tick) {
        for (const auto& [port, id] : state_) {
            Rational time_base = tick.info(id).time_base;
            for (const TimedRows& timed : tick.earlier_rows(id))
                for (const std::string& json : timed.rows)
                    FFRWD_TRY(node_.fold(StateRow{port, id, timed.pts, time_base.seconds(timed.pts), json}));
        }
        for (const auto& [port, id] : state_) {
            Rational time_base = tick.info(id).time_base;
            std::vector<std::pair<std::int64_t, std::string>> arrived;
            for (Message& message : tick.messages(id)) arrived.emplace_back(message.pts, std::move(message.data));
            for (Frame& frame : tick.frames(id))
                for (std::string& json : frame.rows) arrived.emplace_back(frame.pts, std::move(json));
            for (const auto& [pts, json] : arrived)
                FFRWD_TRY(node_.fold(StateRow{port, id, pts, time_base.seconds(pts), json}));
        }
        return {};
    }

    N node_;
    NodeShape shape_;
    Json params_;
    std::map<std::uint32_t, std::string> ports_;
    std::vector<std::pair<std::string, std::uint32_t>> state_;
    std::set<std::uint32_t> timing_;
    Out out_;
};

}  // namespace ffrwd
