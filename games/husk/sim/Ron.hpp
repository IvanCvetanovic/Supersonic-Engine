#pragma once

// A reader for the RON that HUSK's data is written in (ron 0.12 via serde).
//
// It covers what serde's RON derive produces and the 58 shipped files use:
// anonymous and named structs, tuples, lists, maps, strings, numbers,
// booleans, unit/newtype/tuple/struct enum variants, Some/None, comments, and
// trailing commas. The parse builds a tree and the typed accessors below turn
// it into values, failing with the path of the field that was wrong, as serde
// does.
//
// Numbers keep their literal text until something asks for a type, because the
// type decides the conversion. A u32 field that says "2.0" is an error, and an
// f32 field that says "100" is 100.0. Floats convert with std::from_chars,
// which is correctly rounded and ignores the locale, exactly as Rust's
// f32::from_str is and does. strtof and streams do neither reliably.

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace husk::ron {

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Value {
    enum class Kind : uint8_t { Number, String, Bool, Ident, List, Map, Struct, Tuple, Unit };

    Kind kind = Kind::Unit;

    // Number: the literal. String: the decoded contents. Ident: the name.
    // Struct and Tuple: the name, empty when anonymous (`Some(..)` is a Tuple
    // named "Some").
    std::string text;
    bool boolean = false;

    // List and Tuple elements; Struct field values; Map values.
    std::vector<Value> items;
    // Struct field names, parallel to items.
    std::vector<std::string> names;
    // Map keys, parallel to items.
    std::vector<Value> keys;

    // A struct's field, or nullptr when it is absent (or this is no struct).
    const Value* field(std::string_view name) const;
};

Value parse(std::string_view text, std::string_view source = "<string>");
Value parseFile(const std::filesystem::path& path);

// ---- typed access ------------------------------------------------------------
// Every accessor takes the path of the value for its error message.

float asF32(const Value& v, std::string_view path);
uint64_t asUInt(const Value& v, uint64_t max, std::string_view path);
int64_t asInt(const Value& v, std::string_view path);
bool asBool(const Value& v, std::string_view path);
const std::string& asString(const Value& v, std::string_view path);

inline uint32_t asU32(const Value& v, std::string_view path) {
    return static_cast<uint32_t>(asUInt(v, UINT32_MAX, path));
}
inline uint16_t asU16(const Value& v, std::string_view path) {
    return static_cast<uint16_t>(asUInt(v, UINT16_MAX, path));
}
inline uint8_t asU8(const Value& v, std::string_view path) {
    return static_cast<uint8_t>(asUInt(v, UINT8_MAX, path));
}

// A struct, anonymous or named. The name is not checked, as serde's RON does
// not check it either.
const Value& asStruct(const Value& v, std::string_view path);
const std::vector<Value>& asList(const Value& v, std::string_view path);
// An anonymous tuple of exactly `arity` elements.
const std::vector<Value>& asTuple(const Value& v, size_t arity, std::string_view path);
// Some(x) gives &x, None gives nullptr.
const Value* asOption(const Value& v, std::string_view path);

// A required struct field. Absent is an error, as for a serde field without a
// default.
const Value& require(const Value& s, std::string_view name, std::string_view path);

// An enum variant: its name, and its payload when it has one (nullptr for a
// unit variant). Newtype and tuple variants are Tuples, struct variants
// Structs.
struct Variant {
    std::string_view name;
    const Value* payload = nullptr;
};
Variant asVariant(const Value& v, std::string_view path);

std::string join(std::string_view path, std::string_view field);

} // namespace husk::ron
