#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "fields.hpp"

namespace ffrwd {

/// A caption: text from `start_t` to `end_t`, in seconds. What a query's
/// `cue[]` is.
struct Cue {
    double start_t = 0.0;
    double end_t = 0.0;
    std::string text;

    FFRWD_FIELDS(start_t, end_t, text)

    /// Whether the cue is showing at `t` seconds: from its start up to, not
    /// including, its end.
    bool covers(double t) const { return start_t <= t && t < end_t; }

    friend bool operator==(const Cue&, const Cue&) = default;
};

/// Cues kept from the tick they arrive on until they end: what a node that
/// shows cues holds between ticks.
class Cues {
public:
    /// Keeps `cue`, in start order.
    void add(Cue cue) {
        auto at = std::partition_point(held_.begin(), held_.end(),
                                       [&](const Cue& held) { return held.start_t <= cue.start_t; });
        held_.insert(at, std::move(cue));
    }

    /// The cues showing at `t` seconds, oldest first.
    std::vector<const Cue*> at(double t) const {
        std::vector<const Cue*> showing;
        for (const Cue& cue : held_)
            if (cue.covers(t)) showing.push_back(&cue);
        return showing;
    }

    /// Forgets every cue that ended by `t` seconds.
    void drop_ended(double t) {
        std::erase_if(held_, [t](const Cue& cue) { return cue.end_t <= t; });
    }

    std::size_t size() const { return held_.size(); }
    bool empty() const { return held_.empty(); }

private:
    std::vector<Cue> held_;
};

/// One span a key is seen across, named by when it started and by its
/// number. A row type with a `Span` field writes its `start_t` and `id` in
/// that field's place, which is how a reader of the rows tells apart two
/// spans that start on one tick.
struct Span {
    /// The seconds of the tick it was first seen on: `start_t` in the rows.
    double start_t = 0.0;
    /// How many spans began before this one: `id` in the rows.
    std::uint64_t id = 0;
    /// How many ticks old it is: 0 on the tick it starts. Not written.
    std::uint64_t age = 0;

    static constexpr bool ffrwd_flatten = true;
    FFRWD_FIELDS(start_t, id)

    friend bool operator==(const Span&, const Span&) = default;
};

/// Which span a sighting belongs to, for a node writing a row per tick while
/// something lasts, each row carrying the `start_t` of its span. Call `tick`
/// once a tick, then `see` for each thing seen on it.
///
/// A span ends when its key goes unseen for more than `gap` ticks (0 by
/// default: one tick unseen ends it), or once it is `longest` ticks old; the
/// next sighting starts a new one. `Spans<>` tracks one thing, keyed by
/// nothing.
template <class K = std::monostate>
class Spans {
public:
    /// How many ticks in a row a span's key may go unseen and the span still
    /// go on.
    template <class Self>
    Self&& gap(this Self&& self, std::uint64_t ticks) {
        self.gap_ = ticks;
        return std::forward<Self>(self);
    }

    /// The most ticks one span lasts; the tick after its last starts a new
    /// span for a key still seen.
    template <class Self>
    Self&& longest(this Self&& self, std::uint64_t ticks) {
        self.longest_ = std::max<std::uint64_t>(ticks, 1);
        return std::forward<Self>(self);
    }

    /// Starts the tick at `t` seconds, ending the spans that ran out.
    void tick(double t) {
        std::uint64_t tick = now_ ? now_->first + 1 : 0;
        now_ = {tick, t};
        std::erase_if(open_, [&](const Open& open) {
            std::uint64_t unseen = tick - open.seen - 1;
            std::uint64_t age = tick - open.started;
            return !(unseen <= gap_ && (!longest_ || age < *longest_));
        });
    }

    /// A sighting of `key` on this tick: the span it belongs to, started
    /// here when none is open for it.
    Span see(K key = K{}) {
        if (!now_) now_ = {0, 0.0};
        auto [tick, t] = *now_;
        for (Open& open : open_) {
            if (open.key == key) {
                open.seen = tick;
                open.span.age = tick - open.started;
                return open.span;
            }
        }
        Span span{t, started_, 0};
        ++started_;
        open_.push_back({std::move(key), span, tick, tick});
        return span;
    }

    /// How many spans are open.
    std::size_t open() const { return open_.size(); }

private:
    struct Open {
        K key;
        Span span;
        std::uint64_t started;
        std::uint64_t seen;
    };

    std::vector<Open> open_;
    std::uint64_t gap_ = 0;
    std::optional<std::uint64_t> longest_;
    std::optional<std::pair<std::uint64_t, double>> now_;
    std::uint64_t started_ = 0;
};

}  // namespace ffrwd
