#pragma once

// A small, self-contained JSON reader/writer.
//
// The scene and prefab serialisers emit JSON, so they need to be able to read
// it back. Deserialize previously slurped the file into a string it never
// looked at, wiped the registry, and fabricated three hardcoded entities while
// reporting success. No JSON library is vendored, and pulling one in for this
// format would be heavier than the ~200 lines below.

#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Supersonic::Json {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() = default;
    explicit Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    explicit Value(double n) : m_type(Type::Number), m_number(n) {}
    explicit Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {}
    explicit Value(Array a) : m_type(Type::Array), m_array(std::move(a)) {}
    explicit Value(Object o) : m_type(Type::Object), m_object(std::move(o)) {}

    Type GetType() const { return m_type; }
    bool IsObject() const { return m_type == Type::Object; }
    bool IsArray() const { return m_type == Type::Array; }
    bool IsNumber() const { return m_type == Type::Number; }
    bool IsString() const { return m_type == Type::String; }
    bool IsBool() const { return m_type == Type::Bool; }

    // Lookups return defaults instead of throwing, so a partially-written or
    // hand-edited scene file degrades field by field rather than all at once.
    bool Has(const std::string& key) const {
        return m_type == Type::Object && m_object.find(key) != m_object.end();
    }

    const Value& operator[](const std::string& key) const {
        static const Value kNull;
        if (m_type != Type::Object) return kNull;
        auto it = m_object.find(key);
        return it == m_object.end() ? kNull : it->second;
    }

    const Array& AsArray() const {
        static const Array kEmpty;
        return m_type == Type::Array ? m_array : kEmpty;
    }

    // Lets a caller enumerate members rather than only ask for ones it already
    // knows the name of, which is what makes "do these two writers emit the
    // same components?" answerable.
    const Object& AsObject() const {
        static const Object kEmpty;
        return m_type == Type::Object ? m_object : kEmpty;
    }

    double AsNumber(double fallback = 0.0) const {
        return m_type == Type::Number ? m_number : fallback;
    }

    float AsFloat(float fallback = 0.0f) const {
        return m_type == Type::Number ? static_cast<float>(m_number) : fallback;
    }

    std::string AsString(const std::string& fallback = {}) const {
        return m_type == Type::String ? m_string : fallback;
    }

    bool AsBool(bool fallback = false) const {
        return m_type == Type::Bool ? m_bool : fallback;
    }

    // The one mutator, and it exists for exactly one caller: a migration has to
    // be able to add the key a newer reader expects to a document written
    // before that key existed. Everything else here is built by the parser and
    // read.
    //
    // Turning a non-object into an object rather than refusing: a migration
    // running against a document that is the wrong shape has bigger problems
    // than this, and silently doing nothing is the failure mode that would be
    // discovered in whatever the migration was supposed to fix.
    void Set(const std::string& key, Value value) {
        if (m_type != Type::Object) {
            m_type = Type::Object;
            m_object.clear();
        }
        m_object.insert_or_assign(key, std::move(value));
    }

private:
    Type m_type{Type::Null};
    bool m_bool{false};
    double m_number{0.0};
    std::string m_string;
    Array m_array;
    Object m_object;
};

// Escapes a string for embedding in JSON. Entity tags are free-form user text;
// writing them raw produced unparseable files the moment someone used a quote,
// and allowed a crafted tag to inject document structure.
inline std::string Escape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (const char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    static const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[(c >> 4) & 0xF];
                    out += hex[c & 0xF];
                } else {
                    out += c;
                }
        }
    }
    return out;
}

class Parser {
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    // Nesting deeper than this is refused rather than recursed into.
    //
    // parseValue, parseObject and parseArray are mutual recursion with no
    // limit, so roughly two kilobytes of '[' was enough to exhaust the stack
    // and take the process down *inside* Json::Parse - which is precisely what
    // SceneSerializer's non-destructive design exists to survive. It reads the
    // whole file, parses it, and only then touches the registry, so that a bad
    // scene leaves the open one intact. A stack overflow during the parse
    // defeats that by killing the process instead of returning false.
    //
    // The limit is enforced while parsing rather than checked afterwards,
    // because Value's destructor recurses too: a tree deep enough to overflow
    // on the way in would also overflow being freed.
    static constexpr int kMaxDepth = 64;

    bool Parse(Value& out) {
        skipWhitespace();
        if (!parseValue(out)) return false;
        skipWhitespace();

        // Trailing content is a malformed document, not a parsed one. Without
        // this, "{...} garbage" returned the leading object and reported
        // success, so a truncated-then-appended file loaded as though it were
        // whole.
        if (m_pos != m_text.size()) return fail("trailing content after the top-level value");
        return true;
    }

    const std::string& Error() const { return m_error; }

private:
    void skipWhitespace() {
        while (m_pos < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_pos]))) ++m_pos;
    }

    bool fail(const std::string& why) {
        if (m_error.empty()) {
            m_error = why + " at offset " + std::to_string(m_pos);
        }
        return false;
    }

    bool parseValue(Value& out) {
        skipWhitespace();
        if (m_pos >= m_text.size()) return fail("unexpected end of input");

        // Every nested value goes through here, so this is the one place the
        // depth has to be counted.
        const DepthGuard guard(*this);
        if (m_depth > kMaxDepth) return fail("nesting deeper than 64 levels");

        switch (m_text[m_pos]) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': {
                std::string s;
                if (!parseString(s)) return false;
                out = Value(std::move(s));
                return true;
            }
            case 't':
                if (m_text.compare(m_pos, 4, "true") == 0) { m_pos += 4; out = Value(true); return true; }
                return fail("invalid literal");
            case 'f':
                if (m_text.compare(m_pos, 5, "false") == 0) { m_pos += 5; out = Value(false); return true; }
                return fail("invalid literal");
            case 'n':
                if (m_text.compare(m_pos, 4, "null") == 0) { m_pos += 4; out = Value(); return true; }
                return fail("invalid literal");
            default:
                return parseNumber(out);
        }
    }

    bool parseObject(Value& out) {
        ++m_pos; // '{'
        Object obj;
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == '}') { ++m_pos; out = Value(std::move(obj)); return true; }

        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return false;
            skipWhitespace();
            if (m_pos >= m_text.size() || m_text[m_pos] != ':') return fail("expected ':'");
            ++m_pos;

            Value value;
            if (!parseValue(value)) return false;
            // insert_or_assign, not emplace: std::map::emplace keeps the
            // FIRST value for a duplicate key and silently drops the second,
            // so a hand-edited scene with a repeated key loaded as whichever
            // copy happened to come first.
            obj.insert_or_assign(std::move(key), std::move(value));

            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("unterminated object");
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == '}') { ++m_pos; break; }
            return fail("expected ',' or '}'");
        }

        out = Value(std::move(obj));
        return true;
    }

    bool parseArray(Value& out) {
        ++m_pos; // '['
        Array arr;
        skipWhitespace();
        if (m_pos < m_text.size() && m_text[m_pos] == ']') { ++m_pos; out = Value(std::move(arr)); return true; }

        while (true) {
            Value value;
            if (!parseValue(value)) return false;
            arr.push_back(std::move(value));

            skipWhitespace();
            if (m_pos >= m_text.size()) return fail("unterminated array");
            if (m_text[m_pos] == ',') { ++m_pos; continue; }
            if (m_text[m_pos] == ']') { ++m_pos; break; }
            return fail("expected ',' or ']'");
        }

        out = Value(std::move(arr));
        return true;
    }

    bool parseString(std::string& out) {
        if (m_pos >= m_text.size() || m_text[m_pos] != '"') return fail("expected string");
        ++m_pos;

        out.clear();
        while (m_pos < m_text.size()) {
            const char c = m_text[m_pos++];
            if (c == '"') return true;
            if (c != '\\') { out += c; continue; }

            if (m_pos >= m_text.size()) return fail("unterminated escape");
            const char esc = m_text[m_pos++];
            switch (esc) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    if (m_pos + 4 > m_text.size()) return fail("truncated \\u escape");
                    const std::string hex = m_text.substr(m_pos, 4);
                    m_pos += 4;
                    const auto code = static_cast<unsigned>(std::strtoul(hex.c_str(), nullptr, 16));
                    // Minimal UTF-8 encoding; enough for the ASCII control
                    // characters Escape() emits.
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(Value& out) {
        const size_t start = m_pos;
        if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
        bool anyDigits = false;
        while (m_pos < m_text.size() &&
               (std::isdigit(static_cast<unsigned char>(m_text[m_pos])) ||
                m_text[m_pos] == '.' || m_text[m_pos] == 'e' || m_text[m_pos] == 'E' ||
                ((m_text[m_pos] == '+' || m_text[m_pos] == '-') &&
                 (m_text[m_pos - 1] == 'e' || m_text[m_pos - 1] == 'E')))) {
            if (std::isdigit(static_cast<unsigned char>(m_text[m_pos]))) anyDigits = true;
            ++m_pos;
        }
        if (!anyDigits) return fail("invalid number");

        out = Value(std::strtod(m_text.substr(start, m_pos - start).c_str(), nullptr));
        return true;
    }

    struct DepthGuard {
        explicit DepthGuard(Parser& parser) : m_parser(parser) { ++m_parser.m_depth; }
        ~DepthGuard() { --m_parser.m_depth; }
        Parser& m_parser;
    };

    const std::string& m_text;
    size_t m_pos{0};
    int m_depth{0};
    std::string m_error;
};

inline bool Parse(const std::string& text, Value& out, std::string& error) {
    Parser parser(text);
    if (parser.Parse(out)) return true;
    error = parser.Error();
    return false;
}

} // namespace Supersonic::Json
