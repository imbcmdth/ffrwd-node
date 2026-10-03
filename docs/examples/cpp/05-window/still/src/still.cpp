#include <algorithm>
#include <cstdlib>
#include <optional>
#include <utility>

#include "ffrwd/node.hpp"

struct Params {
    double shortest;
    double longest;
    double tolerance;
    FFRWD_FIELDS(shortest, longest, tolerance)
};

struct Still {
    double start_t = 0.0;
    double end_t = 0.0;
    FFRWD_FIELDS(start_t, end_t)
};

/// How far apart two pictures' luma planes are: the mean difference of a
/// pixel.
double difference(const std::uint8_t* a, const std::uint8_t* b, std::size_t size) {
    std::uint64_t total = 0;
    for (std::size_t at = 0; at < size; ++at) total += std::uint64_t(std::abs(int(a[at]) - int(b[at])));
    return double(total) / double(std::max<std::size_t>(size, 1));
}

struct StillNode : ffrwd::Node<StillNode, Params> {
    static constexpr std::string_view name = "still";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"shortest":{"type":"number","minimum":0,"default":1},"longest":{"type":"number","exclusiveMinimum":0,"maximum":600,"default":10},"tolerance":{"type":"number","minimum":0,"maximum":255,"default":2}},"additionalProperties":false})";

    std::uint32_t v = 0;
    /// The bytes of a picture's luma plane, which come first in yuv420p.
    std::size_t luma = 0;
    Params params;
    /// Where the stretch the picture is still in began: its pts and seconds.
    std::optional<std::pair<std::int64_t, double>> open;

    /// Writes the open stretch as ending at `end_t`, if it lasted long enough.
    ffrwd::Status close(double end_t, ffrwd::Out& out) {
        auto stretch = std::exchange(open, std::nullopt);
        if (stretch && end_t - stretch->second >= params.shortest)
            return out.row("stills", stretch->first, Still{stretch->second, end_t});
        return {};
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound& bound) {
        auto rate = bound.rate_of("v");
        double frame = rate ? rate->duration(1) : 1.0;
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().window(2, 1).pixel_formats({"yuv420p"}))
            .output(ffrwd::Output::rows("stills").latency(params.longest + frame).schema<Still>());
    }

    static ffrwd::Result<StillNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        StillNode node;
        node.v = v.id;
        node.luma = std::size_t(video->width) * video->height;
        node.params = params;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto seconds = [&](std::int64_t pts) { return tick.time_base().seconds(pts); };
        auto frames = tick.frames(v);
        if (frames.size() != 2) {
            auto last = tick.frame(v);
            return close(last ? seconds(last->pts) : tick.seconds(), out);
        }
        const ffrwd::Frame& before = frames[0];
        const ffrwd::Frame& after = frames[1];
        ffrwd::Bytes a = tick.fetch(v, before.index);
        ffrwd::Bytes b = tick.fetch(v, after.index);
        if (a.size() < luma || b.size() < luma) return ffrwd::fail("`v` is not a whole yuv420p picture");
        double moved = difference(a.data(), b.data(), luma);
        if (moved > params.tolerance) return close(seconds(after.pts), out);
        if (!open) open = std::pair(before.pts, seconds(before.pts));
        double start_t = open->second;
        if (seconds(after.pts) - start_t >= params.longest) {
            FFRWD_TRY(close(seconds(after.pts), out));
            open = std::pair(after.pts, seconds(after.pts));
        }
        return {};
    }
};

FFRWD_EXPORT(StillNode);
