#include <algorithm>
#include <cmath>

#include "ffrwd/node.hpp"

struct Params {
    std::uint8_t black;
    std::uint8_t white;
    FFRWD_FIELDS(black, white)
};

/// `value` with `black` moved to 0 and `white` to 255.
std::uint8_t stretch(std::uint8_t value, Params params) {
    double black = params.black, white = params.white;
    return std::uint8_t(std::clamp(std::round((value - black) * 255.0 / (white - black)), 0.0, 255.0));
}

struct Levels : ffrwd::Node<Levels, Params> {
    static constexpr std::string_view name = "levels";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"black":{"type":"integer","minimum":0,"maximum":254,"default":16},"white":{"type":"integer","minimum":1,"maximum":255,"default":235}},"additionalProperties":false})";

    std::uint32_t v = 0;
    Params params;

    static ffrwd::Result<ffrwd::Shape> shape(const Params& params, const ffrwd::Bound&) {
        if (params.black >= params.white) return ffrwd::fail("levels needs `black` under `white`");
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Levels> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        Levels node;
        node.v = v.id;
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
        if (params.black == 0 && params.white == 255) return out.pass("v", v, *frame);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        for (std::size_t at = 0; at + 3 < pixels.size(); at += 4)
            for (std::size_t channel = at; channel < at + 3; ++channel)
                pixels[channel] = stretch(pixels[channel], params);
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Levels);
