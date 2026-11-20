// Minimal dependency-free JSON parser.
// Supports the full JSON grammar (objects, arrays, strings with escapes,
// numbers, booleans, null). Deliberately small: this app only needs to read
// a handful of fields out of a fairly deep, nested API response, so a
// generic DOM-style value is simpler than a bespoke streaming parser.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiosk::json {

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }

    bool asBool(bool fallback = false) const {
        return type_ == Type::Bool ? bool_ : fallback;
    }
    double asDouble(double fallback = 0.0) const {
        return type_ == Type::Number ? number_ : fallback;
    }
    long asLong(long fallback = 0) const {
        return type_ == Type::Number ? static_cast<long>(number_) : fallback;
    }
    const std::string& asString(const std::string& fallback = "") const {
        return type_ == Type::String ? string_ : fallback;
    }
    const std::vector<Value>& asArray() const {
        static const std::vector<Value> empty;
        return type_ == Type::Array ? array_ : empty;
    }

    // Returns a Null value (not a reference into the map) when missing, so
    // callers can safely chain `.at("a").at("b").asString()` even for
    // partially-malformed objects.
    const Value& at(const std::string& key) const {
        static const Value null_value;
        if (type_ != Type::Object) return null_value;
        auto it = object_.find(key);
        return it == object_.end() ? null_value : it->second;
    }

    bool has(const std::string& key) const {
        return type_ == Type::Object && object_.count(key) > 0;
    }

    static Value makeNull() { return Value(); }
    static Value makeBool(bool b) { Value v; v.type_ = Type::Bool; v.bool_ = b; return v; }
    static Value makeNumber(double d) { Value v; v.type_ = Type::Number; v.number_ = d; return v; }
    static Value makeString(std::string s) { Value v; v.type_ = Type::String; v.string_ = std::move(s); return v; }
    static Value makeArray(std::vector<Value> a) { Value v; v.type_ = Type::Array; v.array_ = std::move(a); return v; }
    static Value makeObject(std::map<std::string, Value> o) { Value v; v.type_ = Type::Object; v.object_ = std::move(o); return v; }

private:
    Type type_;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::vector<Value> array_;
    std::map<std::string, Value> object_;
};

struct ParseError : std::runtime_error {
    explicit ParseError(const std::string& msg) : std::runtime_error(msg) {}
};

// Throws ParseError on malformed input.
Value parse(const std::string& text);

// Non-throwing variant. Returns false and leaves `out` untouched on failure.
bool tryParse(const std::string& text, Value& out) noexcept;

} // namespace kiosk::json
