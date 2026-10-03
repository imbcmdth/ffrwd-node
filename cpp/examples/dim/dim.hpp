#pragma once

// Dims the picture inside every box it is handed by `amount`: 0 leaves it,
// 1 makes it black. It reads four fields, `{x, y, w, h}`, and any row
// carrying them will do.

#include <cstdint>
#include <string_view>
#include <vector>

#include "../common/stand_ins.hpp"
#include "ffrwd/node.hpp"

namespace dim {

struct Params {
    double amount = 0.5;
    FFRWD_FIELDS(amount)
};

struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    FFRWD_FIELDS(x, y, w, h)
};

/// One flag a pixel: inside any of `boxes`.
inline std::vector<bool> covered(const std::vector<Box>& boxes, std::size_t width, std::size_t height) {
    std::vector<bool> inside(width * height);
    for (const Box& found : boxes) {
        auto rect = stand_ins::Rect::of(found.x, found.y, found.w, found.h, width, height);
        if (!rect) continue;
        for (std::size_t y = rect->y0; y < rect->y1; ++y)
            for (std::size_t x = rect->x0; x < rect->x1; ++x) inside[y * width + x] = true;
    }
    return inside;
}

struct Dim : ffrwd::Node<Dim, Params> {
    static constexpr std::string_view name = "dim";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"amount":{"type":"number","minimum":0,"maximum":1,"default":0.5}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::uint32_t boxes = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    double amount = 0.0;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .input(ffrwd::Input::rows("boxes").schema<Box>())
            .output(ffrwd::Output::like("v"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<Dim> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        FFRWD_LET(boxes, init.stream("boxes"));
        Dim dim;
        dim.v = v.id;
        dim.boxes = boxes.id;
        dim.width = video->width;
        dim.height = video->height;
        dim.amount = params.amount;
        return dim;
    }

    ffrwd::Status set_params(Params params) {
        amount = params.amount;
        return {};
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        FFRWD_LET(found, tick.rows<Box>(boxes));
        if (found.empty() || amount == 0.0) return out.pass("v", v, *frame);
        FFRWD_LET(picture, stand_ins::Picture::of(tick.fetch(v, frame->index), width, height));
        picture.darken(covered(found, width, height), amount);
        return out.frame("v", frame->pts, frame->duration, std::move(picture.data));
    }
};

}  // namespace dim
