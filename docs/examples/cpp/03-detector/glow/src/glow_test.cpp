#include "glow.cpp"

#include <algorithm>
#include <string>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

/// The `start_t` of every row of four lit ticks handed to `workers`
/// instances in turn, in pts order.
std::vector<std::string> starts(std::size_t workers) {
    auto open = [] {
        auto v = ffrwd::BoundStream::video("v", 0, 2, 2, "rgba", ffrwd::Rational(1, 10));
        return CHECK_OK(ffrwd::mock::Harness<GlowNode>::open("", {v}));
    };
    std::vector<ffrwd::mock::Harness<GlowNode>> instances;
    for (std::size_t n = 0; n < workers; ++n) instances.push_back(open());
    std::vector<std::pair<std::int64_t, std::string>> rows;
    for (std::int64_t n = 0; n < 4; ++n) {
        auto& worker = instances[std::size_t(n) % workers];
        auto tick = worker.tick(n).ordinal(std::uint64_t(n)).frame(0, n, ffrwd::Bytes::filled(16, 255));
        for (auto& row : CHECK_OK(worker.process(tick)).messages("glows")) rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    std::vector<std::string> starts;
    for (const auto& [pts, row] : rows) starts.push_back(row.substr(0, row.find(',')));
    return starts;
}

TEST(spans_kept_across_ticks_split_with_the_workers) {
    CHECK(starts(1) == std::vector<std::string>(4, R"({"start_t":0.0)"));
    CHECK(starts(2) == (std::vector<std::string>{R"({"start_t":0.0)", R"({"start_t":0.1)",
                                                 R"({"start_t":0.0)", R"({"start_t":0.1)"}));
}
