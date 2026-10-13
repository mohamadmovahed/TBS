#include "Json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace json {

namespace {

const Value kNull;
const std::string kEmpty;
const Value::Array kNoItems;
const Value::Object kNoMembers;

void escape(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

void appendUtf8(std::string &out, uint32_t cp)
{
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

class Parser
{
public:
    explicit Parser(const std::string &t) : s(t) {}

    bool run(Value *out, std::string *error)
    {
        ws();
        if (!value(out, 0) || (ws(), pos != s.size())) {
            if (error)
                *error = (msg.empty() ? std::string("unexpected character") : msg) + " at offset " + std::to_string(pos);
            return false;
        }
        return true;
    }

private:
    void ws()
    {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
            ++pos;
    }
    bool fail(const char *m)
    {
        msg = m;
        return false;
    }
    bool literal(const char *word)
    {
        const size_t n = std::strlen(word);
        if (s.compare(pos, n, word) != 0)
            return fail("invalid literal");
        pos += n;
        return true;
    }
    bool hex4(uint32_t *v)
    {
        if (pos + 4 > s.size())
            return fail("short \\u escape");
        *v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s[pos++];
            *v <<= 4;
            if (c >= '0' && c <= '9')
                *v |= static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f')
                *v |= static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                *v |= static_cast<uint32_t>(c - 'A' + 10);
            else
                return fail("bad \\u escape");
        }
        return true;
    }
    bool string(std::string *out)
    {
        ++pos; // opening quote
        while (pos < s.size()) {
            const char c = s[pos++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\') {
                *out += c;
                continue;
            }
            if (pos >= s.size())
                break;
            const char e = s[pos++];
            switch (e) {
            case '"': *out += '"'; break;
            case '\\': *out += '\\'; break;
            case '/': *out += '/'; break;
            case 'b': *out += '\b'; break;
            case 'f': *out += '\f'; break;
            case 'n': *out += '\n'; break;
            case 'r': *out += '\r'; break;
            case 't': *out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!hex4(&cp))
                    return false;
                if (cp >= 0xD800 && cp < 0xDC00 && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                    pos += 2;
                    uint32_t lo = 0;
                    if (!hex4(&lo))
                        return false;
                    if (lo >= 0xDC00 && lo < 0xE000)
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                appendUtf8(*out, cp);
                break;
            }
            default:
                return fail("bad escape");
            }
        }
        return fail("unterminated string");
    }
    bool value(Value *out, int depth)
    {
        if (depth > 64)
            return fail("nesting too deep");
        if (pos >= s.size())
            return fail("unexpected end");
        const char c = s[pos];
        if (c == 'n') {
            *out = Value();
            return literal("null");
        }
        if (c == 't') {
            *out = Value(true);
            return literal("true");
        }
        if (c == 'f') {
            *out = Value(false);
            return literal("false");
        }
        if (c == '"') {
            std::string str;
            if (!string(&str))
                return false;
            *out = Value(std::move(str));
            return true;
        }
        if (c == '[') {
            ++pos;
            Value::Array a;
            ws();
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                *out = Value(std::move(a));
                return true;
            }
            while (true) {
                Value v;
                ws();
                if (!value(&v, depth + 1))
                    return false;
                a.push_back(std::move(v));
                ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == ']') {
                    ++pos;
                    *out = Value(std::move(a));
                    return true;
                }
                return fail("expected , or ]");
            }
        }
        if (c == '{') {
            ++pos;
            Value::Object o;
            ws();
            if (pos < s.size() && s[pos] == '}') {
                ++pos;
                *out = Value(std::move(o));
                return true;
            }
            while (true) {
                ws();
                if (pos >= s.size() || s[pos] != '"')
                    return fail("expected a key");
                std::string key;
                if (!string(&key))
                    return false;
                ws();
                if (pos >= s.size() || s[pos] != ':')
                    return fail("expected :");
                ++pos;
                ws();
                Value v;
                if (!value(&v, depth + 1))
                    return false;
                o[key] = std::move(v);
                ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == '}') {
                    ++pos;
                    *out = Value(std::move(o));
                    return true;
                }
                return fail("expected , or }");
            }
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const char *begin = s.c_str() + pos;
            char *end = nullptr;
            const double d = std::strtod(begin, &end);
            if (end == begin)
                return fail("bad number");
            pos += static_cast<size_t>(end - begin);
            *out = Value(d);
            return true;
        }
        return fail("unexpected character");
    }

    const std::string &s;
    size_t pos = 0;
    std::string msg;
};

} // namespace

const std::string &Value::toString() const
{
    return m_type == Type::String ? m_string : kEmpty;
}

size_t Value::size() const
{
    if (m_type == Type::Array)
        return m_array->size();
    if (m_type == Type::Object)
        return m_object->size();
    return 0;
}

const Value &Value::operator[](size_t i) const
{
    return m_type == Type::Array && i < m_array->size() ? (*m_array)[i] : kNull;
}

void Value::push(Value v)
{
    if (m_type != Type::Array) {
        *this = Value(Array{});
    } else if (m_array.use_count() > 1) {
        m_array = std::make_shared<Array>(*m_array);
    }
    m_array->push_back(std::move(v));
}

const Value::Array &Value::items() const
{
    return m_type == Type::Array ? *m_array : kNoItems;
}

Value &Value::operator[](const std::string &key)
{
    if (m_type != Type::Object)
        *this = Value(Object{});
    else if (m_object.use_count() > 1)
        m_object = std::make_shared<Object>(*m_object);
    return (*m_object)[key];
}

const Value &Value::get(const std::string &key) const
{
    if (m_type != Type::Object)
        return kNull;
    auto it = m_object->find(key);
    return it == m_object->end() ? kNull : it->second;
}

bool Value::has(const std::string &key) const
{
    return m_type == Type::Object && m_object->count(key) > 0;
}

const Value::Object &Value::members() const
{
    return m_type == Type::Object ? *m_object : kNoMembers;
}

std::string Value::dump(int indent) const
{
    std::string out;
    write(out, indent, 0);
    return out;
}

void Value::write(std::string &out, int indent, int depth) const
{
    auto newline = [&](int d) {
        if (indent < 0)
            return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (m_type) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += m_bool ? "true" : "false"; break;
    case Type::Number: {
        if (!std::isfinite(m_number)) {
            out += "null";
        } else if (std::fabs(m_number) < 9e15 && m_number == std::floor(m_number)) {
            out += std::to_string(static_cast<long long>(m_number));
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.12g", m_number);
            out += buf;
        }
        break;
    }
    case Type::String: escape(out, m_string); break;
    case Type::Array: {
        out += '[';
        bool first = true;
        for (const Value &v : *m_array) {
            if (!first)
                out += ',';
            first = false;
            newline(depth + 1);
            v.write(out, indent, depth + 1);
        }
        if (!m_array->empty())
            newline(depth);
        out += ']';
        break;
    }
    case Type::Object: {
        out += '{';
        bool first = true;
        for (const auto &kv : *m_object) {
            if (!first)
                out += ',';
            first = false;
            newline(depth + 1);
            escape(out, kv.first);
            out += indent >= 0 ? ": " : ":";
            kv.second.write(out, indent, depth + 1);
        }
        if (!m_object->empty())
            newline(depth);
        out += '}';
        break;
    }
    }
}

bool parse(const std::string &text, Value *out, std::string *error)
{
    Parser p(text);
    Value v;
    if (!p.run(&v, error))
        return false;
    if (out)
        *out = std::move(v);
    return true;
}

} // namespace json
