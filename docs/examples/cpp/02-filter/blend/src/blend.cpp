#include <cmath>

#include "ffrwd/node.hpp"

struct Params {
    double mix;
    FFRWD_FIELDS(mix)
};

struct Blend : ffrwd::Node<Blend, Params> {
    static constexpr std::string_view name = "blend";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"mix":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::uint32_t over = 0;
    double mix = 0.0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(ffrwd::Input::video("over").lockstep().like("v").pixel_formats({"rgba"}))
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Blend> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        FFRWD_LET(over, init.stream("over"));
        Blend node;
        node.v = v.id;
        node.over = over.id;
        node.mix = params.mix;
        return node;
    }

    ffrwd::Status set_params(Params params) {
        mix = params.mix;
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        auto top = tick.frame(over);
        if (!top) return out.pass("v", v, *frame);
        if (mix == 0.0) return out.pass("v", v, *frame);
        if (mix == 1.0) return out.same("v", frame->pts, frame->duration, over, top->index);
        ffrwd::Bytes pixels = tick.fetch(v, frame->index);
        ffrwd::Bytes above = tick.fetch(over, top->index);
        for (std::size_t at = 0; at < pixels.size() && at < above.size(); ++at) {
            double mixed = double(pixels[at]) + (double(above[at]) - double(pixels[at])) * mix;
            pixels[at] = std::uint8_t(std::round(mixed));
        }
        return out.frame("v", frame->pts, frame->duration, std::move(pixels));
    }
};

FFRWD_EXPORT(Blend);
