#pragma once

#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "json.hpp"
#include "result.hpp"

#define FFRWD_PARENS ()
#define FFRWD_EXPAND(...) FFRWD_EXPAND4(FFRWD_EXPAND4(FFRWD_EXPAND4(FFRWD_EXPAND4(__VA_ARGS__))))
#define FFRWD_EXPAND4(...) FFRWD_EXPAND3(FFRWD_EXPAND3(FFRWD_EXPAND3(FFRWD_EXPAND3(__VA_ARGS__))))
#define FFRWD_EXPAND3(...) FFRWD_EXPAND2(FFRWD_EXPAND2(FFRWD_EXPAND2(FFRWD_EXPAND2(__VA_ARGS__))))
#define FFRWD_EXPAND2(...) FFRWD_EXPAND1(FFRWD_EXPAND1(FFRWD_EXPAND1(FFRWD_EXPAND1(__VA_ARGS__))))
#define FFRWD_EXPAND1(...) __VA_ARGS__
#define FFRWD_FOR_EACH(macro, ...) \
    __VA_OPT__(FFRWD_EXPAND(FFRWD_FOR_EACH_HELPER(macro, __VA_ARGS__)))
#define FFRWD_FOR_EACH_HELPER(macro, first, ...) \
    macro(first) __VA_OPT__(FFRWD_FOR_EACH_AGAIN FFRWD_PARENS(macro, __VA_ARGS__))
#define FFRWD_FOR_EACH_AGAIN() FFRWD_FOR_EACH_HELPER
#define FFRWD_VISIT_FIELD(field) visit(#field, self.field);

/// Names a struct's fields for JSON, inside the struct: what a row type, a
/// params type or a schema is read from and written as, in the order given.
/// A field that is itself flattened (`Span`) writes its own fields in its
/// place.
#define FFRWD_FIELDS(...)                                              \
    template <class FfrwdSelf, class FfrwdVisit>                       \
    static void ffrwd_visit([[maybe_unused]] FfrwdSelf& self,          \
                            [[maybe_unused]] FfrwdVisit&& visit) {     \
        FFRWD_FOR_EACH(FFRWD_VISIT_FIELD, __VA_ARGS__)                 \
    }

namespace ffrwd {

namespace detail {
struct NoVisit {
    template <class T>
    void operator()(const char*, T&) const {}
};
}  // namespace detail

/// A type whose fields `FFRWD_FIELDS` names.
template <class T>
concept Fields = requires(T& value) { T::ffrwd_visit(value, detail::NoVisit{}); };

/// A type whose fields stand in its parent's place when it is a field.
template <class T>
concept Flattened = Fields<T> && requires { requires bool(T::ffrwd_flatten); };

template <class T>
struct IsOptional : std::false_type {};
template <class T>
struct IsOptional<std::optional<T>> : std::true_type {};
template <class T>
struct IsVector : std::false_type {};
template <class T, class A>
struct IsVector<std::vector<T, A>> : std::true_type {};
template <class T>
struct IsStringMap : std::false_type {};
template <class T, class C, class A>
struct IsStringMap<std::map<std::string, T, C, A>> : std::true_type {};
template <class T>
struct IsArray : std::false_type {};
template <class T, std::size_t N>
struct IsArray<std::array<T, N>> : std::true_type {};

/// `value` as JSON: numbers, strings, booleans, `std::optional` (null when
/// empty), `std::vector`, `std::array`, `std::map` keyed by string, `Json`
/// and structs naming their fields.
template <class T>
Json to_json(const T& value);

/// `json` read into `value`; the error names what did not read.
template <class T>
Status from_json(const Json& json, T& value);

namespace detail {

inline std::string kind_of(const Json& json) {
    switch (json.type()) {
        case Json::Type::Null: return "null";
        case Json::Type::Bool: return "a boolean";
        case Json::Type::Int:
        case Json::Type::Float: return "a number";
        case Json::Type::String: return "a string";
        case Json::Type::Array: return "an array";
        case Json::Type::Object: return "an object";
    }
    return "a value";
}

inline std::string field_name(std::string_view path) {
    return path.empty() ? std::string("the row") : "`" + std::string(path) + "`";
}

inline std::string join(std::string_view path, std::string_view name) {
    return path.empty() ? std::string(name) : std::string(path) + "." + std::string(name);
}

template <class T>
Status read(const Json& json, T& value, std::string_view path);

template <class T>
void write_fields(Json& object, const T& value) {
    T::ffrwd_visit(value, [&](const char* name, const auto& field) {
        using F = std::remove_cvref_t<decltype(field)>;
        if constexpr (Flattened<F>) {
            write_fields(object, field);
        } else {
            object.as_object().emplace_back(name, to_json(field));
        }
    });
}

template <class T>
Status read_fields(const Json& json, T& value, std::string_view path) {
    Status status;
    T::ffrwd_visit(value, [&](const char* name, auto& field) {
        if (!status) return;
        using F = std::remove_cvref_t<decltype(field)>;
        if constexpr (Flattened<F>) {
            status = read_fields(json, field, path);
        } else {
            const Json* member = json.find(name);
            if (member == nullptr) {
                if constexpr (IsOptional<F>::value) {
                    field.reset();
                } else {
                    status = fail("missing field `" + join(path, name) + "`");
                }
                return;
            }
            status = read(*member, field, join(path, name));
        }
    });
    return status;
}

template <class T>
Status read(const Json& json, T& value, std::string_view path) {
    auto refuse = [&](std::string_view wanted) -> Status {
        return fail(field_name(path) + " is " + std::string(wanted) + ", not " + json.dump());
    };
    if constexpr (std::is_same_v<T, Json>) {
        value = json;
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!json.is_bool()) return refuse("a boolean");
        value = json.as_bool();
    } else if constexpr (std::is_integral_v<T>) {
        if (json.is_int()) {
            std::int64_t whole = json.as_int();
            if constexpr (std::is_unsigned_v<T>) {
                if (whole < 0 || std::uint64_t(whole) > std::numeric_limits<T>::max())
                    return refuse("a whole number in range");
            } else {
                if (whole < std::numeric_limits<T>::min() || whole > std::numeric_limits<T>::max())
                    return refuse("a whole number in range");
            }
            value = static_cast<T>(whole);
        } else if (json.is_float() && std::trunc(json.as_double()) == json.as_double() &&
                   json.as_double() >= double(std::numeric_limits<T>::min()) &&
                   json.as_double() <= double(std::numeric_limits<T>::max())) {
            value = static_cast<T>(json.as_double());
        } else {
            return refuse("a whole number");
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!json.is_number()) return refuse("a number");
        value = static_cast<T>(json.as_double());
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!json.is_string()) return refuse("a string");
        value = json.as_string();
    } else if constexpr (IsOptional<T>::value) {
        if (json.is_null()) {
            value.reset();
        } else {
            typename T::value_type inner{};
            FFRWD_TRY(read(json, inner, path));
            value = std::move(inner);
        }
    } else if constexpr (IsVector<T>::value) {
        if (!json.is_array()) return refuse("an array");
        value.clear();
        std::size_t n = 0;
        for (const Json& item : json.as_array()) {
            typename T::value_type inner{};
            FFRWD_TRY(read(item, inner, std::string(path) + "[" + std::to_string(n++) + "]"));
            value.push_back(std::move(inner));
        }
    } else if constexpr (IsArray<T>::value) {
        if (!json.is_array() || json.size() != value.size())
            return refuse("an array of " + std::to_string(value.size()));
        for (std::size_t n = 0; n < value.size(); ++n)
            FFRWD_TRY(read(json[n], value[n], std::string(path) + "[" + std::to_string(n) + "]"));
    } else if constexpr (IsStringMap<T>::value) {
        if (!json.is_object()) return refuse("an object");
        value.clear();
        for (const auto& [key, member] : json.as_object()) {
            typename T::mapped_type inner{};
            FFRWD_TRY(read(member, inner, join(path, key)));
            value.emplace(key, std::move(inner));
        }
    } else if constexpr (Fields<T>) {
        if (!json.is_object()) return refuse("an object");
        return read_fields(json, value, path);
    } else {
        static_assert(sizeof(T) == 0, "this type has no JSON form: name its fields with FFRWD_FIELDS");
    }
    return {};
}

}  // namespace detail

template <class T>
Json to_json(const T& value) {
    if constexpr (std::is_same_v<T, Json>) {
        return value;
    } else if constexpr (std::is_same_v<T, bool> || std::is_arithmetic_v<T>) {
        return Json(value);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        return Json(std::string_view(value));
    } else if constexpr (IsOptional<T>::value) {
        return value ? to_json(*value) : Json();
    } else if constexpr (IsVector<T>::value || IsArray<T>::value) {
        Json array = Json::array();
        for (const auto& item : value) array.push_back(to_json(item));
        return array;
    } else if constexpr (IsStringMap<T>::value) {
        Json object = Json::object();
        for (const auto& [key, item] : value) object.as_object().emplace_back(key, to_json(item));
        return object;
    } else if constexpr (Fields<T>) {
        Json object = Json::object();
        detail::write_fields(object, value);
        return object;
    } else {
        static_assert(sizeof(T) == 0, "this type has no JSON form: name its fields with FFRWD_FIELDS");
    }
}

template <class T>
Status from_json(const Json& json, T& value) {
    return detail::read(json, value, "");
}

/// One JSON row read as a `T`.
template <class T>
Result<T> parse(std::string_view row) {
    auto json = Json::parse(row);
    if (!json) return fail("the row " + std::string(row) + " does not read: " + json.error().message);
    T value{};
    auto read = from_json(*json, value);
    if (!read) return fail("the row " + std::string(row) + " does not read: " + read.error().message);
    return value;
}

/// The JSON schema of a JSON value's shape: a fraction is `number`, a whole
/// number `integer`, then `string`, `boolean`, `array` and nested `object`s,
/// each member required; null takes any type and is not required. Keys
/// sorted, as serde_json writes a map.
Json schema_for(const Json& value);

/// The JSON schema of a row type, from what `T{}` writes. A field that is an
/// empty `std::optional` takes any type and is not required. Other fields
/// are allowed, as a reader naming only the fields it reads wants.
template <class T>
std::string schema_of() {
    return schema_for(to_json(T{})).dump();
}

}  // namespace ffrwd
