#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "ffrwd/node.hpp"

constexpr std::array<std::array<std::uint8_t, 4>, 7> COLOURS{{
    {192, 192, 192, 255},
    {192, 192, 0, 255},
    {0, 192, 192, 255},
    {0, 192, 0, 255},
    {192, 0, 192, 255},
    {192, 0, 0, 255},
    {0, 0, 192, 255},
}};

struct Params {
    std::uint32_t width;
    std::uint32_t height;
    double fps;
    std::optional<double> seconds;
    FFRWD_FIELDS(width, height, fps, seconds)
};

struct Bars : ffrwd::Node<Bars, Params> {
    static constexpr std::string_view name = "bars";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"width":{"type":"integer","minimum":16,"maximum":8192,"default":1280},"height":{"type":"integer","minimum":16,"maximum":8192,"default":720},"fps":{"type":"number","exclusiveMinimum":0,"maximum":240,"default":30},"seconds":{"type":["number","null"],"exclusiveMinimum":0}},"additionalProperties":false})";

    std::size_t width = 0;
    std::size_t height = 0;
    std::optional<double> seconds;

    /// Seven bars, and a white line crossing them once a second.
    ffrwd::Bytes draw(double t) const {
        auto line = std::size_t((t - std::trunc(t)) * double(width));
        ffrwd::Bytes canvas(width * height * 4);
        for (std::size_t y = 0; y < height; ++y)
            for (std::size_t x = 0; x < width; ++x) {
                std::array<std::uint8_t, 4> colour{255, 255, 255, 255};
                if (x != line) colour = COLOURS[x * COLOURS.size() / width];
                std::copy(colour.begin(), colour.end(), canvas.data() + (y * width + x) * 4);
            }
        return canvas;
    }

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto [width, height] = std::pair(params.width, params.height);
        return ffrwd::Shape()
            .rate(ffrwd::Rational::approximate(params.fps, 1001))
            .output(ffrwd::Output::video("video").size(width, height).pixel_format("rgba").row(0))
            .relation_row(R"({"width":)" + std::to_string(width) + R"(,"height":)" + std::to_string(height) +
                          "}")
            .bounded(params.seconds.has_value())
            .pure();
    }

    static ffrwd::Result<Bars> init(Params params, const ffrwd::Init&) {
        Bars node;
        node.width = params.width;
        node.height = params.height;
        node.seconds = params.seconds;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        if (seconds && tick.seconds() >= *seconds) {
            out.finish();
            return {};
        }
        return out.frame("video", tick.pts(), 1, draw(tick.seconds()));
    }
};

FFRWD_EXPORT(Bars);
