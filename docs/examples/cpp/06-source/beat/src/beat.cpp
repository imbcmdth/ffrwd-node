#include <chrono>
#include <cmath>
#include <string>
#include <thread>

#include "ffrwd/node.hpp"

struct Params {
    double every;
    std::uint32_t width;
    std::uint32_t height;
    FFRWD_FIELDS(every, width, height)
};

struct Beat : ffrwd::Node<Beat, Params> {
    static constexpr std::string_view name = "beat";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"every":{"type":"number","minimum":0.01,"maximum":3600,"default":1},"width":{"type":"integer","minimum":16,"maximum":8192,"default":320},"height":{"type":"integer","minimum":16,"maximum":8192,"default":240}},"additionalProperties":false})";

    std::int64_t every = 0;
    std::int64_t next = 0;
    std::size_t pixels = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        auto [width, height] = std::pair(params.width, params.height);
        return ffrwd::Shape()
            .self_clocked()
            .output(ffrwd::Output::video("video").size(width, height).pixel_format("rgba").row(0))
            .relation_row(R"({"width":)" + std::to_string(width) + R"(,"height":)" + std::to_string(height) +
                          "}")
            .bounded(false);
    }

    static ffrwd::Result<Beat> init(Params params, const ffrwd::Init&) {
        Beat node;
        node.every = std::int64_t(std::round(params.every * 1e6));
        node.next = 0;
        node.pixels = std::size_t(params.width) * params.height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        std::int64_t now = tick.pts();
        if (now < next) std::this_thread::sleep_for(std::chrono::microseconds(next - now));
        auto wall = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());
        auto grey = std::uint8_t(wall.count() % 8 * 32);
        ffrwd::Bytes frame(pixels * 4);
        for (std::size_t at = 0; at < pixels; ++at) {
            frame[at * 4] = frame[at * 4 + 1] = frame[at * 4 + 2] = grey;
            frame[at * 4 + 3] = 255;
        }
        FFRWD_TRY(out.frame("video", next, every, std::move(frame)));
        next += every;
        return {};
    }
};

FFRWD_EXPORT(Beat);
