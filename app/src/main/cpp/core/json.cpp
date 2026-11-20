#include "json.h"

#include <cctype>
#include <cstdlib>

namespace kiosk::json {

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text), pos_(0), len_(text.size()) {}

    Value parseDocument() {
        skipWhitespace();
        Value v = parseValue();
        skipWhitespace();
        // Trailing garbage after the top-level value is tolerated rather than
        // treated as fatal -- APIs occasionally append trailing newlines/junk.
        return v;
    }

private:
    const std::string& s_;
    size_t pos_;
    size_t len_;

    [[noreturn]] void fail(const std::string& what) {
        throw ParseError("JSON parse error at offset " + std::to_string(pos_) + ": " + what);
    }

    char peek() {
        if (pos_ >= len_) fail("unexpected end of input");
        return s_[pos_];
    }

    char advance() { return s_[pos_++]; }

    bool eof() const { return pos_ >= len_; }

    void skipWhitespace() {
        while (pos_ < len_) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else {
                break;
            }
        }
    }

    void expect(char c) {
        if (eof() || s_[pos_] != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    bool matchLiteral(const char* lit, size_t n) {
        if (pos_ + n > len_) return false;
        if (s_.compare(pos_, n, lit) != 0) return false;
        pos_ += n;
        return true;
    }

    Value parseValue() {
        skipWhitespace();
        if (eof()) fail("unexpected end of input");
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value::makeString(parseStringRaw());
            case 't':
                if (matchLiteral("true", 4)) return Value::makeBool(true);
                fail("invalid literal");
            case 'f':
                if (matchLiteral("false", 5)) return Value::makeBool(false);
                fail("invalid literal");
            case 'n':
                if (matchLiteral("null", 4)) return Value::makeNull();
                fail("invalid literal");
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail("unexpected character");
        }
    }

    Value parseObject() {
        expect('{');
        std::map<std::string, Value> obj;
        skipWhitespace();
        if (!eof() && peek() == '}') { ++pos_; return Value::makeObject(std::move(obj)); }
        while (true) {
            skipWhitespace();
            if (eof() || peek() != '"') fail("expected string key");
            std::string key = parseStringRaw();
            skipWhitespace();
            expect(':');
            Value val = parseValue();
            obj[key] = std::move(val);
            skipWhitespace();
            if (eof()) fail("unterminated object");
            char c = advance();
            if (c == ',') continue;
            if (c == '}') break;
            fail("expected ',' or '}'");
        }
        return Value::makeObject(std::move(obj));
    }

    Value parseArray() {
        expect('[');
        std::vector<Value> arr;
        skipWhitespace();
        if (!eof() && peek() == ']') { ++pos_; return Value::makeArray(std::move(arr)); }
        while (true) {
            Value val = parseValue();
            arr.push_back(std::move(val));
            skipWhitespace();
            if (eof()) fail("unterminated array");
            char c = advance();
            if (c == ',') continue;
            if (c == ']') break;
            fail("expected ',' or ']'");
        }
        return Value::makeArray(std::move(arr));
    }

    static void appendUtf8(std::string& out, unsigned int codepoint) {
        if (codepoint <= 0x7F) {
            out.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }

    unsigned int parseHex4() {
        if (pos_ + 4 > len_) fail("truncated \\u escape");
        unsigned int value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned int>(c - 'A' + 10);
            else fail("invalid \\u escape");
        }
        return value;
    }

    std::string parseStringRaw() {
        expect('"');
        std::string out;
        while (true) {
            if (eof()) fail("unterminated string");
            char c = advance();
            if (c == '"') break;
            if (c == '\\') {
                if (eof()) fail("unterminated escape");
                char esc = advance();
                switch (esc) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        unsigned int cp = parseHex4();
                        if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < len_ &&
                            s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            unsigned int low = parseHex4();
                            if (low >= 0xDC00 && low <= 0xDFFF) {
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                            }
                        }
                        appendUtf8(out, cp);
                        break;
                    }
                    default:
                        fail("invalid escape character");
                }
            } else {
                out.push_back(c);
            }
        }
        return out;
    }

    Value parseNumber() {
        size_t start = pos_;
        if (!eof() && peek() == '-') ++pos_;
        if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number");
        while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        if (!eof() && peek() == '.') {
            ++pos_;
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number");
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!eof() && (peek() == '+' || peek() == '-')) ++pos_;
            if (eof() || !std::isdigit(static_cast<unsigned char>(peek()))) fail("invalid number exponent");
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek()))) ++pos_;
        }
        std::string token = s_.substr(start, pos_ - start);
        return Value::makeNumber(std::strtod(token.c_str(), nullptr));
    }
};

} // namespace

Value parse(const std::string& text) {
    Parser parser(text);
    return parser.parseDocument();
}

bool tryParse(const std::string& text, Value& out) noexcept {
    try {
        out = parse(text);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace kiosk::json
