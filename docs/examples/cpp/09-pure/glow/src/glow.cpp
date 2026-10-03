#include <algorithm>
#include <array>
#include <optional>

#include "ffrwd/node.hpp"

struct Params {
    std::uint8_t threshold;
    std::uint64_t every;
    FFRWD_FIELDS(threshold, every)
};

struct Glow {
    double start_t = 0.0;
    std::uint64_t id = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint32_t w = 0;
    std::uint32_t h = 0;
    FFRWD_FIELDS(start_t, id, x, y, w, h)
};

/// The box around every pixel of an rgba picture at least `threshold`
/// bright.
std::optional<std::array<std::uint32_t, 4>> bright(const ffrwd::Bytes& pixels, std::size_t width,
                                                   std::uint8_t threshold) {
    std::optional<std::array<std::size_t, 4>> found;
    for (std::size_t at = 0; at * 4 + 3 < pixels.size(); ++at) {
        const std::uint8_t* pixel = pixels.data() + at * 4;
        std::uint32_t luma = (54 * pixel[0] + 183 * pixel[1] + 19 * pixel[2]) >> 8;
        if (luma >= threshold) {
            std::size_t x = at % width, y = at / width;
            if (!found) found = std::array{x, y, x, y};
            auto& [x0, y0, x1, y1] = *found;
            x0 = std::min(x0, x);
            y0 = std::min(y0, y);
            x1 = std::max(x1, x);
            y1 = std::max(y1, y);
        }
    }
    if (!found) return std::nullopt;
    auto [x0, y0, x1, y1] = *found;
    return std::array{std::uint32_t(x0), std::uint32_t(y0), std::uint32_t(x1 - x0 + 1),
                      std::uint32_t(y1 - y0 + 1)};
}

struct GlowNode : ffrwd::Node<GlowNode, Params> {
    static constexpr std::string_view name = "glow";
    static constexpr std::string_view version = "0.2.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"threshold":{"type":"integer","minimum":0,"maximum":255,"default":230},"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::uint8_t threshold = 0;
    std::uint64_t every = 1;
    /// One frame of the picture, in its time base.
    std::int64_t step = 1;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::rows("glows").schema<Glow>())
            .pure();
    }

    static ffrwd::Result<GlowNode> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        GlowNode node;
        node.v = v.id;
        node.width = video->width;
        node.threshold = params.threshold;
        node.every = params.every;
        if (v.hint.rate)
            node.step = std::max<std::int64_t>(v.info.time_base.pts(v.hint.rate->duration(1)), 1);
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        auto box = bright(pixels, width, threshold);
        if (!box) return {};
        auto [x, y, w, h] = *box;
        auto into = std::int64_t(tick.ordinal() % every);
        Glow glow{tick.time_base().seconds(frame->pts - into * step), tick.ordinal() / every, x, y, w, h};
        return out.row("glows", frame->pts, glow);
    }
};

FFRWD_EXPORT(GlowNode);
