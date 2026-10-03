#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "fields.hpp"
#include "json.hpp"
#include "result.hpp"

namespace ffrwd {

/// The schema of a node that takes no params.
inline constexpr std::string_view NO_PARAMS =
    R"({"type":"object","properties":{},"additionalProperties":false})";

/// The params of a node that takes none.
struct NoParams {
    FFRWD_FIELDS()
    friend bool operator==(const NoParams&, const NoParams&) = default;
};

/// `params` checked against `schema` and filled in: an empty string is `{}`,
/// a param set to null is a param not set, every param is checked against
/// the schema, the schema's defaults fill in what is not set, and a whole
/// number given as `30.0` to a param declared `integer` reads as `30`.
///
/// The schema keywords checked are `type`, `enum`, `const`, `minimum`,
/// `maximum`, `exclusiveMinimum`, `exclusiveMaximum`, `minLength`,
/// `maxLength`, `minItems`, `maxItems`, `items`, `properties`, `required`
/// and `additionalProperties`; the compiler checks the call's params against
/// the whole schema before a node sees them.
Result<Json> check_params(std::string_view schema, std::string_view params);

/// `params` read as a `P` against `schema`, beside the object it was read
/// from.
template <class P>
Result<std::pair<P, Json>> read_params(std::string_view schema, std::string_view params) {
    FFRWD_LET(value, check_params(schema, params));
    P parsed{};
    if (auto read = from_json(value, parsed); !read)
        return fail("the params do not read: " + read.error().message);
    return std::pair<P, Json>(std::move(parsed), std::move(value));
}

}  // namespace ffrwd
