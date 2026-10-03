#include <optional>
#include <string>
#include <vector>

#include "check.hpp"
#include "ffrwd/rows.hpp"

using ffrwd::Json;

namespace {

struct Spot {
    double start_t = 0.0;
    std::uint64_t id = 0;
    std::uint32_t x = 0;
    std::string label;
    std::optional<bool> hidden;
    std::vector<float> vector;
    FFRWD_FIELDS(start_t, id, x, label, hidden, vector)
};

struct Sighting {
    ffrwd::Span span;
    std::string label;
    FFRWD_FIELDS(span, label)
};

}  // namespace

TEST(a_schema_from_a_row_type) {
    Json schema = CHECK_OK(Json::parse(ffrwd::schema_of<Spot>()));
    const Json& properties = schema["properties"];
    CHECK(properties["start_t"]["type"] == Json("number"));
    CHECK(properties["id"]["type"] == Json("integer"));
    CHECK(properties["x"]["type"] == Json("integer"));
    CHECK(properties["label"]["type"] == Json("string"));
    CHECK(properties["hidden"] == Json::object());
    CHECK(properties["vector"]["type"] == Json("array"));
    CHECK_EQ(schema["required"].dump(), std::string(R"(["id","label","start_t","vector","x"])"));
    CHECK_EQ(ffrwd::schema_of<Spot>().substr(0, 14), std::string(R"({"properties":)"));
}

TEST(rows_parse_and_say_which_did_not) {
    Spot spot = CHECK_OK(ffrwd::parse<Spot>(R"({"start_t":1.5,"id":2,"x":3,"label":"a","vector":[]})"));
    CHECK_EQ(spot.start_t, 1.5);
    CHECK_EQ(spot.id, 2u);
    CHECK_EQ(spot.x, 3u);
    CHECK(!spot.hidden);
    CHECK_HAS(CHECK_ERR(ffrwd::parse<Spot>(R"({"start_t":"soon"})")), "soon");
    CHECK_HAS(CHECK_ERR(ffrwd::parse<Spot>(R"({"start_t":1})")), "missing field `id`");
    CHECK_HAS(CHECK_ERR(ffrwd::parse<Spot>(R"({"start_t":1,"id":-1})")), "`id`");
}

TEST(a_span_runs_while_its_key_is_seen) {
    ffrwd::Spans<std::string> spans;
    std::vector<double> starts;
    bool seen[] = {true, true, false, true, true};
    for (int n = 0; n < 5; ++n) {
        spans.tick(n);
        if (seen[n]) starts.push_back(spans.see("mark").start_t);
    }
    CHECK(starts == (std::vector<double>{0.0, 0.0, 3.0, 3.0}));
}

TEST(a_gap_keeps_the_span) {
    ffrwd::Spans<> spans = ffrwd::Spans<>().gap(2);
    std::vector<double> starts;
    bool seen[] = {true, false, false, true, false, false, false, true};
    for (int n = 0; n < 8; ++n) {
        spans.tick(n * 0.5);
        if (seen[n]) starts.push_back(spans.see().start_t);
    }
    CHECK(starts == (std::vector<double>{0.0, 0.0, 3.5}));
}

TEST(the_longest_span_splits) {
    ffrwd::Spans<> spans = ffrwd::Spans<>().longest(3);
    std::vector<ffrwd::Span> seen;
    for (int n = 0; n < 7; ++n) {
        spans.tick(n);
        seen.push_back(spans.see());
    }
    std::vector<ffrwd::Span> expected = {{0.0, 0, 0}, {0.0, 0, 1}, {0.0, 0, 2}, {3.0, 1, 0},
                                         {3.0, 1, 1}, {3.0, 1, 2}, {6.0, 2, 0}};
    CHECK(seen == expected);
}

TEST(keys_have_spans_of_their_own) {
    ffrwd::Spans<std::string> spans;
    spans.tick(0.0);
    CHECK_EQ(spans.see("a").start_t, 0.0);
    spans.tick(1.0);
    CHECK_EQ(spans.see("a").start_t, 0.0);
    CHECK_EQ(spans.see("b").start_t, 1.0);
    CHECK_EQ(spans.open(), 2u);
    spans.tick(2.0);
    CHECK_EQ(spans.open(), 2u);
    spans.see("b");
    spans.tick(3.0);
    CHECK_EQ(spans.open(), 1u);
}

TEST(spans_starting_on_one_tick_write_one_start_t_and_their_own_ids) {
    ffrwd::Spans<std::string> spans;
    spans.tick(2.5);
    std::vector<std::string> rows;
    for (std::string key : {"a", "b"}) rows.push_back(ffrwd::to_json(Sighting{spans.see(key), key}).dump());
    CHECK_EQ(rows[0], std::string(R"({"start_t":2.5,"id":0,"label":"a"})"));
    CHECK_EQ(rows[1], std::string(R"({"start_t":2.5,"id":1,"label":"b"})"));
    Json schema = CHECK_OK(Json::parse(ffrwd::schema_of<Sighting>()));
    CHECK(schema["properties"]["start_t"]["type"] == Json("number"));
    CHECK(schema["properties"]["id"]["type"] == Json("integer"));
    Sighting read = CHECK_OK(ffrwd::parse<Sighting>(R"({"start_t":2.5,"id":1,"label":"b"})"));
    CHECK_EQ(read.span.start_t, 2.5);
    CHECK_EQ(read.span.id, 1u);
}

TEST(cues_show_while_they_cover_the_time) {
    ffrwd::Cues cues;
    cues.add({2.0, 4.0, "second"});
    cues.add({0.0, 3.0, "first"});
    auto at = [&](double t) {
        std::vector<std::string> texts;
        for (const ffrwd::Cue* cue : cues.at(t)) texts.push_back(cue->text);
        return texts;
    };
    CHECK(at(2.5) == (std::vector<std::string>{"first", "second"}));
    CHECK(at(3.0) == (std::vector<std::string>{"second"}));
    CHECK(at(4.0).empty());
    cues.drop_ended(3.0);
    CHECK_EQ(cues.size(), 1u);
}
