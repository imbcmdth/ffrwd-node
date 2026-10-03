#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "result.hpp"

namespace ffrwd {

/// A JSON value. Whole numbers and fractions are kept apart, as a schema's
/// `integer` and `number` are, and an object keeps its keys in the order
/// they were written.
class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::vector<std::pair<std::string, Json>>;

    enum class Type { Null, Bool, Int, Float, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool value) : value_(value) {}
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    Json(T value) : value_(static_cast<std::int64_t>(value)) {}
    Json(double value) : value_(value) {}
    Json(float value) : value_(static_cast<double>(value)) {}
    Json(const char* value) : value_(std::string(value)) {}
    Json(std::string value) : value_(std::move(value)) {}
    Json(std::string_view value) : value_(std::string(value)) {}
    Json(Array value) : value_(std::move(value)) {}
    Json(Object value) : value_(std::move(value)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    /// `text` read as one JSON value, nothing after it but white space.
    static Result<Json> parse(std::string_view text);

    /// The value written compactly, as serde_json writes it.
    std::string dump() const;

    Type type() const { return static_cast<Type>(value_.index()); }
    bool is_null() const { return type() == Type::Null; }
    bool is_bool() const { return type() == Type::Bool; }
    bool is_int() const { return type() == Type::Int; }
    bool is_float() const { return type() == Type::Float; }
    bool is_number() const { return is_int() || is_float(); }
    bool is_string() const { return type() == Type::String; }
    bool is_array() const { return type() == Type::Array; }
    bool is_object() const { return type() == Type::Object; }

    bool as_bool() const { return std::get<bool>(value_); }
    std::int64_t as_int() const { return std::get<std::int64_t>(value_); }
    /// A number of either kind, as a double.
    double as_double() const {
        return is_int() ? static_cast<double>(as_int()) : std::get<double>(value_);
    }
    const std::string& as_string() const { return std::get<std::string>(value_); }
    const Array& as_array() const { return std::get<Array>(value_); }
    Array& as_array() { return std::get<Array>(value_); }
    const Object& as_object() const { return std::get<Object>(value_); }
    Object& as_object() { return std::get<Object>(value_); }

    /// Member `key` of an object, or none.
    const Json* find(std::string_view key) const;
    Json* find(std::string_view key);
    bool contains(std::string_view key) const { return find(key) != nullptr; }

    /// Member `key` of an object; null when it has none.
    const Json& operator[](std::string_view key) const;
    /// Member `key` of an object, added as null when it has none. A null
    /// value becomes an object first.
    Json& operator[](std::string_view key);
    const Json& operator[](std::size_t at) const { return as_array()[at]; }
    Json& operator[](std::size_t at) { return as_array()[at]; }

    /// Sets member `key`: in place when it is there, at the end when not.
    void set(std::string_view key, Json value);
    bool erase(std::string_view key);
    void push_back(Json value) { as_array().push_back(std::move(value)); }
    std::size_t size() const;

    /// Equal as serde_json compares: objects whatever their key order, and a
    /// whole number never equal to a fraction.
    friend bool operator==(const Json& a, const Json& b);

private:
    std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object> value_;
};

/// A double as serde_json writes it: the shortest text that reads back the
/// same, a whole one with `.0`.
std::string format_double(double value);

}  // namespace ffrwd
