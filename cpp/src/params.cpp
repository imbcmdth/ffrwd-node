#include "ffrwd/params.hpp"

#include <cmath>
#include <limits>
#include <vector>

namespace ffrwd {

namespace {

std::string named(std::string_view path) {
    return path.empty() ? std::string("the params") : "`" + std::string(path) + "`";
}

std::string join(std::string_view path, std::string_view name) {
    return path.empty() ? std::string(name) : std::string(path) + "." + std::string(name);
}

std::string counted(std::uint64_t n, std::string_view what) {
    return std::to_string(n) + " " + std::string(what) + (n == 1 ? "" : "s");
}

std::string bound_text(const Json& bound) {
    std::string text = bound.dump();
    if (bound.is_float() && text.ends_with(".0")) text.resize(text.size() - 2);
    return text;
}

bool whole(double value) { return std::isfinite(value) && std::trunc(value) == value; }

bool type_matches(const Json& value, std::string_view kind) {
    if (kind == "null") return value.is_null();
    if (kind == "boolean") return value.is_bool();
    if (kind == "string") return value.is_string();
    if (kind == "array") return value.is_array();
    if (kind == "object") return value.is_object();
    if (kind == "number") return value.is_number();
    if (kind == "integer") return value.is_int() || (value.is_float() && whole(value.as_double()));
    return true;
}

std::optional<std::uint64_t> count_of(const Json& schema, std::string_view key) {
    const Json* found = schema.find(key);
    if (!found || !found->is_int() || found->as_int() < 0) return std::nullopt;
    return std::uint64_t(found->as_int());
}

std::size_t characters(const std::string& text) {
    std::size_t count = 0;
    for (char c : text)
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++count;
    return count;
}

Status check(Json& value, const Json& schema, const std::string& path) {
    if (!schema.is_object()) return {};
    if (const Json* kind = schema.find("type")) {
        std::vector<std::string> kinds;
        if (kind->is_string()) {
            kinds.push_back(kind->as_string());
        } else if (kind->is_array()) {
            for (const Json& each : kind->as_array())
                if (each.is_string()) kinds.push_back(each.as_string());
        }
        bool matched = kinds.empty();
        for (const std::string& each : kinds)
            if (type_matches(value, each)) matched = true;
        if (!matched) {
            std::string listed;
            for (std::size_t n = 0; n < kinds.size(); ++n) listed += (n ? " or " : "") + kinds[n];
            return fail(named(path) + " is " + listed + ", not " + value.dump());
        }
        bool integer = false, number = false;
        for (const std::string& each : kinds) {
            integer = integer || each == "integer";
            number = number || each == "number";
        }
        if (integer && !number && value.is_float() && whole(value.as_double()) &&
            value.as_double() >= -9223372036854775808.0 && value.as_double() < 9223372036854775808.0)
            value = Json(static_cast<std::int64_t>(value.as_double()));
    }
    if (const Json* allowed = schema.find("enum"); allowed && allowed->is_array()) {
        bool found = false;
        for (const Json& option : allowed->as_array()) found = found || option == value;
        if (!found) {
            std::string listed;
            for (std::size_t n = 0; n < allowed->size(); ++n)
                listed += (n ? ", " : "") + (*allowed)[n].dump();
            return fail(named(path) + " is one of " + listed + ", not " + value.dump());
        }
    }
    if (const Json* constant = schema.find("const"); constant && !(*constant == value))
        return fail(named(path) + " is " + constant->dump() + ", not " + value.dump());
    if (value.is_number()) {
        double number = value.as_double();
        auto bound = [&](std::string_view key) -> const Json* {
            const Json* found = schema.find(key);
            return found && found->is_number() ? found : nullptr;
        };
        if (const Json* min = bound("minimum"); min && number < min->as_double())
            return fail(named(path) + " is at least " + bound_text(*min) + ", not " + value.dump());
        if (const Json* max = bound("maximum"); max && number > max->as_double())
            return fail(named(path) + " is at most " + bound_text(*max) + ", not " + value.dump());
        if (const Json* min = bound("exclusiveMinimum"); min && number <= min->as_double())
            return fail(named(path) + " is more than " + bound_text(*min) + ", not " + value.dump());
        if (const Json* max = bound("exclusiveMaximum"); max && number >= max->as_double())
            return fail(named(path) + " is less than " + bound_text(*max) + ", not " + value.dump());
    }
    if (value.is_string()) {
        std::size_t chars = characters(value.as_string());
        if (auto min = count_of(schema, "minLength"); min && chars < *min)
            return fail(named(path) + " is at least " + counted(*min, "character"));
        if (auto max = count_of(schema, "maxLength"); max && chars > *max)
            return fail(named(path) + " is at most " + counted(*max, "character"));
    }
    if (value.is_array()) {
        std::size_t len = value.size();
        if (auto min = count_of(schema, "minItems"); min && len < *min)
            return fail(named(path) + " holds at least " + counted(*min, "item"));
        if (auto max = count_of(schema, "maxItems"); max && len > *max)
            return fail(named(path) + " holds at most " + counted(*max, "item"));
        if (const Json* items = schema.find("items")) {
            std::string base = path.empty() ? "params" : path;
            for (std::size_t n = 0; n < len; ++n)
                FFRWD_TRY(check(value[n], *items, base + "[" + std::to_string(n) + "]"));
        }
    }
    if (value.is_object()) {
        const Json* properties = schema.find("properties");
        if (properties && !properties->is_object()) properties = nullptr;
        if (const Json* required = schema.find("required"); required && required->is_array()) {
            for (const Json& name : required->as_array())
                if (name.is_string() && !value.contains(name.as_string()))
                    return fail(named(join(path, name.as_string())) + " is required");
        }
        const Json* additional = schema.find("additionalProperties");
        bool closed = additional && additional->is_bool() && !additional->as_bool();
        for (auto& [name, field] : value.as_object()) {
            const Json* property = properties ? properties->find(name) : nullptr;
            if (property) {
                FFRWD_TRY(check(field, *property, join(path, name)));
            } else if (closed) {
                std::string takes = "takes none";
                if (properties && properties->size() > 0) {
                    takes = "takes ";
                    bool first = true;
                    for (const auto& [key, unused] : properties->as_object()) {
                        takes += (first ? "`" : ", `") + key + "`";
                        first = false;
                    }
                }
                return fail(named(join(path, name)) + " is not a param here; " +
                            (path.empty() ? "the node " + takes : named(path) + " " + takes));
            }
        }
    }
    return {};
}

void fill_defaults(Json& value, const Json& schema) {
    if (!value.is_object()) return;
    const Json* properties = schema.find("properties");
    if (!properties || !properties->is_object()) return;
    for (const auto& [name, property] : properties->as_object()) {
        const Json* fallback = property.find("default");
        if (fallback && !value.contains(name)) value.set(name, *fallback);
    }
}

}  // namespace

Result<Json> check_params(std::string_view schema_text, std::string_view params) {
    auto schema = Json::parse(schema_text);
    if (!schema) return fail("the params schema is not JSON: " + schema.error().message);
    std::string_view text = params;
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                             text.front() == '\r'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r'))
        text.remove_suffix(1);
    Json value = Json::object();
    if (!text.empty()) {
        auto parsed = Json::parse(text);
        if (!parsed) return fail("the params are not JSON: " + parsed.error().message);
        value = std::move(*parsed);
    }
    if (!value.is_object()) return fail("the params are a JSON object, not " + std::string(text));
    auto& members = value.as_object();
    std::erase_if(members, [](const auto& member) { return member.second.is_null(); });
    FFRWD_TRY(check(value, *schema, ""));
    fill_defaults(value, *schema);
    return value;
}

}  // namespace ffrwd
