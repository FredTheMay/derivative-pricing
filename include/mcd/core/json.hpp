#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcd::json {

// A minimal, scope-limited JSON value: flat objects only (no nested
// objects/arrays), numbers (double), strings, and bools. This is not a
// general-purpose JSON library -- it exists to serialize/deserialize
// mcd_cli's flat request/response shapes
// (docs/design/06-cli-bindings-reporting.md sec.3.1) and nothing else.
// Hand-written per CLAUDE.md sec.5: no JSON library is on the approved
// dependency list, and every other numerical/parsing primitive in this
// project (Philox, the inverse normal CDF, Householder QR) is hand-written
// too, scoped to exactly what it needs to do.
class Value {
  public:
    enum class Type : std::uint8_t { Null, Bool, Number, String };

    Value() noexcept = default;
    [[nodiscard]] static Value from_bool(bool b) noexcept;
    [[nodiscard]] static Value from_number(double d) noexcept;
    [[nodiscard]] static Value from_string(std::string s) noexcept;

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] const std::string& as_string() const;

  private:
    Type type_ = Type::Null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
};

// Insertion-ordered (not a map) so serialize() output has a stable,
// predictable field order matching whatever order the caller built it in --
// purely cosmetic (JSON object key order carries no semantic meaning), but
// makes CLI output easier for a human or a test to read.
using Object = std::vector<std::pair<std::string, Value>>;

[[nodiscard]] const Value* find(const Object& obj, std::string_view key) noexcept;

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Parses a single flat JSON object, e.g. {"a": 1, "b": "x", "c": true}.
// Throws ParseError with a human-readable message on anything outside that
// scope (nested objects/arrays, \u escapes, malformed syntax) -- callers
// (mcd_cli) catch this and emit a clean JSON error response, never a crash.
[[nodiscard]] Object parse_object(std::string_view text);

// Serializes a flat object back to compact, single-line JSON text.
[[nodiscard]] std::string serialize(const Object& obj);

} // namespace mcd::json
