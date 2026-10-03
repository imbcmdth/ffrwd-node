#include <cmath>
#include <cstdint>
#include <vector>

#include "check.hpp"
#include "ffrwd/frame.hpp"

using namespace ffrwd::frame;

namespace {

constexpr Norm EIGHT_BITS{{0.0f, 0.0f, 0.0f}, {1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f}};

/// ffrwd-frame's own test picture: three sawtooth ramps with a flat block
/// over the lower right, alpha varying.
std::vector<std::uint8_t> edge(std::size_t width, std::size_t height) {
    std::vector<std::uint8_t> pixels;
    for (std::size_t y = 0; y < height; ++y)
        for (std::size_t x = 0; x < width; ++x) {
            std::uint8_t r = (x * 7 + y * 11) % 256, g = (x * 5 + y * 3) % 256, b = (x * 2 + y * 13) % 256;
            if (3 * x >= width && 3 * y >= height) r = 250, g = 8, b = 130;
            pixels.insert(pixels.end(), {r, g, b, std::uint8_t(255 - (x + y) % 256)});
        }
    return pixels;
}

/// And its other: splitmix64 from 0x5EED, one draw a pixel, its low four
/// bytes.
std::vector<std::uint8_t> noise(std::size_t width, std::size_t height) {
    std::vector<std::uint8_t> pixels;
    std::uint64_t state = 0x5EED;
    for (std::size_t n = 0; n < width * height; ++n) {
        state += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = state;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        for (int shift = 0; shift < 32; shift += 8) pixels.push_back(std::uint8_t(z >> shift));
    }
    return pixels;
}

std::uint64_t digest(const std::vector<std::uint8_t>& data) {
    std::uint64_t value = 0xCBF29CE484222325ull;
    for (std::uint8_t byte : data) value = (value ^ byte) * 0x100000001B3ull;
    return value;
}

/// Planes of plain eight-bit values back to interleaved bytes.
std::vector<std::uint8_t> eight(const std::vector<float>& planes, std::size_t width, std::size_t height) {
    std::size_t plane = width * height;
    std::vector<std::uint8_t> out;
    for (std::size_t at = 0; at < plane; ++at)
        for (std::size_t c = 0; c < 3; ++c)
            out.push_back(std::uint8_t(std::clamp(std::round(planes[c * plane + at]), 0.0f, 255.0f)));
    return out;
}

}  // namespace

TEST(frame_is_four_bytes_a_pixel_and_nothing_else) {
    std::vector<std::uint8_t> data(8 * 5 * 4 + 4);
    auto frame = CHECK_OK(Rgba::make(std::span(data).first(8 * 5 * 4), 8, 5));
    CHECK_EQ(frame.width, 8u);
    CHECK_EQ(frame.height, 5u);
    CHECK_ERR(Rgba::make(std::span(data).first(8 * 5 * 4 - 1), 8, 5));
    CHECK_EQ(CHECK_ERR(Rgba::make(data, 8, 5)), "an rgba frame of 8x5 is 160 bytes, not 164");
    CHECK_ERR(Rgba::make(std::span(data).first(8 * 5 * 3), 8, 5));
}

TEST(frame_box_grows_a_tenth_of_itself_floored_and_clamped) {
    auto rect = Rect::padded(300, 100, 100, 200, 0.1, 1280, 720);
    CHECK((rect == Rect{290, 80, 410, 320}));
    CHECK_EQ(rect->width(), 120u);
    CHECK_EQ(rect->height(), 240u);
    CHECK((Rect::padded(14, 14, 33, 33, 0.1, 640, 480) == Rect{10, 10, 50, 50}));
    CHECK((Rect::padded(5, 4, 100, 200, 0.1, 1280, 720) == Rect{0, 0, 115, 224}));
    CHECK((Rect::padded(1200, 600, 100, 200, 0.1, 1280, 720) == Rect{1190, 580, 1280, 720}));
    CHECK(Rect::padded(-50, -50, 400, 400, 0.1, 320, 240) == Rect::whole(320, 240));
}

TEST(frame_box_with_nothing_on_the_frame_is_no_rect) {
    CHECK(!Rect::padded(2000, 100, 50, 50, 0.1, 1280, 720));
    CHECK(!Rect::padded(-500, 100, 50, 50, 0.1, 1280, 720));
    CHECK(!Rect::padded(100, 2000, 50, 50, 0.1, 1280, 720));
    CHECK(!Rect::padded(100, 100, 0, 50, 0.1, 1280, 720));
    CHECK(!Rect::padded(100, 100, 50, 0, 0.1, 1280, 720));
}

TEST(frame_resize_is_ffrwd_frames_to_the_byte) {
    // What ffrwd-frame 0.1.1's `planes` answers for each, as eight-bit bytes.
    struct Case {
        bool edged;
        std::size_t sw, sh;
        std::optional<Rect> rect;
        std::size_t width, height;
        std::uint64_t want;
    };
    std::vector<Case> cases{
        {true, 9, 7, {}, 32, 24, 0x2b81603c6e9b7af2ull},
        {false, 9, 7, {}, 32, 24, 0x89c99096ab2298acull},
        {true, 500, 380, {}, 224, 224, 0x3f0ef4e1e3120a7dull},
        {false, 500, 380, {}, 224, 224, 0xb6ef2710b0b018d8ull},
        {true, 9, 7, {}, 9, 7, 0x67bff18552e376deull},
        {false, 320, 240, Rect{80, 60, 187, 140}, 320, 240, 0x817c929422ae445bull},
        {false, 320, 240, {}, 160, 240, 0xe1ec2af94805ba44ull},
        {true, 320, 240, {}, 320, 80, 0xee245d22a607658full},
        {true, 64, 48, {}, 200, 20, 0x10d6a057a703d7dbull},
        {false, 40, 30, Rect{30, 20, 99, 99}, 16, 16, 0xc4e1a45b3de0dbc7ull},
    };
    for (const Case& each : cases) {
        auto pixels = each.edged ? edge(each.sw, each.sh) : noise(each.sw, each.sh);
        auto frame = CHECK_OK(Rgba::make(pixels, each.sw, each.sh));
        auto rect = each.rect.value_or(Rect::whole(each.sw, each.sh));
        auto got = planes(frame, rect, each.width, each.height, Filter::Bilinear, EIGHT_BITS);
        CHECK_EQ(digest(eight(got, each.width, each.height)), each.want);
    }
}

TEST(frame_tensor_is_ffrwd_frames_to_the_bit) {
    auto pixels = noise(97, 61);
    auto frame = CHECK_OK(Rgba::make(pixels, 97, 61));
    auto padded = *Rect::padded(20, 10, 30, 25, 0.1, 97, 61);
    auto one = tensor(frame, padded, 24, 24, Filter::Bilinear, IMAGENET);
    CHECK_EQ(one.size(), 3u * 24 * 24 * 4);
    CHECK_EQ(digest(one), 0x5411c2aba83d4522ull);
    std::vector<Rect> both{Rect::whole(97, 61), padded};
    CHECK_EQ(digest(tensors(frame, both, 16, 12, Filter::Bilinear, IMAGENET)), 0xf5a3beedbb307eedull);
    CHECK(tensors(frame, {}, 16, 12, Filter::Bilinear, IMAGENET).empty());
}

TEST(frame_crop_with_no_pixels_is_black) {
    auto pixels = edge(9, 7);
    auto frame = CHECK_OK(Rgba::make(pixels, 9, 7));
    for (float value : planes(frame, {5, 5, 5, 7}, 4, 4, Filter::Bilinear, EIGHT_BITS)) CHECK_EQ(value, 0.0f);
}

TEST(frame_alpha_byte_reaches_nothing) {
    auto opaque = edge(24, 24), varying = edge(24, 24);
    for (std::size_t at = 3; at < varying.size(); at += 4) varying[at] = std::uint8_t(at);
    auto a = CHECK_OK(Rgba::make(opaque, 24, 24)), b = CHECK_OK(Rgba::make(varying, 24, 24));
    auto whole = Rect::whole(24, 24);
    CHECK(tensor(a, whole, 10, 10, Filter::Bilinear, IMAGENET) == tensor(b, whole, 10, 10, Filter::Bilinear, IMAGENET));
}
