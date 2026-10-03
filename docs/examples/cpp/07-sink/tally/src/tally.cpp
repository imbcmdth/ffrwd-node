#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ffrwd/node.hpp"

/// One row per stream, written on the last call.
struct Count {
    std::string port;
    std::string codec;
    std::uint64_t packets = 0;
    std::uint64_t keyframes = 0;
    std::uint64_t bytes = 0;
    double seconds = 0.0;
    FFRWD_FIELDS(port, codec, packets, keyframes, bytes, seconds)
};

struct Stream {
    std::uint32_t id = 0;
    ffrwd::Rational time_base;
    Count count;
    std::optional<std::int64_t> first;
    std::optional<std::int64_t> last;
};

struct Tally : ffrwd::Node<Tally> {
    static constexpr std::string_view name = "tally";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view rows_schema =
        R"({"type":"object","properties":{"port":{"type":"string"},"codec":{"type":"string"},"packets":{"type":"integer"},"keyframes":{"type":"integer"},"bytes":{"type":"integer"},"seconds":{"type":"number"}},"required":["port","codec","packets","keyframes","bytes","seconds"]})";

    std::vector<Stream> streams;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::packets("video").optional().many().arrival())
            .input(ffrwd::Input::packets("audio").optional().many().arrival())
            .rate(ffrwd::Rational(10, 1));
    }

    static ffrwd::Result<Tally> init(ffrwd::NoParams, const ffrwd::Init& init) {
        Tally node;
        for (const ffrwd::BoundStream& stream : init.all()) {
            const auto* coded = stream.format ? std::get_if<ffrwd::CodedStream>(&*stream.format) : nullptr;
            if (!coded) return ffrwd::fail("`" + stream.port + "` carries no packets");
            node.streams.push_back(
                Stream{stream.id, coded->time_base, Count{stream.port, coded->codec}, {}, {}});
        }
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        for (Stream& stream : streams) {
            for (const ffrwd::Packet& packet : tick.packets(stream.id)) {
                Count& count = stream.count;
                count.packets += 1;
                count.keyframes += packet.keyframe ? 1 : 0;
                count.bytes += packet.data.size();
                std::int64_t end = packet.pts + packet.duration.value_or(0);
                stream.first = std::min(stream.first.value_or(packet.pts), packet.pts);
                stream.last = std::max(stream.last.value_or(end), end);
            }
        }
        if (tick.last()) {
            for (Stream& stream : streams) {
                if (stream.first && stream.last)
                    stream.count.seconds = stream.time_base.seconds(*stream.last - *stream.first);
                out.report(stream.count);
            }
        }
        return {};
    }
};

FFRWD_EXPORT(Tally);
