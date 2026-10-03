#pragma once

// The crop and Pillow's bilinear resize that the Rust crate ffrwd-frame
// gives a Rust module, with its names: a frame, a rect of it, and the planes
// or the tensor a vision model reads. The resize is ffrwd-frame's to the
// byte: the same fixed point, across first and then down, each pass rounding
// back into eight bits, a pass skipped when its axis keeps its size.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "ffrwd/result.hpp"

namespace ffrwd::frame {

/// A frame as the wire carries it: 4 bytes a pixel, row-major, no padding.
/// The alpha byte is read by nothing here.
struct Rgba {
    std::span<const std::uint8_t> data;
    std::size_t width = 0;
    std::size_t height = 0;

    /// A frame over `data`, or an error when it is not exactly
    /// `width * height * 4` bytes.
    static Result<Rgba> make(std::span<const std::uint8_t> data, std::size_t width, std::size_t height) {
        std::size_t want = width * height * 4;
        if (data.size() != want)
            return fail("an rgba frame of " + std::to_string(width) + "x" + std::to_string(height) + " is " +
                        std::to_string(want) + " bytes, not " + std::to_string(data.size()));
        return Rgba{data, width, height};
    }
};

/// A half-open rectangle in frame pixels: exclusive on the right and bottom.
struct Rect {
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    /// The whole frame.
    static Rect whole(std::size_t width, std::size_t height) { return {0, 0, width, height}; }

    /// The crop a detector's box names: `x`, `y`, `w` and `h` in frame
    /// pixels, widened by `pad` of the box's own width and height on every
    /// side, each edge floored to a whole pixel and clamped to the frame.
    /// None when nothing of it lands on the frame.
    static std::optional<Rect> padded(double x, double y, double w, double h, double pad, std::size_t width,
                                      std::size_t height) {
        double pw = w * pad, ph = h * pad;
        auto floor_clamp = [](double value, std::size_t limit) {
            return std::size_t(std::clamp(std::floor(value), 0.0, double(limit)));
        };
        Rect rect{floor_clamp(x - pw, width), floor_clamp(y - ph, height), floor_clamp(x + w + pw, width),
                  floor_clamp(y + h + ph, height)};
        if (rect.x1 > rect.x0 && rect.y1 > rect.y0) return rect;
        return std::nullopt;
    }

    /// How many pixels across.
    std::size_t width() const { return x1 > x0 ? x1 - x0 : 0; }
    /// How many pixels down.
    std::size_t height() const { return y1 > y0 ? y1 - y0 : 0; }

    friend bool operator==(const Rect&, const Rect&) = default;
};

/// Pillow's resampling filters. `Bilinear` is Pillow's BILINEAR,
/// antialiased when downscaling.
enum class Filter { Bilinear };

/// Per-channel mean and std applied after scaling to 0..1.
struct Norm {
    std::array<float, 3> mean;
    std::array<float, 3> std;
};

/// What the usual export normalizes with: ImageNet's mean and standard
/// deviation, over red, green and blue in 0..1.
inline constexpr Norm IMAGENET{{0.485f, 0.456f, 0.406f}, {0.229f, 0.224f, 0.225f}};

namespace detail {

/// For each of `size` pixels made from `from`: the first pixel it reads,
/// and its weights in fixed point of `precision` bits.
struct Kernel {
    int precision = 0;
    std::vector<std::size_t> starts;
    std::vector<std::vector<std::int32_t>> weights;
};

inline Kernel kernel(std::size_t from, std::size_t size) {
    double scale = double(from) / double(size);
    double stretch = std::max(scale, 1.0);
    double recip = 1.0 / stretch;
    std::vector<std::vector<double>> rows;
    Kernel k;
    double heaviest = 0.0;
    for (std::size_t out = 0; out < size; ++out) {
        double centre = (double(out) + 0.5) * scale;
        auto first = std::size_t(std::max(std::floor(centre - stretch), 0.0));
        auto end = std::size_t(std::min(std::ceil(centre + stretch), double(from)));
        std::vector<double> weights;
        double total = 0.0;
        for (std::size_t x = first; x < end; ++x) {
            double distance = std::abs((double(x) - (centre - 0.5)) * recip);
            double weight = distance < 1.0 ? 1.0 - distance : 0.0;
            if (weight == 0.0 && weights.empty()) {
                ++first;
                continue;
            }
            weights.push_back(weight);
            total += weight;
        }
        while (!weights.empty() && weights.back() == 0.0) weights.pop_back();
        if (total != 0.0)
            for (double& weight : weights) weight /= total;
        for (double weight : weights) heaviest = std::max(heaviest, weight);
        k.starts.push_back(first);
        rows.push_back(std::move(weights));
    }
    for (int bits = 0; bits < 22; ++bits) {
        k.precision = bits;
        if (std::int32_t(std::round(heaviest * double(1 << (bits + 1)))) >= (1 << 15)) break;
    }
    double fixed = double(1 << k.precision);
    for (const auto& weights : rows) {
        std::vector<std::int32_t> taps;
        for (double weight : weights) taps.push_back(std::int32_t(std::round(weight * fixed)));
        k.weights.push_back(std::move(taps));
    }
    return k;
}

inline std::uint8_t clip(std::int32_t sum, int precision) {
    return std::uint8_t(std::clamp(sum >> precision, 0, 255));
}

/// Interleaved rgb of `width` x `height` resized across to `to` wide.
inline std::vector<std::uint8_t> across(const std::vector<std::uint8_t>& rgb, std::size_t width,
                                        std::size_t height, std::size_t to) {
    Kernel k = kernel(width, to);
    std::vector<std::uint8_t> out(to * height * 3);
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < to; ++x)
            for (std::size_t c = 0; c < 3; ++c) {
                std::int32_t sum = 1 << (k.precision - 1);
                for (std::size_t n = 0; n < k.weights[x].size(); ++n)
                    sum += rgb[(y * width + k.starts[x] + n) * 3 + c] * k.weights[x][n];
                out[(y * to + x) * 3 + c] = clip(sum, k.precision);
            }
    return out;
}

/// Interleaved rgb of `width` x `height` resized down to `to` high.
inline std::vector<std::uint8_t> down(const std::vector<std::uint8_t>& rgb, std::size_t width,
                                      std::size_t height, std::size_t to) {
    Kernel k = kernel(height, to);
    std::vector<std::uint8_t> out(width * to * 3);
    for (std::size_t y = 0; y < to; ++y)
        for (std::size_t at = 0; at < width * 3; ++at) {
            std::int32_t sum = 1 << (k.precision - 1);
            for (std::size_t n = 0; n < k.weights[y].size(); ++n)
                sum += rgb[(k.starts[y] + n) * width * 3 + at] * k.weights[y][n];
            out[y * width * 3 + at] = clip(sum, k.precision);
        }
    return out;
}

/// `rect` of the frame, brought inside it, resized to `width` x `height`:
/// eight-bit interleaved red, green and blue. Black when the crop or the
/// target has no pixels.
inline std::vector<std::uint8_t> resized_rgb(const Rgba& frame, Rect rect, std::size_t width,
                                             std::size_t height) {
    rect.x1 = std::min(rect.x1, frame.width);
    rect.y1 = std::min(rect.y1, frame.height);
    rect.x0 = std::min(rect.x0, rect.x1);
    rect.y0 = std::min(rect.y0, rect.y1);
    std::size_t cw = rect.width(), ch = rect.height();
    if (cw == 0 || ch == 0 || width == 0 || height == 0) return std::vector<std::uint8_t>(width * height * 3);
    std::vector<std::uint8_t> rgb(cw * ch * 3);
    for (std::size_t y = 0; y < ch; ++y)
        for (std::size_t x = 0; x < cw; ++x)
            for (std::size_t c = 0; c < 3; ++c)
                rgb[(y * cw + x) * 3 + c] = frame.data[((rect.y0 + y) * frame.width + rect.x0 + x) * 4 + c];
    if (cw != width) rgb = across(rgb, cw, ch, width);
    if (ch != height) rgb = down(rgb, width, ch, height);
    return rgb;
}

}  // namespace detail

/// `rect` of `frame` resized to `width` x `height` (stretched, no aspect
/// padding), then scaled to 0..1 and normalized: planar RGB,
/// `[3, height, width]`, the red plane first.
inline std::vector<float> planes(const Rgba& frame, Rect rect, std::size_t width, std::size_t height,
                                 Filter, const Norm& norm) {
    auto rgb = detail::resized_rgb(frame, rect, width, height);
    std::size_t plane = width * height;
    std::vector<float> out(plane * 3);
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t at = 0; at < plane; ++at)
            out[c * plane + at] = (float(rgb[at * 3 + c]) / 255.0f - norm.mean[c]) / norm.std[c];
    return out;
}

/// Several crops of one frame as one `[n, 3, height, width]` tensor's
/// bytes, little-endian fp32, in the order the rects were given.
inline std::vector<std::uint8_t> tensors(const Rgba& frame, std::span<const Rect> rects, std::size_t width,
                                         std::size_t height, Filter filter, const Norm& norm) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(rects.size() * width * height * 3 * 4);
    for (const Rect& rect : rects)
        for (float sample : planes(frame, rect, width, height, filter, norm)) {
            auto bits = std::bit_cast<std::uint32_t>(sample);
            for (int shift = 0; shift < 32; shift += 8) bytes.push_back(std::uint8_t(bits >> shift));
        }
    return bytes;
}

/// The same as the little-endian fp32 bytes of a `[1, 3, height, width]`
/// tensor.
inline std::vector<std::uint8_t> tensor(const Rgba& frame, Rect rect, std::size_t width, std::size_t height,
                                        Filter filter, const Norm& norm) {
    return tensors(frame, std::span<const Rect>(&rect, 1), width, height, filter, norm);
}

}  // namespace ffrwd::frame
