#include <algorithm>
#include <cmath>

#include "ffrwd/node.hpp"

/// The fields `boxmask` reads. Any row carrying them will do.
struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
    FFRWD_FIELDS(x, y, w, h)
};

/// `value` as a whole pixel from 0 to `limit`.
std::size_t edge(double value, std::size_t limit) {
    return std::size_t(std::fmin(std::fmax(value, 0.0), double(limit)));
}

struct BoxMask : ffrwd::Node<BoxMask> {
    static constexpr std::string_view name = "boxmask";
    static constexpr std::string_view version = "0.1.0";

    std::uint32_t v = 0;
    std::uint32_t boxes = 0;
    std::size_t width = 0;
    std::size_t height = 0;

    static ffrwd::Result<ffrwd::Shape> shape(const ffrwd::NoParams&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().timing())
            .input(ffrwd::Input::rows("boxes").schema<Box>())
            .output(ffrwd::Output::like("v").pixel_format("gray"))
            .pure()
            .one_to_one();
    }

    static ffrwd::Result<BoxMask> init(ffrwd::NoParams, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        FFRWD_LET(boxes, init.stream("boxes"));
        BoxMask node;
        node.v = v.id;
        node.boxes = boxes.id;
        node.width = video->width;
        node.height = video->height;
        return node;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return {};
        ffrwd::Bytes mask(width * height);
        FFRWD_LET(found, tick.rows<Box>(boxes));
        for (const Box& box : found) {
            std::size_t x0 = edge(box.x, width), y0 = edge(box.y, height);
            std::size_t x1 = std::max(edge(box.x + box.w, width), x0), y1 = edge(box.y + box.h, height);
            for (std::size_t y = y0; y < y1; ++y)
                std::fill(mask.data() + y * width + x0, mask.data() + y * width + x1, 255);
        }
        return out.frame("v", frame->pts, frame->duration, std::move(mask));
    }
};

FFRWD_EXPORT(BoxMask);
