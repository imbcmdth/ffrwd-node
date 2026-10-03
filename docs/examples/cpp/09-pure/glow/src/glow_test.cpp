#include "glow.cpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

ffrwd::mock::Harness<GlowNode> open() {
    auto v =
        ffrwd::BoundStream::video("v", 0, 4, 4, "rgba", ffrwd::Rational(1, 15)).rate(ffrwd::Rational(15, 1));
    return CHECK_OK(ffrwd::mock::Harness<GlowNode>::open(R"({"every":3})", {v}));
}

/// A picture lit at one pixel, which moves along the top row.
ffrwd::Bytes lit(std::size_t n) {
    ffrwd::Bytes pixels(4 * 4 * 4);
    std::fill(pixels.data() + (n % 4) * 4, pixels.data() + (n % 4) * 4 + 4, 255);
    return pixels;
}

/// Every row of `ticks` ticks handed to `workers` instances in turn, in
/// pts order.
std::vector<std::pair<std::int64_t, std::string>> rows(std::size_t ticks, std::size_t workers) {
    std::vector<ffrwd::mock::Harness<GlowNode>> instances;
    for (std::size_t n = 0; n < workers; ++n) instances.push_back(open());
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (std::size_t n = 0; n < ticks; ++n) {
        auto& worker = instances[n % workers];
        auto tick = worker.tick(std::int64_t(n)).ordinal(n).frame(0, std::int64_t(n), lit(n));
        for (auto& row : CHECK_OK(worker.process(tick)).messages("glows")) rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

TEST(any_number_of_workers_write_the_same_rows) {
    auto alone = rows(10, 1);
    CHECK_EQ(alone.size(), 10u);
    CHECK(rows(10, 2) == alone);
    CHECK(rows(10, 3) == alone);
}

TEST(a_sighting_starts_every_so_many_frames) {
    auto ids = rows(7, 1);
    CHECK(ids[2].second.starts_with(R"({"start_t":0.0,"id":0,)"));
    CHECK(ids[3].second.starts_with(R"({"start_t":0.2,"id":1,)"));
}
