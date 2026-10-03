#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "bytes.hpp"
#include "fields.hpp"
#include "result.hpp"
#include "rows.hpp"
#include "shape.hpp"
#include "time.hpp"
#include "types.hpp"

namespace ffrwd {

/// New bytes in the port's format.
struct FramePayload {
    std::int64_t pts = 0;
    std::optional<std::int64_t> duration;
    Bytes data;
};

/// An input frame's bytes leaving uncopied: frame `index` of stream `id`
/// this tick.
struct SamePayload {
    std::int64_t pts = 0;
    std::optional<std::int64_t> duration;
    std::uint32_t id = 0;
    std::uint32_t index = 0;
};

/// One message; empty is a progress mark, never delivered.
struct MessagePayload {
    std::int64_t pts = 0;
    std::string data;
};

/// One thing leaving on a port. Its pts and duration are in the port's time
/// base.
using Payload = std::variant<FramePayload, SamePayload, MessagePayload, Packet>;

struct Emission {
    std::string port;
    Payload payload;
};

/// What one tick produced.
struct Emitted {
    std::vector<Emission> items;
    /// Rows for the run's rows output.
    std::vector<std::string> reports;
    bool finished = false;

    /// The messages that left on `port`, as `(pts, text)`.
    std::vector<std::pair<std::int64_t, std::string>> messages(std::string_view port) const;

    /// The payloads that left on `port`, in order.
    std::vector<const Payload*> on(std::string_view port) const;
};

/// Where a tick's emissions go. Each one is checked as it is made: the port
/// is one the shape declares and of the payload's kind, and its pts never go
/// back on that port, within a call or across calls, nor a packet's dts.
class Out {
public:
    explicit Out(const NodeShape& shape);

    /// The streams read for their timing alone, by id, with their ports.
    void timing(std::map<std::uint32_t, std::string> timing) { timing_ = std::move(timing); }
    void begin(Rational clock) { clock_ = clock; }
    Emitted take() { return std::exchange(emitted_, Emitted{}); }

    /// The time base `port` is counted in: its own, or the clock's.
    Result<Rational> time_base(std::string_view port) const;

    /// `seconds` as a pts on `port`.
    Result<std::int64_t> pts(std::string_view port, double seconds) const;

    /// The last pts that left on `port`, which the next may not be under.
    std::optional<std::int64_t> last(std::string_view port) const;

    /// New bytes on a video or audio port: one picture tightly packed, or a
    /// run of interleaved samples.
    Status frame(std::string_view port, std::int64_t pts, std::optional<std::int64_t> duration,
                 Bytes data);

    /// Frame `index` of stream `id` leaving on `port` uncopied, at `pts`.
    /// Its format has to be the port's, and its input not one read for its
    /// timing alone.
    Status same(std::string_view port, std::int64_t pts, std::optional<std::int64_t> duration,
                std::uint32_t id, std::uint32_t index);

    /// `frame` of stream `id` leaving on `port` unchanged at its own pts and
    /// duration: a filter passing a picture through, on a port in the
    /// stream's time base.
    Status pass(std::string_view port, std::uint32_t id, const Frame& frame) {
        return same(port, frame.pts, frame.duration, id, frame.index);
    }

    /// One message on a data port.
    Status message(std::string_view port, std::int64_t pts, std::string data);

    /// `row` as one JSON message on `port` at `pts`.
    template <class T>
    Status row(std::string_view port, std::int64_t pts, const T& row) {
        return message(port, pts, to_json(row).dump());
    }

    /// Each of `rows` as a message on `port` at `pts`.
    template <class T>
    Status rows(std::string_view port, std::int64_t pts, const std::vector<T>& rows) {
        for (const T& each : rows) FFRWD_TRY(row(port, pts, each));
        return {};
    }

    /// `cue` on `port`, stamped at its start.
    Status cue(std::string_view port, const Cue& cue);

    /// A progress mark: nothing more will leave on `port` stamped before
    /// `pts`. Never delivered.
    Status progress(std::string_view port, std::int64_t pts) { return message(port, pts, {}); }

    /// One packet on a packets port, in decode order: its dts never
    /// decreases.
    Status packet(std::string_view port, Packet packet);

    /// One row for the run's rows output, as a sink writes them.
    template <class T>
    void report(const T& row) {
        emitted_.reports.push_back(to_json(row).dump());
    }

    /// Nothing more will leave: the host makes the last call and ends every
    /// output. A node on an input clock ends with it and need not say so.
    void finish() { emitted_.finished = true; }

private:
    struct Port {
        std::string name;
        Kind kind;
        std::optional<Rational> time_base;
        std::optional<std::int64_t> last_pts;
        std::optional<std::int64_t> last_dts;
    };

    Result<Port*> port(std::string_view name, std::initializer_list<Kind> kinds);
    Status stamp(std::string_view name, std::initializer_list<Kind> kinds, std::int64_t pts);
    void push(std::string_view port, Payload payload);

    std::vector<Port> ports_;
    Rational clock_ = Rational::micros();
    Emitted emitted_;
    std::map<std::uint32_t, std::string> timing_;
};

}  // namespace ffrwd
