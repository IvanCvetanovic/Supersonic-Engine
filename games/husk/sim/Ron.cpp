#include "Ron.hpp"

#include <charconv>
#include <fstream>
#include <limits>
#include <sstream>

namespace husk::ron {

namespace {

bool isIdentStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool isIdentChar(char c) {
    return isIdentStart(c) || (c >= '0' && c <= '9');
}

// ron's is_float_char: what a numeric literal may contain.
bool isNumberChar(char c) {
    return (c >= '0' && c <= '9') || c == 'e' || c == 'E' || c == '.' || c == '+' || c == '-' ||
           c == '_';
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

class Parser {
public:
    Parser(std::string_view src, std::string_view source) : m_src(src), m_source(source) {}

    Value document() {
        skipTrivia();
        skipAttributes();
        Value v = value();
        skipTrivia();
        if (m_pos != m_src.size()) fail("unexpected text after the value");
        return v;
    }

private:
    std::string_view m_src;
    std::string_view m_source;
    size_t m_pos = 0;

    [[noreturn]] void fail(std::string_view what) const {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < m_pos && i < m_src.size(); ++i) {
            if (m_src[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        std::ostringstream msg;
        msg << m_source << ":" << line << ":" << col << ": " << what;
        throw Error(msg.str());
    }

    char peek(size_t ahead = 0) const {
        return m_pos + ahead < m_src.size() ? m_src[m_pos + ahead] : '\0';
    }

    void expect(char c) {
        skipTrivia();
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++m_pos;
    }

    void skipTrivia() {
        for (;;) {
            const char c = peek();
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else if (c == '/' && peek(1) == '/') {
                while (m_pos < m_src.size() && m_src[m_pos] != '\n') ++m_pos;
            } else if (c == '/' && peek(1) == '*') {
                // Nested, as ron's are.
                m_pos += 2;
                int depth = 1;
                while (depth > 0) {
                    if (m_pos >= m_src.size()) fail("unterminated block comment");
                    if (peek() == '/' && peek(1) == '*') {
                        ++depth;
                        m_pos += 2;
                    } else if (peek() == '*' && peek(1) == '/') {
                        --depth;
                        m_pos += 2;
                    } else {
                        ++m_pos;
                    }
                }
            } else {
                return;
            }
        }
    }

    // `#![enable(...)]` extension attributes. None of the shipped files uses
    // one; they are skipped rather than rejected so that one appearing is not a
    // parse error that reads like a data error.
    void skipAttributes() {
        while (peek() == '#' && peek(1) == '!') {
            while (m_pos < m_src.size() && m_src[m_pos] != ']') ++m_pos;
            if (m_pos < m_src.size()) ++m_pos;
            skipTrivia();
        }
    }

    std::string ident() {
        const size_t start = m_pos;
        while (isIdentChar(peek())) ++m_pos;
        return std::string(m_src.substr(start, m_pos - start));
    }

    Value value() {
        skipTrivia();
        const char c = peek();
        if (c == '"') return string();
        if (c == 'r' && (peek(1) == '"' || peek(1) == '#')) return rawString();
        if (c == '[') return list();
        if (c == '{') return map();
        if (c == '(') return parenthesised(std::string());
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') return number();
        if (isIdentStart(c)) {
            std::string name = ident();
            if (name == "true" || name == "false") {
                Value v;
                v.kind = Value::Kind::Bool;
                v.boolean = name == "true";
                return v;
            }
            const size_t save = m_pos;
            skipTrivia();
            if (peek() == '(') return parenthesised(std::move(name));
            m_pos = save;
            Value v;
            v.kind = Value::Kind::Ident;
            v.text = std::move(name);
            return v;
        }
        if (c == '\0') fail("unexpected end of input");
        fail(std::string("unexpected character '") + c + "'");
    }

    Value number() {
        const size_t start = m_pos;
        while (isNumberChar(peek())) ++m_pos;
        Value v;
        v.kind = Value::Kind::Number;
        for (char ch : m_src.substr(start, m_pos - start)) {
            if (ch != '_') v.text += ch;
        }
        return v;
    }

    Value string() {
        ++m_pos; // opening quote
        Value v;
        v.kind = Value::Kind::String;
        for (;;) {
            if (m_pos >= m_src.size()) fail("unterminated string");
            const char c = m_src[m_pos++];
            if (c == '"') break;
            if (c != '\\') {
                v.text += c;
                continue;
            }
            const char e = m_pos < m_src.size() ? m_src[m_pos++] : '\0';
            switch (e) {
            case 'n': v.text += '\n'; break;
            case 't': v.text += '\t'; break;
            case 'r': v.text += '\r'; break;
            case '0': v.text += '\0'; break;
            case '\\': v.text += '\\'; break;
            case '"': v.text += '"'; break;
            case '\'': v.text += '\''; break;
            case 'x': {
                if (m_pos + 2 > m_src.size()) fail("truncated \\x escape");
                uint32_t cp = 0;
                const auto r = std::from_chars(m_src.data() + m_pos, m_src.data() + m_pos + 2, cp, 16);
                if (r.ec != std::errc()) fail("bad \\x escape");
                m_pos += 2;
                v.text += static_cast<char>(cp);
                break;
            }
            case 'u': {
                if (peek() != '{') fail("expected '{' after \\u");
                ++m_pos;
                const size_t start = m_pos;
                while (m_pos < m_src.size() && m_src[m_pos] != '}') ++m_pos;
                uint32_t cp = 0;
                const auto r = std::from_chars(m_src.data() + start, m_src.data() + m_pos, cp, 16);
                if (r.ec != std::errc() || peek() != '}') fail("bad \\u{...} escape");
                ++m_pos;
                appendUtf8(v.text, cp);
                break;
            }
            case '\n': {
                // A backslash-newline continues the string past the leading
                // whitespace of the next line, as in Rust.
                while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++m_pos;
                break;
            }
            default: fail("unknown escape in string");
            }
        }
        return v;
    }

    Value rawString() {
        ++m_pos; // r
        size_t hashes = 0;
        while (peek() == '#') {
            ++hashes;
            ++m_pos;
        }
        if (peek() != '"') fail("expected '\"' in raw string");
        ++m_pos;
        Value v;
        v.kind = Value::Kind::String;
        for (;;) {
            if (m_pos >= m_src.size()) fail("unterminated raw string");
            if (m_src[m_pos] == '"') {
                size_t n = 0;
                while (n < hashes && peek(1 + n) == '#') ++n;
                if (n == hashes) {
                    m_pos += 1 + hashes;
                    break;
                }
            }
            v.text += m_src[m_pos++];
        }
        return v;
    }

    Value list() {
        ++m_pos; // [
        Value v;
        v.kind = Value::Kind::List;
        for (;;) {
            skipTrivia();
            if (peek() == ']') break;
            v.items.push_back(value());
            skipTrivia();
            if (peek() == ',') {
                ++m_pos;
                continue;
            }
            if (peek() != ']') fail("expected ',' or ']' in list");
        }
        ++m_pos;
        return v;
    }

    Value map() {
        ++m_pos; // {
        Value v;
        v.kind = Value::Kind::Map;
        for (;;) {
            skipTrivia();
            if (peek() == '}') break;
            v.keys.push_back(value());
            expect(':');
            v.items.push_back(value());
            skipTrivia();
            if (peek() == ',') {
                ++m_pos;
                continue;
            }
            if (peek() != '}') fail("expected ',' or '}' in map");
        }
        ++m_pos;
        return v;
    }

    // `(`...`)`, anonymous or after a name. A struct when the first thing
    // inside is `ident:`; a tuple otherwise.
    Value parenthesised(std::string name) {
        expect('(');
        skipTrivia();
        Value v;
        v.text = std::move(name);
        if (peek() == ')') {
            ++m_pos;
            v.kind = v.text.empty() ? Value::Kind::Unit : Value::Kind::Tuple;
            return v;
        }

        bool isStruct = false;
        const size_t save = m_pos;
        if (isIdentStart(peek())) {
            ident();
            skipTrivia();
            isStruct = peek() == ':' && peek(1) != ':';
        }
        m_pos = save;

        v.kind = isStruct ? Value::Kind::Struct : Value::Kind::Tuple;
        for (;;) {
            skipTrivia();
            if (peek() == ')') break;
            if (isStruct) {
                if (!isIdentStart(peek())) fail("expected a field name");
                v.names.push_back(ident());
                expect(':');
            }
            v.items.push_back(value());
            skipTrivia();
            if (peek() == ',') {
                ++m_pos;
                continue;
            }
            if (peek() != ')') fail(isStruct ? "expected ',' or ')' in struct" : "expected ',' or ')' in tuple");
        }
        ++m_pos;
        return v;
    }
};

const char* kindName(Value::Kind k) {
    switch (k) {
    case Value::Kind::Number: return "a number";
    case Value::Kind::String: return "a string";
    case Value::Kind::Bool: return "a boolean";
    case Value::Kind::Ident: return "an identifier";
    case Value::Kind::List: return "a list";
    case Value::Kind::Map: return "a map";
    case Value::Kind::Struct: return "a struct";
    case Value::Kind::Tuple: return "a tuple";
    case Value::Kind::Unit: return "()";
    }
    return "?";
}

[[noreturn]] void typeError(const Value& v, std::string_view wanted, std::string_view path) {
    std::ostringstream msg;
    msg << path << ": expected " << wanted << ", found " << kindName(v.kind);
    if (!v.text.empty() && v.kind != Value::Kind::String) msg << " '" << v.text << "'";
    throw Error(msg.str());
}

} // namespace

const Value* Value::field(std::string_view name) const {
    if (kind != Kind::Struct) return nullptr;
    for (size_t i = 0; i < names.size(); ++i) {
        if (names[i] == name) return &items[i];
    }
    return nullptr;
}

Value parse(std::string_view text, std::string_view source) {
    // A UTF-8 byte-order mark is not RON, but an editor can add one; serde's
    // RON rejects it, and so would this, with a message about a character the
    // file does not visibly contain. Skipped instead.
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return Parser(text, source).document();
}

Value parseFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("failed to read " + path.generic_string());
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();
    return parse(text, path.generic_string());
}

std::string join(std::string_view path, std::string_view field) {
    std::string out(path);
    out += '.';
    out += field;
    return out;
}

float asF32(const Value& v, std::string_view path) {
    if (v.kind == Value::Kind::Ident) {
        if (v.text == "inf") return std::numeric_limits<float>::infinity();
        if (v.text == "NaN") return std::numeric_limits<float>::quiet_NaN();
    }
    if (v.kind != Value::Kind::Number) typeError(v, "a float", path);
    std::string_view text = v.text;
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    float out = 0.0f;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size()) typeError(v, "a float", path);
    return out;
}

uint64_t asUInt(const Value& v, uint64_t max, std::string_view path) {
    if (v.kind != Value::Kind::Number) typeError(v, "an unsigned integer", path);
    std::string_view text = v.text;
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    uint64_t out = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size()) {
        typeError(v, "an unsigned integer", path);
    }
    if (out > max) {
        throw Error(std::string(path) + ": " + v.text + " is out of range");
    }
    return out;
}

int64_t asInt(const Value& v, std::string_view path) {
    if (v.kind != Value::Kind::Number) typeError(v, "an integer", path);
    std::string_view text = v.text;
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    int64_t out = 0;
    const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
    if (r.ec != std::errc() || r.ptr != text.data() + text.size()) typeError(v, "an integer", path);
    return out;
}

bool asBool(const Value& v, std::string_view path) {
    if (v.kind != Value::Kind::Bool) typeError(v, "a boolean", path);
    return v.boolean;
}

const std::string& asString(const Value& v, std::string_view path) {
    if (v.kind != Value::Kind::String) typeError(v, "a string", path);
    return v.text;
}

const Value& asStruct(const Value& v, std::string_view path) {
    // `()` is a struct with no fields as far as serde's RON is concerned, so a
    // struct whose every field has a default may be written that way.
    if (v.kind != Value::Kind::Struct && v.kind != Value::Kind::Unit) typeError(v, "a struct", path);
    return v;
}

const std::vector<Value>& asList(const Value& v, std::string_view path) {
    if (v.kind != Value::Kind::List) typeError(v, "a list", path);
    return v.items;
}

const std::vector<Value>& asTuple(const Value& v, size_t arity, std::string_view path) {
    if (v.kind != Value::Kind::Tuple || !v.text.empty()) typeError(v, "a tuple", path);
    if (v.items.size() != arity) {
        throw Error(std::string(path) + ": expected a tuple of " + std::to_string(arity) +
                    " elements, found " + std::to_string(v.items.size()));
    }
    return v.items;
}

const Value* asOption(const Value& v, std::string_view path) {
    if (v.kind == Value::Kind::Ident && v.text == "None") return nullptr;
    if (v.kind == Value::Kind::Tuple && v.text == "Some" && v.items.size() == 1) return &v.items[0];
    typeError(v, "Some(..) or None", path);
}

const Value& require(const Value& s, std::string_view name, std::string_view path) {
    const Value* f = asStruct(s, path).field(name);
    if (!f) throw Error(std::string(path) + ": missing field `" + std::string(name) + "`");
    return *f;
}

Variant asVariant(const Value& v, std::string_view path) {
    if (v.kind == Value::Kind::Ident) return {v.text, nullptr};
    if ((v.kind == Value::Kind::Tuple || v.kind == Value::Kind::Struct) && !v.text.empty()) {
        return {v.text, &v};
    }
    typeError(v, "an enum variant", path);
}

} // namespace husk::ron
