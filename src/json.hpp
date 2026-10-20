// 极简 JSON：解析 + 序列化。无依赖，header-only。
// 支持 null / bool / number / string / array / object
#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cmath>
#include <sstream>
#include <fstream>
#include <stdexcept>

namespace sj {

class Value;
using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;
    Array arr;
    Object obj;

    Value() {}
    Value(Type t) : type(t) {}
    Value(bool v) : type(Type::Bool), b(v) {}
    Value(double v) : type(Type::Number), num(v) {}
    Value(int v) : type(Type::Number), num((double)v) {}
    Value(const char* v) : type(Type::String), str(v ? v : "") {}
    Value(const std::string& v) : type(Type::String), str(v) {}
    Value(const Array& v) : type(Type::Array), arr(v) {}
    Value(const Object& v) : type(Type::Object), obj(v) {}

    bool isNull() const { return type == Type::Null; }
    bool isNum() const { return type == Type::Number; }
    bool isStr() const { return type == Type::String; }
    bool isArr() const { return type == Type::Array; }
    bool isObj() const { return type == Type::Object; }
    bool isBool() const { return type == Type::Bool; }

    // 安全取值
    double numOr(double d = 0) const { return isNum() ? num : d; }
    int intOr(int d = 0) const { return isNum() ? (int)llround(num) : d; }
    bool boolOr(bool d = false) const { return isBool() ? b : d; }
    std::string strOr(const std::string& d = "") const { return isStr() ? str : d; }

    Value& operator[](const std::string& k) { return obj[k]; }
    const Value* find(const std::string& k) const {
        if (!isObj()) return nullptr;
        auto it = obj.find(k);
        return it == obj.end() ? nullptr : &it->second;
    }
    bool has(const std::string& k) const { return find(k) != nullptr; }
    std::string getStr(const std::string& k, const std::string& d = "") const {
        auto p = find(k); return p ? p->strOr(d) : d;
    }
    int getInt(const std::string& k, int d = 0) const {
        auto p = find(k); return p ? p->intOr(d) : d;
    }
    bool getBool(const std::string& k, bool d = false) const {
        auto p = find(k); return p ? p->boolOr(d) : d;
    }
};

// ---------------- 解析 ----------------
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parse() {
        skipWs();
        Value v = parseValue();
        return v;
    }

private:
    const std::string& s_;
    size_t i_ = 0;

    void skipWs() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\xEF' || c == '\xBB' || c == '\xBF')
                i_++;
            else break;
        }
    }
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }
    void expect(char c) {
        if (peek() != c) throw std::runtime_error("json: expect char");
        i_++;
    }

    Value parseValue() {
        char c = peek();
        switch (c) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return Value(parseString());
            case 't': expect('t'); lit("rue"); return Value(true);
            case 'f': expect('f'); lit("alse"); return Value(false);
            case 'n': expect('n'); lit("ull"); return Value();
            default: return parseNumber();
        }
    }
    void lit(const char* rest) {
        for (const char* p = rest; *p; p++) {
            if (peek() != *p) throw std::runtime_error("json: bad literal");
            i_++;
        }
    }
    Value parseNumber() {
        size_t start = i_;
        if (peek() == '-' || peek() == '+') i_++;
        while (i_ < s_.size() && (isdigit((unsigned char)s_[i_]) || s_[i_] == '.' ||
               s_[i_] == 'e' || s_[i_] == 'E' || s_[i_] == '-' || s_[i_] == '+'))
            i_++;
        if (i_ == start) throw std::runtime_error("json: bad number");
        return Value(strtod(s_.substr(start, i_ - start).c_str(), nullptr));
    }
    std::string parseString() {
        expect('"');
        std::string out;
        while (i_ < s_.size()) {
            char c = s_[i_++];
            if (c == '"') break;
            if (c == '\\') {
                if (i_ >= s_.size()) break;
                char e = s_[i_++];
                switch (e) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'u': {
                        if (i_ + 4 > s_.size()) break;
                        unsigned cp = (unsigned)strtoul(s_.substr(i_, 4).c_str(), nullptr, 16);
                        i_ += 4;
                        encodeUtf8(cp, out);
                        break;
                    }
                    case '/': out += '/'; break;
                    case '\\': out += '\\'; break;
                    case '"': out += '"'; break;
                    default: out += e; break;
                }
            } else out += c;
        }
        return out;
    }
    static void encodeUtf8(unsigned cp, std::string& out) {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }
    Value parseArray() {
        expect('[');
        Array a;
        skipWs();
        if (peek() == ']') { i_++; return Value(a); }
        while (i_ < s_.size()) {
            skipWs();
            a.push_back(parseValue());
            skipWs();
            if (peek() == ',') { i_++; continue; }
            if (peek() == ']') { i_++; break; }
            throw std::runtime_error("json: bad array");
        }
        return Value(a);
    }
    Value parseObject() {
        expect('{');
        Object o;
        skipWs();
        if (peek() == '}') { i_++; return Value(o); }
        while (i_ < s_.size()) {
            skipWs();
            std::string k = parseString();
            skipWs();
            expect(':');
            skipWs();
            o[k] = parseValue();
            skipWs();
            if (peek() == ',') { i_++; continue; }
            if (peek() == '}') { i_++; break; }
            throw std::runtime_error("json: bad object");
        }
        return Value(o);
    }
};

inline Value parse(const std::string& s) { return Parser(s).parse(); }

// ---------------- 序列化 ----------------
inline void escapeTo(const std::string& in, std::string& out) {
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out += buf;
                } else out += (char)c;
        }
    }
}

inline std::string dump(const Value& v, int indent = 2, int level = 0) {
    std::string pad(level * indent, ' ');
    std::string pad2((level + 1) * indent, ' ');
    std::string out;
    switch (v.type) {
        case Type::Null: return "null";
        case Type::Bool: return v.b ? "true" : "false";
        case Type::Number: {
            std::ostringstream os;
            if (std::fabs(v.num - std::llround(v.num)) < 1e-9) os << (long long)llround(v.num);
            else os << v.num;
            return os.str();
        }
        case Type::String: {
            std::string s = "\""; escapeTo(v.str, s); s += "\"";
            return s;
        }
        case Type::Array: {
            if (v.arr.empty()) return "[]";
            out = "[\n";
            for (size_t i = 0; i < v.arr.size(); i++) {
                out += pad2 + dump(v.arr[i], indent, level + 1);
                if (i + 1 < v.arr.size()) out += ",";
                out += "\n";
            }
            out += pad + "]";
            return out;
        }
        case Type::Object: {
            if (v.obj.empty()) return "{}";
            out = "{\n";
            size_t i = 0;
            for (auto& kv : v.obj) {
                std::string key = "\""; escapeTo(kv.first, key); key += "\"";
                out += pad2 + key + ": " + dump(kv.second, indent, level + 1);
                if (++i < v.obj.size()) out += ",";
                out += "\n";
            }
            out += pad + "}";
            return out;
        }
    }
    return "null";
}

inline bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}
inline bool writeFile(const std::string& path, const std::string& data) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f.write(data.data(), (std::streamsize)data.size());
    return true;
}

} // namespace sj
