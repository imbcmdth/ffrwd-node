#pragma once

// A node's calls on the host, for its unit tests: a `Harness` runs the same
// call sequence a module does, on a `mock::Tick` built by hand.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "node.hpp"

namespace ffrwd::mock {

/// One tick's inputs, built by hand. An id it was not told of, or an index
/// past a stream's frames, stops the test, as the host stops the run.
class Tick final : public Source {
public:
    /// A tick at `pts` of a clock counted in `time_base`, with no streams,
    /// the run's first.
    Tick(std::int64_t pts, Rational time_base) : pts_(pts), time_base_(time_base) {}

    Tick(Tick&&) = default;
    Tick& operator=(Tick&&) = default;

    /// The tick's number in the run: what a worker handed every other tick
    /// sees on its own.
    template <class Self>
    Self&& ordinal(this Self&& self, std::uint64_t ordinal) {
        self.ordinal_ = ordinal;
        return std::forward<Self>(self);
    }

    /// `stream` bound on its port.
    template <class Self>
    Self&& bind(this Self&& self, const BoundStream& stream) {
        self.streams_.emplace_back(stream.port, stream.id);
        self.infos_[stream.id] = stream.info;
        return std::forward<Self>(self);
    }

    /// The instance's final call.
    template <class Self>
    Self&& last_call(this Self&& self) {
        self.last_ = true;
        return std::forward<Self>(self);
    }

    /// A frame of `data` at `pts` on stream `id`, after the ones already
    /// there.
    template <class Self>
    Self&& frame(this Self&& self, std::uint32_t id, std::int64_t pts, Bytes data) {
        return std::forward<Self>(self).frame_with(id, pts, std::nullopt, {}, std::move(data));
    }

    /// A frame with a duration and rows riding it.
    template <class Self>
    Self&& frame_with(this Self&& self, std::uint32_t id, std::int64_t pts,
                      std::optional<std::int64_t> duration, std::vector<std::string> rows,
                      Bytes data) {
        auto& frames = self.frames_[id];
        Frame frame{pts, std::uint32_t(frames.size()), duration, std::move(rows)};
        frames.emplace_back(std::move(frame), std::move(data));
        return std::forward<Self>(self);
    }

    /// A message on data stream `id`.
    template <class Self>
    Self&& message(this Self&& self, std::uint32_t id, std::int64_t pts, std::string data) {
        self.messages_[id].push_back(Message{pts, std::move(data)});
        return std::forward<Self>(self);
    }

    /// `row` as a JSON message on data stream `id`.
    template <class T, class Self>
    Self&& row(this Self&& self, std::uint32_t id, std::int64_t pts, const T& row) {
        self.messages_[id].push_back(Message{pts, to_json(row).dump()});
        return std::forward<Self>(self);
    }

    template <class Self>
    Self&& packet(this Self&& self, std::uint32_t id, Packet packet) {
        self.packets_[id].push_back(std::move(packet));
        return std::forward<Self>(self);
    }

    /// Hold input `id`'s feed as it stands on this tick.
    template <class Self>
    Self&& feed(this Self&& self, std::uint32_t id, Feed feed) {
        self.feeds_[id] = std::move(feed);
        return std::forward<Self>(self);
    }

    /// A feed of hold input `id` that ended since the instance's previous
    /// call, after the ones already there.
    template <class Self>
    Self&& ended(this Self&& self, std::uint32_t id, Feed feed) {
        self.ended_[id].push_back(std::move(feed));
        return std::forward<Self>(self);
    }

    /// Rows a state input received on a tick this instance did not process.
    template <class Self>
    Self&& earlier(this Self&& self, std::uint32_t id, std::int64_t pts, std::vector<std::string> rows) {
        self.earlier_[id].push_back(TimedRows{pts, std::move(rows)});
        return std::forward<Self>(self);
    }

    std::uint64_t ordinal_number() const { return ordinal_; }

    std::int64_t pts() const override { return pts_; }
    std::uint64_t ordinal() const override { return ordinal_; }
    Rational time_base() const override { return time_base_; }
    bool last() const override { return last_; }

    std::vector<std::uint32_t> streams(std::string_view port) const override {
        std::vector<std::uint32_t> ids;
        for (const auto& [name, id] : streams_)
            if (name == port) ids.push_back(id);
        return ids;
    }

    StreamInfo info(std::uint32_t id) const override {
        known(id);
        return infos_.at(id);
    }

    std::optional<Feed> feed(std::uint32_t id) const override {
        known(id);
        auto found = feeds_.find(id);
        if (found == feeds_.end()) return std::nullopt;
        return found->second;
    }

    std::vector<Feed> ended_feeds(std::uint32_t id) const override {
        known(id);
        auto found = ended_.find(id);
        return found == ended_.end() ? std::vector<Feed>{} : found->second;
    }

    std::vector<Frame> frames(std::uint32_t id) const override {
        known(id);
        std::vector<Frame> frames;
        if (auto found = frames_.find(id); found != frames_.end())
            for (const auto& [frame, data] : found->second) frames.push_back(frame);
        return frames;
    }

    Bytes fetch(std::uint32_t id, std::uint32_t index) const override {
        known(id);
        auto found = frames_.find(id);
        if (found == frames_.end() || index >= found->second.size()) {
            std::fprintf(stderr, "stream %u has no frame %u on this tick\n", id, index);
            std::abort();
        }
        return found->second[index].second.clone();
    }

    std::vector<Message> messages(std::uint32_t id) const override {
        known(id);
        auto found = messages_.find(id);
        return found == messages_.end() ? std::vector<Message>{} : found->second;
    }

    std::vector<Packet> packets(std::uint32_t id) const override {
        known(id);
        std::vector<Packet> packets;
        if (auto found = packets_.find(id); found != packets_.end())
            for (const Packet& packet : found->second) packets.push_back(packet.clone());
        return packets;
    }

    std::vector<TimedRows> earlier_rows(std::uint32_t id) const override {
        known(id);
        auto found = earlier_.find(id);
        return found == earlier_.end() ? std::vector<TimedRows>{} : found->second;
    }

private:
    void known(std::uint32_t id) const {
        if (!infos_.contains(id)) {
            std::fprintf(stderr, "stream %u is not bound on this tick\n", id);
            std::abort();
        }
    }

    std::int64_t pts_;
    std::uint64_t ordinal_ = 0;
    Rational time_base_;
    bool last_ = false;
    std::vector<std::pair<std::string, std::uint32_t>> streams_;
    std::map<std::uint32_t, StreamInfo> infos_;
    std::map<std::uint32_t, std::vector<std::pair<Frame, Bytes>>> frames_;
    std::map<std::uint32_t, std::vector<Message>> messages_;
    std::map<std::uint32_t, std::vector<Packet>> packets_;
    std::map<std::uint32_t, Feed> feeds_;
    std::map<std::uint32_t, std::vector<Feed>> ended_;
    std::map<std::uint32_t, std::vector<TimedRows>> earlier_;
};

/// A node opened on the host: `shape` and `init` as the host calls them,
/// every output latched, and ticks that come with the bound streams in
/// place, numbered from 0 on.
template <NodeType N>
class Harness {
public:
    /// Opens `N` with `params` on `bound`, shaped with each stream's hint
    /// as the compiler and `init` both shape it.
    static Result<Harness> open(std::string_view params, std::vector<BoundStream> bound) {
        FFRWD_LET(shape, Runner<N>::shape(params, Bound::of(bound)));
        std::vector<std::string> latched;
        for (const OutputPort& output : shape.outputs) latched.push_back(output.name);
        FFRWD_LET(runner, Runner<N>::init(bound, std::move(latched), params));
        std::optional<Rational> clock;
        if (const auto& resolved = runner.resolved().clock) {
            switch (resolved->kind) {
                case Clock::Kind::Input:
                    for (const BoundStream& stream : bound)
                        if (stream.port == resolved->port) {
                            clock = stream.info.time_base;
                            break;
                        }
                    break;
                case Clock::Kind::Rate: clock = resolved->rate.inverse(); break;
                case Clock::Kind::SelfClocked: clock = Rational::micros(); break;
                case Clock::Kind::RateOf: break;
            }
        }
        return Harness(std::move(runner), std::move(bound), clock);
    }

    /// The clock's time base, for a node whose clock is another input's
    /// rate.
    template <class Self>
    Self&& clock(this Self&& self, Rational time_base) {
        self.clock_ = time_base;
        return std::forward<Self>(self);
    }

    /// A tick at `pts` on the clock, every bound stream in place, numbered
    /// one past the last this harness processed.
    Tick tick(std::int64_t pts) const {
        if (!clock_) {
            std::fprintf(stderr, "the clock's time base is the rate of an input; give it with `clock`\n");
            std::abort();
        }
        Tick tick(pts, *clock_);
        tick.ordinal(next_);
        for (const BoundStream& stream : bound_) tick.bind(stream);
        return tick;
    }

    /// One `process` call.
    Result<Emitted> process(const Tick& tick) {
        next_ = tick.ordinal_number() + 1;
        return runner_.process(tick);
    }

    Status set_params(std::string_view params) { return runner_.set_params(params); }

    N& node() { return runner_.node(); }
    const N& node() const { return runner_.node(); }

    /// The instance's shape, as it was resolved at `init`.
    const NodeShape& shape() const { return runner_.resolved(); }

private:
    Harness(Runner<N> runner, std::vector<BoundStream> bound, std::optional<Rational> clock)
        : runner_(std::move(runner)), bound_(std::move(bound)), clock_(clock) {}

    Runner<N> runner_;
    std::vector<BoundStream> bound_;
    std::optional<Rational> clock_;
    std::uint64_t next_ = 0;
};

}  // namespace ffrwd::mock
