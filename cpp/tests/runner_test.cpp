#include <string>
#include <tuple>
#include <vector>

#include "check.hpp"
#include "ffrwd/mock.hpp"

using namespace ffrwd;

namespace {

struct FoldingParams {
    std::string label;
    FFRWD_FIELDS(label)
};

struct Folding : Node<Folding, FoldingParams> {
    static constexpr std::string_view name = "folding";
    static constexpr std::string_view version = "0.0.0";
    static constexpr std::string_view params_schema =
        R"({"type":"object","properties":{"label":{"type":"string","default":"a"}},"additionalProperties":false})";

    std::uint32_t v = 0;
    std::vector<std::tuple<std::string, std::int64_t, std::string>> folded;

    static Result<Shape> shape(const FoldingParams&, const Bound&) {
        return Shape()
            .input(Input::video("v").clock())
            .input(Input::rows("notes").interval().state())
            .output(Output::rows("seen"));
    }

    static Result<Folding> init(FoldingParams, const Init& init) {
        FFRWD_LET(v, init.stream("v"));
        Folding node;
        node.v = v.id;
        return node;
    }

    Status fold(const StateRow& row) {
        FFRWD_LET(note, row.row<Json>());
        folded.emplace_back(std::string(row.port), row.pts, note["note"].dump());
        return {};
    }

    Status process(const ffrwd::Tick& tick, Out& out) {
        auto frame = tick.frame(v);
        if (!frame) return fail("no frame");
        return out.row("seen", frame->pts, folded.size());
    }
};

struct Plain : Node<Plain> {
    static constexpr std::string_view name = "plain";
    static constexpr std::string_view version = "0.0.0";

    std::uint32_t v = 0;

    static Result<Shape> shape(const NoParams&, const Bound&) {
        return Shape()
            .input(Input::video("v").clock().timing())
            .input(Input::rows("notes").interval().state())
            .output(Output::video("mask").pixel_format("gray"));
    }

    static Result<Plain> init(NoParams, const Init& init) {
        Plain node;
        node.v = init.optional("v")->id;
        return node;
    }

    Status process(const ffrwd::Tick& tick, Out&) {
        tick.fetch(v, 0);
        return {};
    }
};

mock::Harness<Folding> harness() {
    Rational tb(1, 10);
    return check::ok(__FILE__, __LINE__,
                     mock::Harness<Folding>::open("", {BoundStream::video("v", 0, 2, 2, "rgba", tb),
                                                       BoundStream::rows("notes", 1, tb)}));
}

}  // namespace

TEST(earlier_rows_fold_before_the_tick_s_own) {
    auto node = harness();
    auto tick = node.tick(5)
                    .frame(0, 5, Bytes(16))
                    .earlier(1, 1, {R"({"note":1})", R"({"note":2})"})
                    .earlier(1, 3, {R"({"note":3})"})
                    .message(1, 5, R"({"note":4})");
    Emitted emitted = CHECK_OK(node.process(tick));
    std::vector<std::pair<std::int64_t, std::string>> folded;
    for (const auto& [port, pts, note] : node.node().folded) folded.emplace_back(pts, note);
    CHECK(folded == (std::vector<std::pair<std::int64_t, std::string>>{{1, "1"}, {1, "2"}, {3, "3"}, {5, "4"}}));
    auto seen = emitted.messages("seen");
    CHECK_EQ(seen.size(), 1u);
    CHECK_EQ(seen[0].first, 5);
    CHECK_EQ(seen[0].second, std::string("4"));
}

TEST(params_in_force_are_taken_and_others_refused) {
    auto node = harness();
    CHECK_OK(node.set_params(R"({"label":"a"})"));
    CHECK_OK(node.set_params(""));
    CHECK_HAS(CHECK_ERR(node.set_params(R"({"label":"b"})")), "cannot change its params");
}

TEST(describe_is_the_constants) {
    Meta meta = Runner<Folding>::describe();
    CHECK_EQ(meta.name, std::string("folding"));
    CHECK_EQ(meta.version, std::string("0.0.0"));
    CHECK_HAS(meta.params_schema, "label");
    CHECK(meta.rows_schema.empty());
    CHECK_EQ(Runner<Plain>::describe().params_schema, std::string(NO_PARAMS));
}

TEST(a_shape_is_resolved_for_the_host) {
    NodeShape shape = CHECK_OK(Runner<Folding>::shape("", Bound{"v"}));
    CHECK(shape.clock_input() == std::optional<std::string_view>("v"));
    CHECK_ERR(Runner<Folding>::shape(R"({"label":3})", Bound{}));
}

TEST(a_node_without_fold_refuses_its_state_rows) {
    Rational tb(1, 10);
    auto plain = CHECK_OK(mock::Harness<Plain>::open(
        "", {BoundStream::video("v", 0, 2, 2, "rgba", tb), BoundStream::rows("notes", 1, tb)}));
    std::string err = CHECK_ERR(plain.process(plain.tick(0).frame(0, 0, Bytes(16)).message(1, 0, R"({"a":1})")));
    CHECK_HAS(err, "give it a `fold`");
}

TEST(fetching_a_timing_input_ends_the_run_with_its_port_named) {
    Rational tb(1, 10);
    auto plain = CHECK_OK(mock::Harness<Plain>::open(
        "", {BoundStream::video("v", 0, 2, 2, "rgba", tb), BoundStream::rows("notes", 1, tb)}));
    std::string err = CHECK_ERR(plain.process(plain.tick(0).frame(0, 0, Bytes(16))));
    CHECK_HAS(err, "fetched a frame of `v`");
}

TEST(ticks_are_numbered_one_past_the_last) {
    auto node = harness();
    CHECK_EQ(node.tick(0).ordinal(), 0u);
    CHECK_OK(node.process(node.tick(0).ordinal(7).frame(0, 0, Bytes(16))));
    CHECK_EQ(node.tick(1).ordinal(), 8u);
}
