#include <string>
#include <vector>

#include "check.hpp"
#include "dim/dim.hpp"
#include "ffrwd/mock.hpp"
#include "spot/spot.hpp"

using namespace ffrwd;

namespace {

Bytes marked(std::size_t x0) {
    auto picture = stand_ins::Picture::filled(64, 48, {200, 30, 30, 255});
    picture.fill({x0, 8, x0 + 12, 20}, {128, 128, 128, 255});
    return std::move(picture.data);
}

}  // namespace

TEST(dim_overlapping_boxes_dim_once) {
    auto inside = dim::covered({{1.0, 1.0, 2.0, 2.0}, {2.0, 2.0, 9.0, 9.0}}, 4, 4);
    std::size_t count = 0;
    for (bool each : inside) count += each;
    CHECK_EQ(count, 3u + 4u);
}

TEST(dim_reads_six_field_rows_for_their_box) {
    Rational tb(1, 15);
    auto node = CHECK_OK(mock::Harness<dim::Dim>::open(
        R"({"amount":0.75})", {BoundStream::video("v", 0, 4, 4, "rgba", tb), BoundStream::rows("boxes", 1, tb)}));
    auto tick = node.tick(0)
                    .frame(0, 0, Bytes::filled(4 * 4 * 4, 200))
                    .message(1, 0, R"({"start_t":0,"id":0,"x":0,"y":0,"w":2,"h":1})");
    Emitted emitted = CHECK_OK(node.process(tick));
    auto on = emitted.on("v");
    CHECK_EQ(on.size(), 1u);
    const auto& frame = std::get<FramePayload>(*on[0]);
    CHECK((std::vector<int>{frame.data[0], frame.data[1], frame.data[2], frame.data[3]}) ==
          (std::vector<int>{50, 50, 50, 200}));
    CHECK((std::vector<int>{frame.data[8], frame.data[9], frame.data[10], frame.data[11]}) ==
          (std::vector<int>{200, 200, 200, 200}));
}

TEST(dim_passes_a_frame_with_no_boxes_on) {
    Rational tb(1, 15);
    auto node = CHECK_OK(mock::Harness<dim::Dim>::open(
        "", {BoundStream::video("v", 0, 4, 4, "rgba", tb), BoundStream::rows("boxes", 1, tb)}));
    Emitted emitted = CHECK_OK(node.process(node.tick(3).frame(0, 3, Bytes(64))));
    const auto& same = std::get<SamePayload>(*emitted.on("v")[0]);
    CHECK_EQ(same.pts, 3);
    CHECK_OK(node.set_params(R"({"amount":0})"));
    CHECK_EQ(node.node().amount, 0.0);
}

TEST(spot_writes_a_row_a_frame_named_by_the_sighting) {
    auto v = BoundStream::video("v", 0, 64, 48, "rgba", Rational(1, 10));
    auto node = CHECK_OK(mock::Harness<spot::Spot>::open(R"({"every":3})", {v}));
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (std::int64_t n = 0; n < 4; ++n) {
        Emitted emitted = CHECK_OK(node.process(node.tick(n).frame(0, n, marked(4 + n))));
        for (auto& row : emitted.messages("spots")) rows.push_back(row);
    }
    CHECK_EQ(rows.size(), 4u);
    std::vector<stand_ins::Spot> read;
    for (const auto& [pts, json] : rows) read.push_back(CHECK_OK(parse<stand_ins::Spot>(json)));
    for (std::int64_t n = 0; n < 4; ++n) CHECK_EQ(rows[n].first, n);
    CHECK_EQ(read[0].span.start_t, 0.0);
    CHECK_EQ(read[2].span.id, 0u);
    CHECK_EQ(read[3].span.start_t, 0.3);
    CHECK_EQ(read[3].span.id, 1u);
    CHECK_EQ(rows[2].second, std::string(R"({"start_t":0.0,"id":0,"x":6,"y":8,"w":12,"h":12})"));
}

TEST(spot_writes_no_row_without_a_mark) {
    auto v = BoundStream::video("v", 0, 64, 48, "rgba", Rational(1, 10));
    auto node = CHECK_OK(mock::Harness<spot::Spot>::open("", {v}));
    auto blank = stand_ins::Picture::filled(64, 48, {0, 0, 0, 255});
    Emitted emitted = CHECK_OK(node.process(node.tick(0).frame(0, 0, std::move(blank.data))));
    CHECK(emitted.items.empty());
}

TEST(spot_counts_sightings_in_frames_at_the_bound_rate) {
    auto fine = BoundStream::video("v", 0, 64, 48, "rgba", Rational(1, 90000)).rate(Rational(30, 1));
    stand_ins::Spotter spotter(3, fine);
    auto picture = CHECK_OK(stand_ins::Picture::of(marked(4), 64, 48));
    auto seen = spotter.see(7, 7 * 3000, picture);
    CHECK(seen.has_value());
    CHECK_EQ(seen->span.id, 2u);
    CHECK_EQ(seen->span.start_t, 0.2);
}

TEST(examples_shape_as_the_host_reads_them) {
    NodeShape dim = CHECK_OK(Runner<dim::Dim>::shape("", Bound{"v", "boxes"}));
    CHECK_EQ(*dim.inputs[1].schema,
             std::string(R"({"properties":{"h":{"type":"number"},"w":{"type":"number"},"x":{"type":"number"},"y":{"type":"number"}},"required":["h","w","x","y"],"type":"object"})"));
    CHECK(dim.pure && dim.one_to_one);
    NodeShape spot = CHECK_OK(Runner<spot::Spot>::shape("", Bound{"v"}));
    CHECK_EQ(*spot.outputs[0].schema,
             std::string(R"({"properties":{"h":{"type":"integer"},"id":{"type":"integer"},"start_t":{"type":"number"},"w":{"type":"integer"},"x":{"type":"integer"},"y":{"type":"integer"}},"required":["h","id","start_t","w","x","y"],"type":"object"})"));
}
