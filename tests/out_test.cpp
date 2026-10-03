#include "check.hpp"
#include "ffrwd/out.hpp"

using namespace ffrwd;

namespace {

Out out() {
    NodeShape shape = check::ok(__FILE__, __LINE__,
                                Shape()
                                    .input(Input::video("v").clock())
                                    .input(Input::packets("coded"))
                                    .output(Output::like("v"))
                                    .output(Output::rows("spots").time_base(Rational(1, 1000)))
                                    .output(Output::packets("p").following("coded"))
                                    .resolve(Bound{"v", "coded"}));
    Out out(shape);
    out.begin(Rational(1, 15));
    return out;
}

Packet packet(std::int64_t pts, std::optional<std::int64_t> dts) {
    Packet made;
    made.pts = pts;
    made.dts = dts;
    return made;
}

}  // namespace

TEST(a_port_never_goes_back) {
    Out emit = out();
    CHECK_OK(emit.same("v", 2, 1, 0, 0));
    CHECK_OK(emit.same("v", 2, 1, 0, 0));
    CHECK_HAS(CHECK_ERR(emit.same("v", 1, 1, 0, 0)), "from pts 2 to 1");
    emit.take();
    CHECK_ERR(emit.frame("v", 1, std::nullopt, Bytes()));
    CHECK_OK(emit.row("spots", 0, 1));
    CHECK(emit.last("spots") == std::optional<std::int64_t>(0));
}

TEST(ports_and_kinds_are_checked) {
    Out emit = out();
    CHECK_ERR(emit.row("nowhere", 0, 1));
    CHECK_HAS(CHECK_ERR(emit.row("v", 0, 1)), "video output");
    CHECK_ERR(emit.frame("spots", 0, std::nullopt, Bytes()));
}

TEST(packets_keep_decode_order) {
    Out emit = out();
    CHECK_OK(emit.packet("p", packet(3, std::nullopt)));
    CHECK_OK(emit.packet("p", packet(3, 0)));
    CHECK_OK(emit.packet("p", packet(1, 1)));
    CHECK_ERR(emit.packet("p", packet(2, 0)));
}

TEST(seconds_land_in_the_port_time_base) {
    Out emit = out();
    CHECK_EQ(CHECK_OK(emit.pts("v", 2.0)), 30);
    CHECK_EQ(CHECK_OK(emit.pts("spots", 2.0)), 2000);
    CHECK_OK(emit.cue("spots", Cue{1.5, 2.0, "hi"}));
    Emitted emitted = emit.take();
    auto messages = emitted.messages("spots");
    CHECK_EQ(messages.size(), 1u);
    CHECK_EQ(messages[0].first, 1500);
    CHECK_EQ(messages[0].second, std::string(R"({"start_t":1.5,"end_t":2.0,"text":"hi"})"));
}

TEST(reports_and_finish_ride_the_tick) {
    Out emit = out();
    emit.report(Cue{0.0, 1.0, "a"});
    emit.finish();
    Emitted emitted = emit.take();
    CHECK_EQ(emitted.reports.size(), 1u);
    CHECK(emitted.finished);
    CHECK(!emit.take().finished);
}
