#include "levels.cpp"

#include <variant>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

ffrwd::Result<ffrwd::mock::Harness<Levels>> open(std::string_view params) {
    auto v = ffrwd::BoundStream::video("v", 0, 2, 1, "rgba", ffrwd::Rational(1, 25));
    return ffrwd::mock::Harness<Levels>::open(params, {v});
}

TEST(black_and_white_reach_the_ends) {
    auto levels = CHECK_OK(open(""));
    std::vector<std::uint8_t> pixels{16, 16, 16, 255, 235, 126, 235, 255};
    auto tick = levels.tick(0).frame(0, 0, ffrwd::Bytes(pixels));
    auto emitted = CHECK_OK(levels.process(tick));
    auto on = emitted.on("v");
    CHECK_EQ(on.size(), 1u);
    const auto* frame = std::get_if<ffrwd::FramePayload>(on.at(0));
    CHECK(frame && frame->data.vector() == (std::vector<std::uint8_t>{0, 0, 0, 255, 255, 128, 255, 255}));
}

TEST(the_full_range_passes_the_frame_on) {
    auto levels = CHECK_OK(open(R"({"black":0,"white":255})"));
    auto emitted = CHECK_OK(levels.process(levels.tick(0).frame(0, 0, ffrwd::Bytes(8))));
    auto on = emitted.on("v");
    CHECK(on.size() == 1 && std::holds_alternative<ffrwd::SamePayload>(*on[0]));
}

TEST(params_change_between_ticks) {
    auto levels = CHECK_OK(open(""));
    CHECK_OK(levels.set_params(R"({"black":0,"white":255})"));
    auto emitted = CHECK_OK(levels.process(levels.tick(0).frame(0, 0, ffrwd::Bytes(8))));
    auto on = emitted.on("v");
    CHECK(on.size() == 1 && std::holds_alternative<ffrwd::SamePayload>(*on[0]));
}

TEST(black_over_white_is_refused) {
    std::string error = CHECK_ERR(open(R"({"black":200,"white":100})"));
    CHECK_HAS(error, "`black` under `white`");
    CHECK(!open(R"({"black":-1})"));
}
