#pragma once

// What the example nodes share: the grey mark `spot` tracks and the one way
// `dim` draws on a picture, as the sidecar's stand-in modules have them, so
// these write the same rows and the same pixels.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ffrwd/node.hpp"

namespace stand_ins {

/// A half-open rectangle in frame pixels: exclusive on the right and bottom.
struct Rect {
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    std::size_t width() const { return x1 > x0 ? x1 - x0 : 0; }
    std::size_t height() const { return y1 > y0 ? y1 - y0 : 0; }

    /// The pixels a detector's box names, `x`, `y`, `w` and `h` in frame
    /// pixels, each edge floored and clamped to the frame; none when nothing
    /// of it lands there.
    static std::optional<Rect> of(double x, double y, double w, double h, std::size_t width,
                                  std::size_t height) {
        auto clamp = [](double value, std::size_t limit) {
            return std::size_t(std::clamp(std::floor(value), 0.0, double(limit)));
        };
        Rect rect{clamp(x, width), clamp(y, height), clamp(x + w, width), clamp(y + h, height)};
        if (rect.x1 > rect.x0 && rect.y1 > rect.y0) return rect;
        return std::nullopt;
    }

    friend bool operator==(const Rect&, const Rect&) = default;
};

/// An rgba picture to draw on: four bytes a pixel, row after row, no
/// padding.
struct Picture {
    ffrwd::Bytes data;
    std::size_t width = 0;
    std::size_t height = 0;

    /// The bytes a host handed over for a `width` x `height` rgba frame.
    static ffrwd::Result<Picture> of(ffrwd::Bytes data, std::size_t width, std::size_t height) {
        if (data.size() != width * height * 4)
            return ffrwd::fail("an rgba frame of " + std::to_string(width) + "x" + std::to_string(height) +
                               " is " + std::to_string(width * height * 4) + " bytes, not " +
                               std::to_string(data.size()));
        return Picture{std::move(data), width, height};
    }

    /// An opaque picture of one colour.
    static Picture filled(std::size_t width, std::size_t height, std::array<std::uint8_t, 4> colour) {
        Picture picture{ffrwd::Bytes(width * height * 4), width, height};
        for (std::size_t at = 0; at < width * height; ++at)
            for (std::size_t c = 0; c < 4; ++c) picture.data[at * 4 + c] = colour[c];
        return picture;
    }

    /// `rect` painted over, alpha `colour[3]` of 255.
    void fill(Rect rect, std::array<std::uint8_t, 4> colour) {
        std::size_t x1 = std::min(rect.x1, width), y1 = std::min(rect.y1, height);
        std::size_t x0 = std::min(rect.x0, x1), y0 = std::min(rect.y0, y1);
        std::uint32_t alpha = colour[3];
        for (std::size_t y = y0; y < y1; ++y)
            for (std::size_t x = x0; x < x1; ++x)
                for (std::size_t c = 0; c < 3; ++c) {
                    std::uint8_t& under = data[(y * width + x) * 4 + c];
                    under = std::uint8_t((colour[c] * alpha + under * (255 - alpha) + 127) / 255);
                }
    }

    /// Every pixel `covered` names, one flag a pixel, darkened by `amount`
    /// of its value: 0 leaves it, 1 makes it black.
    void darken(const std::vector<bool>& covered, double amount) {
        std::uint32_t keep = std::uint32_t(std::round((1.0 - std::clamp(amount, 0.0, 1.0)) * 256.0));
        std::size_t pixels = std::min(covered.size(), width * height);
        for (std::size_t at = 0; at < pixels; ++at) {
            if (!covered[at]) continue;
            for (std::size_t c = 0; c < 3; ++c) {
                std::uint8_t& channel = data[at * 4 + c];
                channel = std::uint8_t((channel * keep) >> 8);
            }
        }
    }
};

inline bool grey(const std::uint8_t* pixel) {
    std::uint8_t lo = std::min({pixel[0], pixel[1], pixel[2]});
    std::uint8_t hi = std::max({pixel[0], pixel[1], pixel[2]});
    return hi - lo < 24 && lo > 96 && hi < 160;
}

/// The mark the examples track: the box around the largest patch of mid
/// grey in the picture, which in ffmpeg's `testsrc2` is the grey shape that
/// grows and shrinks at the lower left. None when no patch is 32 pixels or
/// more.
inline std::optional<Rect> find(const Picture& frame) {
    const std::size_t smallest = 32;
    std::size_t width = frame.width, height = frame.height, area = width * height;
    std::vector<bool> open(area);
    for (std::size_t at = 0; at < area; ++at) open[at] = grey(frame.data.data() + at * 4);
    std::optional<std::pair<std::size_t, Rect>> best;
    std::vector<std::size_t> stack;
    for (std::size_t start = 0; start < area; ++start) {
        if (!open[start]) continue;
        open[start] = false;
        stack.push_back(start);
        std::size_t count = 0;
        Rect rect{start % width, start / width, start % width + 1, start / width + 1};
        while (!stack.empty()) {
            std::size_t at = stack.back();
            stack.pop_back();
            ++count;
            std::size_t x = at % width, y = at / width;
            rect.x0 = std::min(rect.x0, x);
            rect.y0 = std::min(rect.y0, y);
            rect.x1 = std::max(rect.x1, x + 1);
            rect.y1 = std::max(rect.y1, y + 1);
            auto visit = [&](std::size_t next) {
                if (open[next]) {
                    open[next] = false;
                    stack.push_back(next);
                }
            };
            if (x > 0) visit(at - 1);
            if (x + 1 < width) visit(at + 1);
            if (y > 0) visit(at - width);
            if (y + 1 < height) visit(at + width);
        }
        if (count >= smallest && (!best || count > best->first)) best = std::pair(count, rect);
    }
    if (!best) return std::nullopt;
    return best->second;
}

/// One row of the mark: where it is on this frame, and the sighting it
/// belongs to.
struct Spot {
    /// The sighting: when it began, in seconds, and its number from 0,
    /// written as `start_t` and `id`.
    ffrwd::Span span;
    std::uint32_t x = 0, y = 0, w = 0, h = 0;

    FFRWD_FIELDS(span, x, y, w, h)
};

/// The mark named by frame number: frames `0..every` of the run are
/// sighting 0, the next `every` sighting 1, and so on, each starting at its
/// first frame's time. A function of the frame and its number alone, so
/// every worker of a split run names a frame's sighting alike.
class Spotter {
public:
    /// Sightings of `every` frames of `v`, a frame being the length its rate
    /// says, or one tick of its time base where the call gave no rate.
    Spotter(std::uint64_t every, const ffrwd::BoundStream& v)
        : every_(std::max<std::uint64_t>(every, 1)), time_base_(v.info.time_base) {
        if (v.hint.rate) {
            std::int64_t num = std::int64_t(time_base_.den) * v.hint.rate->den;
            std::int64_t den = std::max<std::int64_t>(std::int64_t(time_base_.num) * v.hint.rate->num, 1);
            step_ = std::max<std::int64_t>((num + den / 2) / den, 1);
        }
    }

    /// Frame `ordinal` of the run, at `pts`: its row, when the mark is in
    /// view.
    std::optional<Spot> see(std::uint64_t ordinal, std::int64_t pts, const Picture& frame) const {
        auto rect = find(frame);
        if (!rect) return std::nullopt;
        std::int64_t into = std::int64_t(ordinal % every_);
        ffrwd::Span span{time_base_.seconds(pts - into * step_), ordinal / every_, std::uint64_t(into)};
        return Spot{span, std::uint32_t(rect->x0), std::uint32_t(rect->y0), std::uint32_t(rect->width()),
                    std::uint32_t(rect->height())};
    }

private:
    std::uint64_t every_;
    std::int64_t step_ = 1;
    ffrwd::Rational time_base_;
};

}  // namespace stand_ins
