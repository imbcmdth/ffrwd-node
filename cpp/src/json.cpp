#include "ffrwd/json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ffrwd {

namespace {

const Json null_json;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    Result<Json> document() {
        skip();
        Json value;
        if (!read(value, 0)) return fail(error_);
        skip();
        if (at_ != text_.size()) return fail(where("trailing characters"));
        return value;
    }

private:
    std::string_view text_;
    std::size_t at_ = 0;
    std::string error_;

    std::string where(std::string_view what) const {
        std::size_t line = 1, column = 1;
        for (std::size_t n = 0; n < at_ && n < text_.size(); ++n) {
            if (text_[n] == '\n') {
                ++line;
                column = 1;
            } else {
                ++column;
            }
        }
        return std::string(what) + " at line " + std::to_string(line) + " column " +
               std::to_string(column);
    }

    bool fault(std::string_view what) {
        error_ = where(what);
        return false;
    }

    void skip() {
        while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' ||
                                      text_[at_] == '\n' || text_[at_] == '\r'))
            ++at_;
    }

    bool literal(std::string_view word) {
        if (text_.substr(at_, word.size()) != word) return fault("expected value");
        at_ += word.size();
        return true;
    }

    bool read(Json& out, int depth) {
        if (depth > 128) return fault("recursion limit exceeded");
        if (at_ >= text_.size()) return fault("EOF while parsing a value");
        char c = text_[at_];
        switch (c) {
            case 'n':
                out = Json();
                return literal("null");
            case 't':
                out = Json(true);
                return literal("true");
            case 'f':
                out = Json(false);
                return literal("false");
            case '"': {
                std::string text;
                if (!string(text)) return false;
                out = Json(std::move(text));
                return true;
            }
            case '[':
                return array(out, depth);
            case '{':
                return object(out, depth);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return number(out);
                return fault("expected value");
        }
    }

    bool array(Json& out, int depth) {
        ++at_;
        Json::Array items;
        skip();
        if (at_ < text_.size() && text_[at_] == ']') {
            ++at_;
            out = Json(std::move(items));
            return true;
        }
        for (;;) {
            skip();
            Json item;
            if (!read(item, depth + 1)) return false;
            items.push_back(std::move(item));
            skip();
            if (at_ >= text_.size()) return fault("EOF while parsing a list");
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == ']') {
                ++at_;
                out = Json(std::move(items));
                return true;
            }
            return fault("expected `,` or `]`");
        }
    }

    bool object(Json& out, int depth) {
        ++at_;
        Json::Object members;
        skip();
        if (at_ < text_.size() && text_[at_] == '}') {
            ++at_;
            out = Json(std::move(members));
            return true;
        }
        for (;;) {
            skip();
            if (at_ >= text_.size()) return fault("EOF while parsing an object");
            if (text_[at_] != '"') return fault("key must be a string");
            std::string key;
            if (!string(key)) return false;
            skip();
            if (at_ >= text_.size() || text_[at_] != ':') return fault("expected `:`");
            ++at_;
            skip();
            Json value;
            if (!read(value, depth + 1)) return false;
            auto found = std::find_if(members.begin(), members.end(),
                                      [&](const auto& member) { return member.first == key; });
            if (found != members.end()) {
                found->second = std::move(value);
            } else {
                members.emplace_back(std::move(key), std::move(value));
            }
            skip();
            if (at_ >= text_.size()) return fault("EOF while parsing an object");
            if (text_[at_] == ',') {
                ++at_;
                continue;
            }
            if (text_[at_] == '}') {
                ++at_;
                out = Json(std::move(members));
                return true;
            }
            return fault("expected `,` or `}`");
        }
    }

    bool hex4(unsigned& code) {
        if (at_ + 4 > text_.size()) return fault("EOF while parsing a string");
        code = 0;
        for (int n = 0; n < 4; ++n) {
            char c = text_[at_++];
            code <<= 4;
            if (c >= '0' && c <= '9') {
                code |= unsigned(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                code |= unsigned(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                code |= unsigned(c - 'A' + 10);
            } else {
                return fault("invalid escape");
            }
        }
        return true;
    }

    static void utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += char(code);
        } else if (code < 0x800) {
            out += char(0xC0 | (code >> 6));
            out += char(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
            out += char(0xE0 | (code >> 12));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        } else {
            out += char(0xF0 | (code >> 18));
            out += char(0x80 | ((code >> 12) & 0x3F));
            out += char(0x80 | ((code >> 6) & 0x3F));
            out += char(0x80 | (code & 0x3F));
        }
    }

    bool string(std::string& out) {
        ++at_;
        for (;;) {
            if (at_ >= text_.size()) return fault("EOF while parsing a string");
            char c = text_[at_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fault("control character while parsing a string");
            if (c != '\\') {
                out += c;
                continue;
            }
            if (at_ >= text_.size()) return fault("EOF while parsing a string");
            char e = text_[at_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned code;
                    if (!hex4(code)) return false;
                    if (code >= 0xD800 && code < 0xDC00) {
                        unsigned low;
                        if (text_.substr(at_, 2) != "\\u") return fault("lone leading surrogate");
                        at_ += 2;
                        if (!hex4(low)) return false;
                        if (low < 0xDC00 || low >= 0xE000) return fault("lone leading surrogate");
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    } else if (code >= 0xDC00 && code < 0xE000) {
                        return fault("lone trailing surrogate");
                    }
                    utf8(out, code);
                    break;
                }
                default:
                    return fault("invalid escape");
            }
        }
    }

    bool number(Json& out) {
        std::size_t start = at_;
        bool whole = true;
        if (text_[at_] == '-') ++at_;
        auto digits = [&] {
            std::size_t from = at_;
            while (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') ++at_;
            return at_ > from;
        };
        if (at_ < text_.size() && text_[at_] == '0') {
            ++at_;
        } else if (!digits()) {
            return fault("invalid number");
        }
        if (at_ < text_.size() && text_[at_] == '.') {
            whole = false;
            ++at_;
            if (!digits()) return fault("invalid number");
        }
        if (at_ < text_.size() && (text_[at_] == 'e' || text_[at_] == 'E')) {
            whole = false;
            ++at_;
            if (at_ < text_.size() && (text_[at_] == '+' || text_[at_] == '-')) ++at_;
            if (!digits()) return fault("invalid number");
        }
        std::string_view text = text_.substr(start, at_ - start);
        if (whole) {
            std::int64_t value = 0;
            auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec == std::errc() && end == text.data() + text.size()) {
                out = Json(value);
                return true;
            }
        }
        std::string copy(text);
        out = Json(std::strtod(copy.c_str(), nullptr));
        return true;
    }
};

void escape(std::string& out, const std::string& text) {
    static const char hex[] = "0123456789abcdef";
    out += '"';
    for (char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void write(std::string& out, const Json& value) {
    switch (value.type()) {
        case Json::Type::Null: out += "null"; break;
        case Json::Type::Bool: out += value.as_bool() ? "true" : "false"; break;
        case Json::Type::Int: out += std::to_string(value.as_int()); break;
        case Json::Type::Float: out += format_double(value.as_double()); break;
        case Json::Type::String: escape(out, value.as_string()); break;
        case Json::Type::Array: {
            out += '[';
            bool first = true;
            for (const Json& item : value.as_array()) {
                if (!first) out += ',';
                first = false;
                write(out, item);
            }
            out += ']';
            break;
        }
        case Json::Type::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, member] : value.as_object()) {
                if (!first) out += ',';
                first = false;
                escape(out, key);
                out += ':';
                write(out, member);
            }
            out += '}';
            break;
        }
    }
}

}  // namespace

std::string format_double(double value) {
    if (!std::isfinite(value)) return "null";
    // The shortest digits that read back as `value`, by asking printf for
    // more until they do: std::to_chars would do it in one, and bring
    // 125 KB of tables into every module.
    char buffer[64];
    int length = 0;
    for (int precision = 0; precision <= 16; ++precision) {
        length = std::snprintf(buffer, sizeof buffer, "%.*e", precision, value);
        if (std::strtod(buffer, nullptr) == value) break;
    }
    std::string_view text(buffer, std::size_t(length));
    bool negative = !text.empty() && text[0] == '-';
    if (negative) text.remove_prefix(1);
    std::size_t e = text.find('e');
    std::string digits;
    for (char c : text.substr(0, e))
        if (c != '.') digits += c;
    int exponent = std::atoi(std::string(text.substr(e + 1)).c_str());
    while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
    length = int(digits.size());
    int point = exponent + 1;
    int k = point - length;
    std::string out = negative ? "-" : "";
    if (k >= 0 && point <= 16) {
        out += digits;
        out.append(std::size_t(k), '0');
        out += ".0";
    } else if (point > 0 && point <= 16) {
        out += digits.substr(0, std::size_t(point));
        out += '.';
        out += digits.substr(std::size_t(point));
    } else if (point > -5 && point <= 0) {
        out += "0.";
        out.append(std::size_t(-point), '0');
        out += digits;
    } else {
        out += digits[0];
        if (length > 1) {
            out += '.';
            out += digits.substr(1);
        }
        out += 'e';
        out += std::to_string(point - 1);
    }
    return out;
}

Result<Json> Json::parse(std::string_view text) { return Parser(text).document(); }

std::string Json::dump() const {
    std::string out;
    write(out, *this);
    return out;
}

const Json* Json::find(std::string_view key) const {
    if (!is_object()) return nullptr;
    for (const auto& [name, value] : as_object())
        if (name == key) return &value;
    return nullptr;
}

Json* Json::find(std::string_view key) {
    if (!is_object()) return nullptr;
    for (auto& [name, value] : as_object())
        if (name == key) return &value;
    return nullptr;
}

const Json& Json::operator[](std::string_view key) const {
    const Json* found = find(key);
    return found ? *found : null_json;
}

Json& Json::operator[](std::string_view key) {
    if (is_null()) value_ = Object{};
    if (Json* found = find(key)) return *found;
    as_object().emplace_back(std::string(key), Json());
    return as_object().back().second;
}

void Json::set(std::string_view key, Json value) { (*this)[key] = std::move(value); }

bool Json::erase(std::string_view key) {
    if (!is_object()) return false;
    auto& members = as_object();
    auto found = std::find_if(members.begin(), members.end(),
                              [&](const auto& member) { return member.first == key; });
    if (found == members.end()) return false;
    members.erase(found);
    return true;
}

std::size_t Json::size() const {
    if (is_array()) return as_array().size();
    if (is_object()) return as_object().size();
    return 0;
}

bool operator==(const Json& a, const Json& b) {
    if (a.type() != b.type()) return false;
    switch (a.type()) {
        case Json::Type::Null: return true;
        case Json::Type::Bool: return a.as_bool() == b.as_bool();
        case Json::Type::Int: return a.as_int() == b.as_int();
        case Json::Type::Float: return a.as_double() == b.as_double();
        case Json::Type::String: return a.as_string() == b.as_string();
        case Json::Type::Array: return a.as_array() == b.as_array();
        case Json::Type::Object: {
            if (a.size() != b.size()) return false;
            for (const auto& [key, value] : a.as_object()) {
                const Json* other = b.find(key);
                if (!other || !(*other == value)) return false;
            }
            return true;
        }
    }
    return false;
}

}  // namespace ffrwd
