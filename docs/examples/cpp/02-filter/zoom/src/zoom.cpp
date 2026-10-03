#include <algorithm>
#include <cmath>
#include <vector>

#include "ffrwd/node.hpp"
#include "resize.hpp"

struct Params {
    double amount;
    double x;
    double y;
    FFRWD_FIELDS(amount, x, y)
};

/// Red, green and blue back to opaque rgba.
ffrwd::Bytes interleave(const std::vector<std::uint8_t>& rgb, std::size_t pixels) {
    ffrwd::Bytes rgba(pixels * 4);
    for (std::size_t at = 0; at < pixels; ++at) {
        for (std::size_t channel = 0; channel < 3; ++channel) rgba[at * 4 + channel] = rgb[at * 3 + channel];
        rgba[at * 4 + 3] = 255;
    }
    return rgba;
}

struct Zoom : ffrwd::Node<Zoom, Params> {
    static constexpr std::string_view name = "zoom";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"amount":{"type":"number","minimum":1,"maximum":16,"default":2},"x":{"type":"number","minimum":0,"maximum":1,"default":0.5},"y":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    Params params;

    /// The part of the picture that fills the frame: `1 / amount` of each
    /// side, centred on `x`, `y` as far as the picture allows.
    resize::Rect crop() const {
        double w = std::max(std::round(double(width) / params.amount), 1.0);
        double h = std::max(std::round(double(height) / params.amount), 1.0);
        auto x0 = std::size_t(std::clamp(params.x * double(width) - w / 2.0, 0.0, double(width) - w));
        auto y0 = std::size_t(std::clamp(params.y * double(height) - h / 2.0, 0.0, double(height) - h));
        return {x0, y0, x0 + std::size_t(w), y0 + std::size_t(h)};
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Zoom> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Zoom node;
        node.v = v.id;
        node.width = video->width;
        node.height = video->height;
        node.params = params;
        return node;
    }

    ffrwd::Status set_params(Params next) {
        params = next;
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        if (params.amount == 1.0) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        if (auto wrong = resize::misfit(pixels.size(), width, height)) return ffrwd::fail(*wrong);
        auto rgb = resize::bilinear(pixels.data(), width, height, crop(), width, height);
        return out.frame("v", frame->pts, frame->duration, interleave(rgb, width * height));
    }
};

FFRWD_EXPORT(Zoom);
