#include <optional>
#include <string>

#include "check.hpp"
#include "ffrwd/params.hpp"

namespace {

constexpr std::string_view SCHEMA = R"({
    "type": "object",
    "properties": {
        "every": {"type": "integer", "minimum": 1, "default": 30},
        "amount": {"type": "number", "minimum": 0, "maximum": 1, "default": 0.5},
        "text": {"type": "string", "minLength": 1},
        "mode": {"enum": ["over", "under"], "default": "over"},
        "fps": {"type": ["number", "null"]}
    },
    "required": ["text"],
    "additionalProperties": false
})";

struct Params {
    std::uint32_t every = 0;
    double amount = 0.0;
    std::string text;
    std::string mode;
    std::optional<double> fps;
    FFRWD_FIELDS(every, amount, text, mode, fps)
};

}  // namespace

TEST(defaults_fill_what_the_call_left_out) {
    auto [params, value] = CHECK_OK(ffrwd::read_params<Params>(SCHEMA, R"({"text":"hi"})"));
    CHECK_EQ(params.every, 30u);
    CHECK_EQ(params.amount, 0.5);
    CHECK_EQ(params.text, std::string("hi"));
    CHECK_EQ(params.mode, std::string("over"));
    CHECK(!params.fps);
    CHECK(value["every"] == ffrwd::Json(30));
}

TEST(a_whole_float_reads_as_an_integer) {
    auto [params, value] = CHECK_OK(ffrwd::read_params<Params>(SCHEMA, R"({"text":"hi","every":15.0})"));
    CHECK_EQ(params.every, 15u);
    CHECK(value["every"].is_int());
    CHECK_HAS(CHECK_ERR(ffrwd::read_params<Params>(SCHEMA, R"({"text":"hi","every":1.5})")),
              "`every` is integer");
}

TEST(null_is_not_set) {
    auto [params, value] =
        CHECK_OK(ffrwd::read_params<Params>(SCHEMA, R"({"text":"hi","fps":null,"every":null})"));
    CHECK(!params.fps);
    CHECK_EQ(params.every, 30u);
}

TEST(refusals_name_the_param) {
    std::pair<const char*, const char*> cases[] = {
        {R"({"text":"hi","every":0})", "`every` is at least 1"},
        {R"({"text":"hi","amount":2})", "`amount` is at most 1"},
        {R"({"text":""})", "`text` is at least 1 character"},
        {R"({"text":"hi","mode":"sideways"})", "`mode` is one of"},
        {R"({"text":"hi","colour":"red"})", "`colour` is not a param here"},
        {R"({})", "`text` is required"},
        {R"([1])", "a JSON object"},
        {R"({"text":7})", "`text` is string"},
    };
    for (auto [params, said] : cases) CHECK_HAS(CHECK_ERR(ffrwd::read_params<Params>(SCHEMA, params)), said);
}

TEST(no_params_reads_nothing) {
    CHECK_OK(ffrwd::read_params<ffrwd::NoParams>(ffrwd::NO_PARAMS, ""));
    CHECK_OK(ffrwd::read_params<ffrwd::NoParams>(ffrwd::NO_PARAMS, " {} "));
    CHECK_HAS(CHECK_ERR(ffrwd::read_params<ffrwd::NoParams>(ffrwd::NO_PARAMS, R"({"x":1})")), "takes none");
}
