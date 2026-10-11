// 宁州一梦 · C++ 版 — 极简 JSON 实现
#include "core/json.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <limits>
#include <locale>

namespace nz::json {

// ------------------------------------------------------------
// 序列化
// ------------------------------------------------------------
static void escapeTo(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
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
                    // 中文按 UTF-8 原样输出，存档可读
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

static void numberTo(std::string& out, double n) {
    if (!std::isfinite(n)) { out += "null"; return; }
    // carry 等小数必须保留双精度，六位截断会让读档改变日收益。
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << n;
    out += stream.str();
}

static void dumpTo(std::string& out, const Value& v, int indent, int depth) {
    const bool pretty = indent >= 0;
    auto newline = [&](int d) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };

    switch (v.type()) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += v.asBool() ? "true" : "false"; break;
        case Type::Number: numberTo(out, v.asNumber()); break;
        case Type::String: escapeTo(out, v.asString()); break;
        case Type::Array: {
            const auto& a = v.asArray();
            if (a.empty()) { out += "[]"; break; }
            out += '[';
            for (size_t i = 0; i < a.size(); ++i) {
                newline(depth + 1);
                dumpTo(out, a[i], indent, depth + 1);
                if (i + 1 < a.size()) out += ',';
            }
            newline(depth);
            out += ']';
            break;
        }
        case Type::Object: {
            const auto& o = v.asObject();
            if (o.empty()) { out += "{}"; break; }
            out += '{';
            bool first = true;
            for (const auto& [k, val] : o) {
                if (!first) out += ',';
                first = false;
                newline(depth + 1);
                escapeTo(out, k);
                out += ':';
                if (pretty) out += ' ';
                dumpTo(out, val, indent, depth + 1);
            }
            newline(depth);
            out += '}';
            break;
        }
    }
}

std::string Value::dump(int indent) const {
    std::string out;
    dumpTo(out, *this, indent, 0);
    if (indent >= 0) out += '\n';
    return out;
}

// ------------------------------------------------------------
// 解析
// ------------------------------------------------------------
namespace {

struct Parser {
    const std::string& s;
    size_t i = 0;
    std::string err;
    int depth = 0;

    explicit Parser(const std::string& text) : s(text) {}

    void skipWs() {
        while (i < s.size()) {
            const char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
            else break;
        }
    }
    bool fail(const std::string& msg) {
        if (err.empty()) err = msg + "（位置 " + std::to_string(i) + "）";
        return false;
    }

    bool parseValue(Value& out) {
        skipWs();
        if (i >= s.size()) return fail("内容意外结束");
        const char c = s[i];

        if (c == '{' || c == '[') {
            if (depth >= 128) return fail("JSON 嵌套过深");
            ++depth;
            const bool ok = c == '{' ? parseObject(out) : parseArray(out);
            --depth;
            return ok;
        }
        if (c == '"') {
            std::string str;
            if (!parseString(str)) return false;
            out = Value(str);
            return true;
        }
        if (c == 't' || c == 'f') return parseBool(out);
        if (c == 'n') return parseNull(out);
        return parseNumber(out);
    }

    bool parseObject(Value& out) {
        ++i;   // 跳过 '{'
        Object obj;
        skipWs();
        if (i < s.size() && s[i] == '}') { ++i; out = Value(obj); return true; }
        while (true) {
            skipWs();
            std::string key;
            if (i >= s.size() || s[i] != '"') return fail("对象的键必须是字符串");
            if (!parseString(key)) return false;
            skipWs();
            if (i >= s.size() || s[i] != ':') return fail("键后缺少冒号");
            ++i;
            Value v;
            if (!parseValue(v)) return false;
            if (!obj.emplace(key, std::move(v)).second) return fail("对象中有重复的键");
            skipWs();
            if (i >= s.size()) return fail("对象未闭合");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == '}') { ++i; break; }
            return fail("对象中出现意外字符");
        }
        out = Value(std::move(obj));
        return true;
    }

    bool parseArray(Value& out) {
        ++i;   // 跳过 '['
        Array arr;
        skipWs();
        if (i < s.size() && s[i] == ']') { ++i; out = Value(arr); return true; }
        while (true) {
            Value v;
            if (!parseValue(v)) return false;
            arr.push_back(std::move(v));
            skipWs();
            if (i >= s.size()) return fail("数组未闭合");
            if (s[i] == ',') { ++i; continue; }
            if (s[i] == ']') { ++i; break; }
            return fail("数组中出现意外字符");
        }
        out = Value(std::move(arr));
        return true;
    }

    bool parseString(std::string& out) {
        ++i;   // 跳过起始引号
        out.clear();
        while (i < s.size()) {
            const char c = s[i++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return fail("字符串含未转义控制字符");
            if (c != '\\') { out += c; continue; }
            if (i >= s.size()) return fail("转义字符不完整");
            const char e = s[i++];
            switch (e) {
                case '"':  out += '"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned code = 0;
                    if (!parseHex(code)) return false;
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (i + 2 > s.size() || s[i] != '\\' || s[i + 1] != 'u')
                            return fail("Unicode 高代理项缺少低代理项");
                        i += 2;
                        unsigned low = 0;
                        if (!parseHex(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return fail("Unicode 代理项无效");
                        code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    } else if (code >= 0xDC00 && code <= 0xDFFF) {
                        return fail("Unicode 低代理项没有高代理项");
                    }
                    // 转为 UTF-8
                    if (code < 0x80) {
                        out += static_cast<char>(code);
                    } else if (code < 0x800) {
                        out += static_cast<char>(0xC0 | (code >> 6));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else if (code < 0x10000) {
                        out += static_cast<char>(0xE0 | (code >> 12));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        out += static_cast<char>(0xF0 | (code >> 18));
                        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        out += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return fail("未知的转义字符");
            }
        }
        return fail("字符串未闭合");
    }

    bool parseHex(unsigned& code) {
        if (i + 4 > s.size()) return fail("\\u 转义不完整");
        code = 0;
        for (int k = 0; k < 4; ++k) {
            const char h = s[i++];
            unsigned value;
            if (h >= '0' && h <= '9') value = h - '0';
            else if (h >= 'a' && h <= 'f') value = h - 'a' + 10;
            else if (h >= 'A' && h <= 'F') value = h - 'A' + 10;
            else return fail("\\u 转义必须包含四位十六进制数字");
            code = (code << 4) | value;
        }
        return true;
    }

    bool parseBool(Value& out) {
        if (s.compare(i, 4, "true") == 0) { i += 4; out = Value(true); return true; }
        if (s.compare(i, 5, "false") == 0) { i += 5; out = Value(false); return true; }
        return fail("无效的布尔值");
    }

    bool parseNull(Value& out) {
        if (s.compare(i, 4, "null") == 0) { i += 4; out = Value(); return true; }
        return fail("无效的 null");
    }

    bool parseNumber(Value& out) {
        const size_t start = i;
        auto digit = [&] { return i < s.size() && s[i] >= '0' && s[i] <= '9'; };
        if (i < s.size() && s[i] == '-') ++i;
        if (!digit()) return fail("无效的数字");
        if (s[i] == '0') ++i;
        else while (digit()) ++i;
        if (i < s.size() && s[i] == '.') {
            ++i;
            if (!digit()) return fail("小数点后缺少数字");
            while (digit()) ++i;
        }
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
            ++i;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            if (!digit()) return fail("指数缺少数字");
            while (digit()) ++i;
        }
        try {
            size_t consumed = 0;
            const double number = std::stod(s.substr(start, i - start), &consumed);
            if (consumed != i - start || !std::isfinite(number)) return fail("无效的数字");
            out = Value(number);
        } catch (...) {
            return fail("数字解析失败");
        }
        return true;
    }
};

} // namespace

Value Value::parse(const std::string& text, std::string* err) {
    if (err) err->clear();
    Parser p(text);
    Value v;
    if (!p.parseValue(v)) {
        if (err) *err = p.err;
        return Value();
    }
    p.skipWs();
    if (p.i != text.size()) {
        if (err) *err = "末尾有多余内容（位置 " + std::to_string(p.i) + "）";
        return Value();
    }
    return v;
}

} // namespace nz::json
