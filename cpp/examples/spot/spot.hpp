#pragma once

// Finds a mark in each frame and writes rows alone: one a frame while the
// mark is in view, `{start_t, id, x, y, w, h}`, every row of one sighting
// carrying the time its block began as `start_t`, a new sighting every
// `every` frames of the run. Counted by the tick's ordinal, so it is pure
// and a run split across workers names every sighting alike.

#include <cstdint>
#include <optional>
#include <string_view>

#include "../common/stand_ins.hpp"
#include "ffrwd/node.hpp"

namespace spot {

struct Params {
    std::uint64_t every = 30;
    FFRWD_FIELDS(every)
};

struct Spot : ffrwd::Node<Spot, Params> {
    static constexpr std::string_view name = "spot";
    static constexpr std::string_view version = "0.1.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"every":{"type":"integer","minimum":1,"default":30}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::size_t width = 0;
    std::size_t height = 0;
    std::optional<stand_ins::Spotter> spotter;

    static ffrwd::Result<ffrwd::Shape> shape(const Params&, const ffrwd::Bound&) {
        return ffrwd::Shape()
            .input(ffrwd::Input::video("v").clock().pixel_formats({"rgba"}))
            .output(ffrwd::Output::rows("spots").schema<stand_ins::Spot>())
            .pure();
    }

    static ffrwd::Result<Spot> init(Params params, const ffrwd::Init& init) {
        FFRWD_LET(v, init.stream("v"));
        const ffrwd::VideoFormat* video = v.video_format();
        if (!video) return ffrwd::fail("`v` is a video input");
        Spot spot;
        spot.v = v.id;
        spot.width = video->width;
        spot.height = video->height;
        spot.spotter.emplace(params.every, v);
        return spot;
    }

    ffrwd::Status process(const ffrwd::Tick& tick, ffrwd::Out& out) {
        for (const ffrwd::Frame& frame : tick.frames(v)) {
            FFRWD_LET(picture, stand_ins::Picture::of(tick.fetch(v, frame.index), width, height));
            if (auto seen = spotter->see(tick.ordinal(), frame.pts, picture))
                FFRWD_TRY(out.row("spots", frame.pts, *seen));
        }
        return {};
    }
};

}  // namespace spot
