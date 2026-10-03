#include <algorithm>
#include <cmath>
#include <vector>

#include "ffrwd/node.hpp"

struct Params {
    double fade;
    FFRWD_FIELDS(fade)
};

/// How much of the band `cue` shows at `t`: rising over `fade` seconds
/// before it starts, whole while it runs, falling over `fade` after it ends.
double opacity(const ffrwd::Cue& cue, double t, double fade) {
    if (fade == 0.0) return cue.covers(t) ? 1.0 : 0.0;
    double rising = (t - (cue.start_t - fade)) / fade;
    double falling = (cue.end_t + fade - t) / fade;
    return std::clamp(std::min(rising, falling), 0.0, 1.0);
}

struct Band : ffrwd::Node<Band, Params> {
    static constexpr std::string_view name = "band";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"fade":{"type":"number","minimum":0,"maximum":5,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    double fade = 0.0;
    std::vector<ffrwd::Cue> cues;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(ffrwd::Input::rows("cues")
                       .interval()
                       .latency(5.0)
                       .ahead(params.fade)
                       .state()
                       .schema<ffrwd::Cue>())
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Band> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Band node;
        node.v = v.id;
        node.width = video->width;
        node.height = video->height;
        node.fade = params.fade;
        return node;
    }

    ffrwd::Status fold(const ffrwd::StateRow& row) {
        FFRWD_LET(cue, row.row<ffrwd::Cue>());
        cues.push_back(std::move(cue));
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        double t = tick.time_base().seconds(frame->pts);
        std::erase_if(cues, [&](const ffrwd::Cue& cue) { return !(cue.end_t + fade > t); });
        double shown = 0.0;
        for (const ffrwd::Cue& cue : cues) shown = std::max(shown, opacity(cue, t, fade));
        if (shown == 0.0) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        double keep = 1.0 - 0.6 * shown;
        std::size_t top = height * 4 / 5;
        for (std::size_t at = top * width * 4; at + 3 < pixels.size(); at += 4)
            for (std::size_t channel = at; channel < at + 3; ++channel)
                pixels[channel] = std::uint8_t(std::round(pixels[channel] * keep));
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Band);
