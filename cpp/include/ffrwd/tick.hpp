#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bytes.hpp"
#include "fields.hpp"
#include "result.hpp"
#include "time.hpp"
#include "types.hpp"

namespace ffrwd {

/// What a tick reads from: the host's resource in a module, a `mock::Tick`
/// in a test.
class Source {
public:
    virtual ~Source() = default;
    virtual std::int64_t pts() const = 0;
    virtual std::uint64_t ordinal() const = 0;
    virtual Rational time_base() const = 0;
    virtual bool last() const = 0;
    virtual std::vector<std::uint32_t> streams(std::string_view port) const = 0;
    virtual StreamInfo info(std::uint32_t id) const = 0;
    virtual std::optional<Feed> feed(std::uint32_t id) const = 0;
    virtual std::vector<Feed> ended_feeds(std::uint32_t id) const = 0;
    virtual std::vector<Frame> frames(std::uint32_t id) const = 0;
    virtual Bytes fetch(std::uint32_t id, std::uint32_t index) const = 0;
    virtual std::vector<Message> messages(std::uint32_t id) const = 0;
    virtual std::vector<Packet> packets(std::uint32_t id) const = 0;
    virtual std::vector<TimedRows> earlier_rows(std::uint32_t id) const = 0;
};

/// One call's inputs, held by the host for exactly that call. Streams are
/// named by the ids `init` gave them; an id it did not give, or an index past
/// a stream's frames, is a fault that stops the run.
class Tick {
public:
    Tick(const Source& source, const std::map<std::uint32_t, std::string>& ports,
         const std::set<std::uint32_t>& timing)
        : source_(source), ports_(ports), timing_(timing) {}

    Tick(const Tick&) = delete;
    Tick& operator=(const Tick&) = delete;

    /// The stream read for its timing alone that this call tried to fetch.
    std::optional<std::uint32_t> refused() const { return refused_; }

    /// The tick's time in `time_base()`: an input clock's first frame this
    /// call, a rate clock's tick number, a self-clocked node's microseconds
    /// since its first call.
    std::int64_t pts() const { return source_.pts(); }

    /// The tick's number in the run, from 0, counted over every instance of
    /// the node: on a frame clock the clock stream's frame number from the
    /// run's first, strides counted as one; on a rate clock `pts`. A node
    /// that numbers things by frame counts with it and stays pure.
    std::uint64_t ordinal() const { return source_.ordinal(); }

    /// The clock input's time base, the inverse of a rate clock's rate, or
    /// microseconds.
    Rational time_base() const { return source_.time_base(); }

    /// The tick's time in seconds.
    double seconds() const { return time_base().seconds(pts()); }

    /// Whether this is the instance's final call, which happens exactly once.
    bool last() const { return source_.last(); }

    /// The streams bound to input `port`, in `init`'s order.
    std::vector<std::uint32_t> streams(std::string_view port) const { return source_.streams(port); }

    /// The first stream bound to input `port`: the only one of a single
    /// port, none for an optional port the call left out.
    std::optional<std::uint32_t> stream(std::string_view port) const {
        auto ids = streams(port);
        if (ids.empty()) return std::nullopt;
        return ids.front();
    }

    /// The stream as the host knows it this tick, time base included.
    StreamInfo info(std::uint32_t id) const { return source_.info(id); }

    /// A hold input's feed as it stands this tick; none while nothing shows.
    std::optional<Feed> feed(std::uint32_t id) const { return source_.feed(id); }

    /// A hold input's feeds that ended since this instance's previous call,
    /// every one before on its first call, oldest first, each with `ends`
    /// set to the last tick it showed on. Empty on every other input.
    std::vector<Feed> ended_feeds(std::uint32_t id) const { return source_.ended_feeds(id); }

    /// The frames this tick hands on stream `id`, oldest first.
    std::vector<Frame> frames(std::uint32_t id) const { return source_.frames(id); }

    /// The newest frame this tick hands on stream `id`: the only one a
    /// window of one, a hold input or an audio input hands.
    std::optional<Frame> frame(std::uint32_t id) const {
        auto all = frames(id);
        if (all.empty()) return std::nullopt;
        return std::move(all.back());
    }

    /// Frame `index`'s bytes, copied on demand: pixels tightly packed, or
    /// interleaved samples. An input read for its timing alone has none: the
    /// call gets nothing back, and ends the run with the port named once it
    /// returns, where the host would fault.
    Bytes fetch(std::uint32_t id, std::uint32_t index) const {
        if (timing_.contains(id)) {
            if (!refused_) refused_ = id;
            return {};
        }
        return source_.fetch(id, index);
    }

    /// The messages this tick hands on data stream `id`, in pts order.
    std::vector<Message> messages(std::uint32_t id) const { return source_.messages(id); }

    /// The packets this tick hands on packets stream `id`, in decode order.
    std::vector<Packet> packets(std::uint32_t id) const { return source_.packets(id); }

    /// The rows of the ticks this instance did not process, on a state
    /// input. A node with a state input has them folded for it, so it rarely
    /// reads them itself.
    std::vector<TimedRows> earlier_rows(std::uint32_t id) const { return source_.earlier_rows(id); }

    /// Every row this tick hands on stream `id`, read as `T`: a data
    /// stream's messages, or the rows riding a frame stream's frames.
    template <class T>
    Result<std::vector<T>> rows(std::uint32_t id) const {
        std::vector<T> read;
        auto on = [&](const Error& error) { return fail("on `" + std::string(port(id)) + "`: " + error.message); };
        auto arrived = messages(id);
        if (arrived.empty()) {
            for (const Frame& frame : frames(id)) {
                auto rows = frame.read_rows<T>();
                if (!rows) return on(rows.error());
                for (T& row : *rows) read.push_back(std::move(row));
            }
        } else {
            for (const Message& message : arrived) {
                auto row = message.row<T>();
                if (!row) return on(row.error());
                read.push_back(std::move(*row));
            }
        }
        return read;
    }

    /// The input port stream `id` is bound to.
    std::string_view port(std::uint32_t id) const {
        auto found = ports_.find(id);
        return found == ports_.end() ? std::string_view("?") : std::string_view(found->second);
    }

private:
    const Source& source_;
    const std::map<std::uint32_t, std::string>& ports_;
    const std::set<std::uint32_t>& timing_;
    mutable std::optional<std::uint32_t> refused_;
};

}  // namespace ffrwd
