#include "mcd/core/json.hpp"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace mcd::json {

Value Value::from_bool(bool b) noexcept {
    Value v;
    v.type_ = Type::Bool;
    v.bool_ = b;
    return v;
}

Value Value::from_number(double d) noexcept {
    Value v;
    v.type_ = Type::Number;
    v.number_ = d;
    return v;
}

Value Value::from_string(std::string s) noexcept {
    Value v;
    v.type_ = Type::String;
    v.string_ = std::move(s);
    return v;
}

bool Value::as_bool() const {
    if (type_ != Type::Bool) {
        throw ParseError("expected a bool value");
    }
    return bool_;
}

double Value::as_number() const {
    if (type_ != Type::Number) {
        throw ParseError("expected a number value");
    }
    return number_;
}

const std::string& Value::as_string() const {
    if (type_ != Type::String) {
        throw ParseError("expected a string value");
    }
    return string_;
}

const Value* find(const Object& obj, std::string_view key) noexcept {
    for (const auto& [k, v] : obj) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

namespace {

// Minimal recursive-descent-style cursor over the input text. Only ever
// parses the single "flat object" grammar this file supports -- see
// mcd::json::Value's class comment for the deliberate scope limits.
class Cursor {
  public:
    explicit Cursor(std::string_view text) : text_(text) {}

    Object parse_top_level_object() {
        skip_whitespace();
        Object obj = parse_object();
        skip_whitespace();
        if (pos_ != text_.size()) {
            throw ParseError("trailing content after top-level object");
        }
        return obj;
    }

  private:
    std::string_view text_;
    std::size_t pos_ = 0;

    [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
    [[nodiscard]] char peek() const {
        if (at_end()) {
            throw ParseError("unexpected end of input");
        }
        return text_[pos_];
    }
    char advance() {
        const char c = peek();
        ++pos_;
        return c;
    }
    void expect(char c) {
        if (advance() != c) {
            throw ParseError(std::string("expected '") + c + "'");
        }
    }
    void skip_whitespace() {
        while (!at_end() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' ||
                text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    Object parse_object() {
        expect('{');
        Object obj;
        skip_whitespace();
        if (!at_end() && peek() == '}') {
            advance();
            return obj;
        }
        while (true) {
            skip_whitespace();
            std::string key = parse_string();
            skip_whitespace();
            expect(':');
            skip_whitespace();
            Value value = parse_value();
            obj.emplace_back(std::move(key), std::move(value));
            skip_whitespace();
            const char c = advance();
            if (c == '}') {
                break;
            }
            if (c != ',') {
                throw ParseError("expected ',' or '}' in object");
            }
        }
        return obj;
    }

    Value parse_value() {
        skip_whitespace();
        const char c = peek();
        if (c == '"') {
            return Value::from_string(parse_string());
        }
        if (c == '{') {
            throw ParseError("nested objects are not supported by this minimal parser");
        }
        if (c == '[') {
            throw ParseError("arrays are not supported by this minimal parser");
        }
        if (c == 't' || c == 'f') {
            return Value::from_bool(parse_bool());
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return Value::from_number(parse_number());
        }
        throw ParseError("unexpected character in value");
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            const char c = advance();
            if (c == '"') {
                break;
            }
            if (c == '\\') {
                const char esc = advance();
                switch (esc) {
                    case '"':
                        out.push_back('"');
                        break;
                    case '\\':
                        out.push_back('\\');
                        break;
                    case '/':
                        out.push_back('/');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    default:
                        throw ParseError(
                            "unsupported escape sequence (this minimal parser does not "
                            "support \\u escapes)");
                }
                continue;
            }
            out.push_back(c);
        }
        return out;
    }

    bool parse_bool() {
        if (text_.substr(pos_, 4) == "true") {
            pos_ += 4;
            return true;
        }
        if (text_.substr(pos_, 5) == "false") {
            pos_ += 5;
            return false;
        }
        throw ParseError("expected 'true' or 'false'");
    }

    double parse_number() {
        const std::size_t start = pos_;
        if (!at_end() && peek() == '-') {
            advance();
        }
        while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) != 0)) {
            advance();
        }
        if (!at_end() && peek() == '.') {
            advance();
            while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) != 0)) {
                advance();
            }
        }
        if (!at_end() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!at_end() && (peek() == '+' || peek() == '-')) {
                advance();
            }
            while (!at_end() && (std::isdigit(static_cast<unsigned char>(peek())) != 0)) {
                advance();
            }
        }
        const std::string token(text_.substr(start, pos_ - start));
        char* endptr = nullptr;
        const double value = std::strtod(token.c_str(), &endptr);
        if (endptr == token.c_str() || token.empty()) {
            throw ParseError("malformed number literal");
        }
        return value;
    }
};

void append_escaped_string(std::string& out, const std::string& s) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\r':
                out += "\\r";
                break;
            default:
                out.push_back(c);
        }
    }
    out.push_back('"');
}

void append_number(std::string& out, double d) {
    // Display precision, not a round-trip-exact guarantee -- this is a compact,
    // human-readable CLI/report format, not a numerical interchange format for
    // full double precision. See docs/design/06-cli-bindings-reporting.md sec.2.1.
    std::array<char, 64> buf{};
    const int n = std::snprintf(buf.data(), buf.size(), "%.10g", d);
    out.append(buf.data(), static_cast<std::size_t>(n));
}

} // namespace

Object parse_object(std::string_view text) {
    Cursor cursor(text);
    return cursor.parse_top_level_object();
}

std::string serialize(const Object& obj) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : obj) {
        if (!first) {
            out.push_back(',');
        }
        first = false;
        append_escaped_string(out, key);
        out.push_back(':');
        switch (value.type()) {
            case Value::Type::Null:
                out += "null";
                break;
            case Value::Type::Bool:
                out += value.as_bool() ? "true" : "false";
                break;
            case Value::Type::Number:
                append_number(out, value.as_number());
                break;
            case Value::Type::String:
                append_escaped_string(out, value.as_string());
                break;
        }
    }
    out.push_back('}');
    return out;
}

} // namespace mcd::json
