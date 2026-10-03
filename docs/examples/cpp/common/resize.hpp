#pragma once

// The crop and resize that the Rust examples take from ffrwd-frame, written
// out for C++: Pillow's bilinear, the kernel widened by the scale when it
// shrinks, across first and then down, each pass rounding back into eight
// bits as Pillow does. Its pixels are those of ffrwd-frame's `planes` with a
// normalization that scales nothing, interleaved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace resize {

/// A part of a picture in pixels: exclusive on the right and bottom.
struct Rect {
    std::size_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;

    static Rect whole(std::size_t width, std::size_t height) { return {0, 0, width, height}; }
    std::size_t width() const { return x1 > x0 ? x1 - x0 : 0; }
    std::size_t height() const { return y1 > y0 ? y1 - y0 : 0; }
};

/// What is wrong with `size` bytes as an rgba picture of `width` x `height`,
/// in ffrwd-frame's words; none when they are exactly that.
inline std::optional<std::string> misfit(std::size_t size, std::size_t width, std::size_t height) {
    std::size_t want = width * height * 4;
    if (size == want) return std::nullopt;
    return "an rgba frame of " + std::to_string(width) + "x" + std::to_string(height) + " is " +
           std::to_string(want) + " bytes, not " + std::to_string(size);
}

namespace detail {

/// One output pixel's taps: the first input pixel it reads, and a weight
/// for each, in fixed point.
struct Taps {
    std::size_t start = 0;
    std::vector<std::int32_t> weights;
};

struct Kernel {
    int precision = 0;
    std::vector<Taps> taps;
};

inline Kernel kernel(std::size_t in_size, std::size_t out_size) {
    double scale = double(in_size) / double(out_size);
    double filter_scale = std::max(scale, 1.0);
    double radius = filter_scale;
    double recip = 1.0 / filter_scale;
    std::vector<std::pair<std::size_t, std::vector<double>>> rows;
    double max_weight = 0.0;
    for (std::size_t out = 0; out < out_size; ++out) {
        double in_center = (double(out) + 0.5) * scale;
        auto x_min = std::size_t(std::max(std::floor(in_center - radius), 0.0));
        auto x_max = std::size_t(std::min(std::ceil(in_center + radius), double(in_size)));
        double center = in_center - 0.5;
        std::vector<double> weights;
        double total = 0.0;
        for (std::size_t x = x_min; x < x_max; ++x) {
            double distance = std::abs((double(x) - center) * recip);
            double w = distance < 1.0 ? 1.0 - distance : 0.0;
            weights.push_back(w);
            total += w;
        }
        if (total != 0.0)
            for (double& w : weights) w /= total;
        for (double w : weights) max_weight = std::max(max_weight, w);
        rows.emplace_back(x_min, std::move(weights));
    }
    Kernel kernel;
    for (int precision = 0; precision < 22; ++precision) {
        kernel.precision = precision;
        if (std::int32_t(std::round(max_weight * double(1 << (precision + 1)))) >= (1 << 15)) break;
    }
    double fixed = double(1 << kernel.precision);
    for (auto& [start, weights] : rows) {
        Taps taps{start, {}};
        for (double w : weights) taps.weights.push_back(std::int32_t(std::round(w * fixed)));
        kernel.taps.push_back(std::move(taps));
    }
    return kernel;
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
        for (std::size_t x = 0; x < to; ++x) {
            const Taps& taps = k.taps[x];
            for (std::size_t c = 0; c < 3; ++c) {
                std::int32_t sum = 1 << (k.precision - 1);
                for (std::size_t n = 0; n < taps.weights.size(); ++n)
                    sum += rgb[(y * width + taps.start + n) * 3 + c] * taps.weights[n];
                out[(y * to + x) * 3 + c] = clip(sum, k.precision);
            }
        }
    return out;
}

/// Interleaved rgb of `width` x `height` resized down to `to` high.
inline std::vector<std::uint8_t> down(const std::vector<std::uint8_t>& rgb, std::size_t width,
                                      std::size_t height, std::size_t to) {
    Kernel k = kernel(height, to);
    std::vector<std::uint8_t> out(width * to * 3);
    for (std::size_t y = 0; y < to; ++y) {
        const Taps& taps = k.taps[y];
        for (std::size_t at = 0; at < width * 3; ++at) {
            std::int32_t sum = 1 << (k.precision - 1);
            for (std::size_t n = 0; n < taps.weights.size(); ++n)
                sum += rgb[(taps.start + n) * width * 3 + at] * taps.weights[n];
            out[y * width * 3 + at] = clip(sum, k.precision);
        }
    }
    return out;
}

}  // namespace detail

/// `rect` of an rgba picture of `frame_width` x `frame_height` resized to
/// `width` x `height`, stretched to fill it: interleaved eight-bit red,
/// green and blue. Black when the crop or the target has no pixels.
inline std::vector<std::uint8_t> bilinear(const std::uint8_t* rgba, std::size_t frame_width,
                                          std::size_t frame_height, Rect rect, std::size_t width,
                                          std::size_t height) {
    rect.x1 = std::min(rect.x1, frame_width);
    rect.y1 = std::min(rect.y1, frame_height);
    rect.x0 = std::min(rect.x0, rect.x1);
    rect.y0 = std::min(rect.y0, rect.y1);
    std::size_t cw = rect.width(), ch = rect.height();
    if (cw == 0 || ch == 0 || width == 0 || height == 0) return std::vector<std::uint8_t>(width * height * 3);
    std::vector<std::uint8_t> rgb(cw * ch * 3);
    for (std::size_t y = 0; y < ch; ++y)
        for (std::size_t x = 0; x < cw; ++x)
            for (std::size_t c = 0; c < 3; ++c)
                rgb[(y * cw + x) * 3 + c] = rgba[((rect.y0 + y) * frame_width + rect.x0 + x) * 4 + c];
    if (cw != width) rgb = detail::across(rgb, cw, ch, width);
    if (ch != height) rgb = detail::down(rgb, width, ch, height);
    return rgb;
}

}  // namespace resize
