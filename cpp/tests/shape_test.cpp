#include "check.hpp"
#include "ffrwd/shape.hpp"

using namespace ffrwd;

namespace {

Shape filter() {
    return Shape().input(Input::video("v").clock().pixel_formats({"rgba"})).output(Output::like("v"));
}

}  // namespace

TEST(the_clock_comes_from_the_input_marked) {
    NodeShape shape = CHECK_OK(filter().resolve(Bound{"v"}));
    CHECK(shape.clock == Clock::input("v"));
    CHECK(shape.outputs[0].kind == Kind::Video);
    CHECK(shape.outputs[0].like->port == std::optional<std::string>("v"));
}

TEST(a_pixel_format_alone_follows_the_clock) {
    NodeShape shape = CHECK_OK(Shape()
                                   .input(Input::video("v").clock())
                                   .output(Output::video("mask").pixel_format("gray"))
                                   .resolve(Bound{"v"}));
    const Like& like = *shape.outputs[0].like;
    CHECK(like.port == std::optional<std::string>("v"));
    CHECK(like.pixel_format == std::optional<std::string>("gray"));
}

TEST(an_output_following_an_unbound_input_is_left_out) {
    NodeShape shape = CHECK_OK(Shape()
                                   .input(Input::video("v").clock())
                                   .input(Input::audio("a").optional())
                                   .output(Output::like("v"))
                                   .output(Output::like("a"))
                                   .resolve(Bound{"v"}));
    CHECK_EQ(shape.outputs.size(), 1u);
    CHECK_EQ(shape.outputs[0].name, std::string("v"));
}

TEST(refusals_name_the_port) {
    CHECK_ERR(Shape().input(Input::video("v")).resolve(Bound{"v"}));
    CHECK_HAS(CHECK_ERR(Shape().input(Input::video("v").clock().optional()).resolve(Bound{"v"})), "`v`");
    CHECK_HAS(CHECK_ERR(filter().input(Input::rows("boxes").hold()).resolve(Bound{"v", "boxes"})), "`boxes`");
    CHECK_ERR(filter().input(Input::video("w").interval()).resolve(Bound{"v", "w"}));
    CHECK_ERR(filter().input(Input::rows("boxes").ignore_rows()).resolve(Bound{"v", "boxes"}));
    CHECK_ERR(Shape().input(Input::audio("a").clock().window(4, 5)).resolve(Bound{"a"}));
    CHECK_HAS(CHECK_ERR(Shape()
                            .input(Input::video("v").many().hold())
                            .rate_of("v")
                            .output(Output::video("out").following("v"))
                            .resolve(Bound{"v"})),
              "many");
    CHECK_ERR(Shape()
                  .rate(Rational(30, 1))
                  .input(Input::video("v"))
                  .output(Output::video("out").size(64, 64).pixel_format("rgba"))
                  .resolve(Bound{"v"}));
}

TEST(a_generator_gives_its_own_format) {
    Shape ticker = Shape()
                       .rate(Rational(30, 1))
                       .output(Output::video("video").size(1280, 720).pixel_format("rgba").row(0))
                       .relation_row("{}")
                       .bounded(false);
    NodeShape shape = CHECK_OK(ticker.resolve(Bound{}));
    const auto* video = std::get_if<VideoFormat>(&*shape.outputs[0].format);
    CHECK(video && video->width == 1280 && video->height == 720 && video->pix_fmt == "rgba");
    CHECK(!shape.bounded);
    CHECK_ERR(Shape().rate(Rational(30, 1)).output(Output::video("video")).resolve(Bound{}));
    CHECK_ERR(Shape().rate(Rational(30, 1)).output(Output::video("video").size(8, 8)).resolve(Bound{}));
}

TEST(hold_and_interval_fields_chain) {
    InputPort feed = Input::video("feed").optional().hold().lead(0.5).port_param("port").port();
    const Hold& hold = std::get<Hold>(feed.pairing);
    CHECK(hold.anchor == Anchor::first_frame());
    CHECK_EQ(hold.lead, 0.5);
    CHECK(hold.port_param == std::optional<std::string>("port"));

    InputPort words = Input::rows("words").latency(2.0).ahead(0.5).state().port();
    Interval expected;
    expected.latency = 2.0;
    expected.ahead = 0.5;
    CHECK(std::get<Interval>(words.pairing) == expected);
    CHECK(words.rows == RowsUse::State);
}

TEST(a_timing_input_is_a_frame_kind) {
    NodeShape shape = CHECK_OK(Shape()
                                   .input(Input::video("v").clock().timing())
                                   .output(Output::video("mask").pixel_format("gray"))
                                   .resolve(Bound{"v"}));
    CHECK(shape.inputs[0].accepts.wants == Wants::Timing);
    CHECK_OK(Shape().input(Input::audio("a").clock()).input(Input::audio("b").timing().like("a")).resolve(Bound{"a", "b"}));
    std::string err = CHECK_ERR(filter().input(Input::rows("boxes").interval().timing()).resolve(Bound{"v", "boxes"}));
    CHECK_HAS(err, "`boxes`");
    CHECK_HAS(err, "timing");
}

TEST(a_data_input_takes_an_anchor_and_a_hold_group) {
    InputPort follow = Input::rows("d").anchor(Anchor::first_frame()).port();
    Interval first;
    first.anchor = Anchor::first_frame();
    CHECK(std::get<Interval>(follow.pairing) == first);

    Input feed = Input::video("feed").optional().group("ad");
    CHECK(std::get<Hold>(feed.port().pairing).group == std::optional<std::string>("ad"));

    Input beside = Input::rows("cues").latency(1.0).group("ad");
    const Interval& interval = std::get<Interval>(beside.port().pairing);
    CHECK(interval.group == std::optional<std::string>("ad"));
    CHECK(interval.latency == std::optional<double>(1.0));
    CHECK(interval.anchor == Anchor::shared_clock());

    Bound bound{"v", "feed", "cues"};
    CHECK_OK(filter().input(feed).input(beside).resolve(bound));

    std::string err = CHECK_ERR(filter().input(Input::video("feed").optional().hold()).input(beside).resolve(bound));
    CHECK_HAS(err, "`cues`");
    CHECK_HAS(err, "`ad`");

    err = CHECK_ERR(filter().input(feed).input(Input(beside).anchor(Anchor::tagged("smart_timed"))).resolve(bound));
    CHECK_HAS(err, "`cues`");
    CHECK_HAS(err, "shared clock");
}

TEST(a_held_frame_input_keeps_its_anchor_and_group) {
    InputPort feed = Input::video("feed").anchor(Anchor::tagged("smart_timed")).group("ad").port();
    const Hold& hold = std::get<Hold>(feed.pairing);
    CHECK(hold.anchor == Anchor::tagged("smart_timed"));
    CHECK(hold.group == std::optional<std::string>("ad"));
}

TEST(bound_says_how_many_streams_and_at_what_rate) {
    Bound bound = Bound{"v"}.rate("v", Rational(30000, 1001)).bind("inputs", {Rational(25, 1), std::nullopt});
    CHECK(bound.has("v") && bound.has("inputs") && !bound.has("a"));
    CHECK_EQ(bound.count("v"), 1u);
    CHECK_EQ(bound.count("inputs"), 2u);
    CHECK_EQ(bound.count("a"), 0u);
    CHECK(bound.rate_of("v") == Rational(30000, 1001));
    CHECK(bound.rate_of("inputs") == Rational(25, 1));
    CHECK(!bound.streams("inputs")[1].rate);
    CHECK(!bound.rate_of("a"));
    CHECK_EQ(bound.inputs()[0].input, std::string("v"));
    CHECK_EQ(bound.inputs()[1].input, std::string("inputs"));

    CHECK_EQ(Bound().rate("a", Rational(48000, 1)).count("a"), 1u);
    CHECK(Bound::names({"v", "a"}) == (Bound{"v", "a"}));
    CHECK_EQ(Bound::names({"v", "v", "v"}).count("v"), 3u);
}

TEST(bound_streams_carry_the_hints_the_shape_was_asked_with) {
    Rational tb(1, 15360);
    std::vector<BoundStream> streams = {
        BoundStream::video("v", 0, 2, 2, "rgba", tb).rate(Rational(30000, 1001)),
        BoundStream::audio("a", 1, 48000, 2, "f32"),
        BoundStream::video("inputs", 2, 2, 2, "rgba", tb).rate(Rational(25, 1)),
        BoundStream::video("inputs", 3, 2, 2, "rgba", tb),
        BoundStream::rows("d", 4, tb),
    };
    Bound bound = Bound::of(streams);
    CHECK(bound.rate_of("v") == Rational(30000, 1001));
    CHECK(bound.rate_of("a") == Rational(48000, 1));
    CHECK_EQ(bound.count("inputs"), 2u);
    CHECK(!bound.streams("inputs")[1].rate);
    CHECK(!bound.rate_of("d"));
    Bound asked = Bound{"v", "a"}
                      .rate("v", Rational(30000, 1001))
                      .rate("a", Rational(48000, 1))
                      .bind("inputs", {Rational(25, 1), std::nullopt})
                      .bind("d", {std::nullopt});
    CHECK(bound == asked);
}
