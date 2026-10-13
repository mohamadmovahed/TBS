#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Small JSON value with parser and writer (enough for the daemon's config and API).
namespace json {

class Value
{
public:
    enum class Type { Null, Bool, Number, String, Array, Object };
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value>;

    Value() = default;
    Value(std::nullptr_t) {}
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    Value(int v) : m_type(Type::Number), m_number(v) {}
    Value(unsigned v) : m_type(Type::Number), m_number(v) {}
    Value(long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Value(unsigned long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Value(long long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Value(unsigned long long v) : m_type(Type::Number), m_number(static_cast<double>(v)) {}
    Value(double v) : m_type(Type::Number), m_number(v) {}
    Value(const char *s) : m_type(Type::String), m_string(s) {}
    Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {}
    Value(Array a) : m_type(Type::Array), m_array(std::make_shared<Array>(std::move(a))) {}
    Value(Object o) : m_type(Type::Object), m_object(std::make_shared<Object>(std::move(o))) {}

    static Value array() { return Value(Array{}); }
    static Value object() { return Value(Object{}); }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool toBool(bool def = false) const { return m_type == Type::Bool ? m_bool : def; }
    double toNumber(double def = 0.0) const { return m_type == Type::Number ? m_number : def; }
    int toInt(int def = 0) const { return m_type == Type::Number ? static_cast<int>(m_number) : def; }
    const std::string &toString() const;
    std::string toString(const std::string &def) const { return m_type == Type::String ? m_string : def; }

    // Arrays
    size_t size() const;
    const Value &operator[](size_t i) const;
    // Exact match for literals such as v[0] (which would otherwise also convert to a key)
    const Value &operator[](int i) const { return (*this)[static_cast<size_t>(i < 0 ? size() : i)]; }
    const Value &operator[](int i) { return static_cast<const Value &>(*this)[i]; }
    void push(Value v);
    const Array &items() const;

    // Objects (operator[] on a non-object turns it into an object)
    Value &operator[](const std::string &key);
    const Value &get(const std::string &key) const; // null if missing
    bool has(const std::string &key) const;
    const Object &members() const;

    std::string dump(int indent = -1) const;

private:
    void write(std::string &out, int indent, int depth) const;

    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::shared_ptr<Array> m_array;   // copy on write (see mutators)
    std::shared_ptr<Object> m_object;
};

// Returns false and a message with the position on error
bool parse(const std::string &text, Value *out, std::string *error = nullptr);

} // namespace json
