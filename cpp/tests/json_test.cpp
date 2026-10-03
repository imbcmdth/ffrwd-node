#include "check.hpp"
#include "ffrwd/json.hpp"

using ffrwd::Json;

TEST(json_reads_and_writes_back_compactly) {
    auto text = R"( {"a": [1, 2.5, true, null], "b": {"c": "d\né"}, "e": -0.0} )";
    Json value = CHECK_OK(Json::parse(text));
    CHECK_EQ(value.dump(), std::string(R"({"a":[1,2.5,true,null],"b":{"c":"d\n)") + "\xc3\xa9" + R"("},"e":-0.0})");
    CHECK(value["a"][0].is_int());
    CHECK(value["a"][1].is_float());
    CHECK(value["missing"].is_null());
}

TEST(json_refuses_what_is_not_json) {
    CHECK_HAS(CHECK_ERR(Json::parse("{\"a\":}")), "expected value");
    CHECK_HAS(CHECK_ERR(Json::parse("[1] 2")), "trailing characters");
    CHECK_HAS(CHECK_ERR(Json::parse("")), "EOF");
}

TEST(doubles_write_as_serde_json_writes_them) {
    CHECK_EQ(ffrwd::format_double(0.0), std::string("0.0"));
    CHECK_EQ(ffrwd::format_double(0.3), std::string("0.3"));
    CHECK_EQ(ffrwd::format_double(30.0), std::string("30.0"));
    CHECK_EQ(ffrwd::format_double(0.066650390625), std::string("0.066650390625"));
    CHECK_EQ(ffrwd::format_double(1.0 / 3.0), std::string("0.3333333333333333"));
    CHECK_EQ(ffrwd::format_double(1e15), std::string("1000000000000000.0"));
    CHECK_EQ(ffrwd::format_double(1e16), std::string("1e16"));
    CHECK_EQ(ffrwd::format_double(1.5e-7), std::string("1.5e-7"));
    CHECK_EQ(ffrwd::format_double(0.0001), std::string("0.0001"));
    CHECK_EQ(ffrwd::format_double(-2.5), std::string("-2.5"));
}

TEST(a_whole_number_is_not_a_fraction) {
    CHECK(!(Json(1) == Json(1.0)));
    CHECK(Json(1.0) == Json(1.0));
    Json a = CHECK_OK(Json::parse(R"({"x":1,"y":[2]})"));
    Json b = CHECK_OK(Json::parse(R"({"y":[2],"x":1})"));
    CHECK(a == b);
}

TEST(keys_keep_their_order_and_set_replaces_in_place) {
    Json value = Json::object();
    value.set("b", 1);
    value.set("a", 2);
    value.set("b", 3);
    CHECK_EQ(value.dump(), std::string(R"({"b":3,"a":2})"));
    CHECK(value.erase("b"));
    CHECK_EQ(value.dump(), std::string(R"({"a":2})"));
}
