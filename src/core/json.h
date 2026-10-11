// 宁州一梦 · C++ 版 — 极简 JSON 读写
//
// 不引入第三方库：游戏存档只需要对象/数组/字符串/数字/布尔/null
// 这几种结构，自己写一个够用的小实现，方便你直接读代码调试。
#pragma once

#include <map>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace nz::json {

class Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value>;

enum class Type { Null, Bool, Number, String, Array, Object };

class Value {
public:
    Value() : type_(Type::Null) {}
    Value(bool b) : type_(Type::Bool), bool_(b) {}
    Value(double n) : type_(Type::Number), num_(n) {}
    Value(int n) : type_(Type::Number), num_(n) {}
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(Array a) : type_(Type::Array), arr_(std::move(a)) {}
    Value(Object o) : type_(Type::Object), obj_(std::move(o)) {}

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBool() const { return type_ == Type::Bool; }

    // 取值；类型不符时返回默认值，避免抛异常打断调试
    bool asBool(bool def = false) const {
        return type_ == Type::Bool ? bool_ : def;
    }
    double asNumber(double def = 0) const {
        return type_ == Type::Number ? num_ : def;
    }
    int asInt(int def = 0) const {
        if (type_ != Type::Number || !std::isfinite(num_) ||
            num_ < std::numeric_limits<int>::min() ||
            num_ > std::numeric_limits<int>::max()) return def;
        return static_cast<int>(num_);
    }
    const std::string& asString(const std::string& def = emptyStr()) const {
        return type_ == Type::String ? str_ : def;
    }

    const Array& asArray() const { return arr_; }
    Array& asArray() { return arr_; }
    const Object& asObject() const { return obj_; }
    Object& asObject() { return obj_; }

    // 对象取值：不存在或类型不符时返回 Null
    const Value& operator[](const std::string& key) const {
        static const Value nul;
        if (type_ != Type::Object) return nul;
        auto it = obj_.find(key);
        return it == obj_.end() ? nul : it->second;
    }
    // 数组下标
    const Value& operator[](size_t i) const {
        static const Value nul;
        if (type_ != Type::Array || i >= arr_.size()) return nul;
        return arr_[i];
    }

    void set(const std::string& key, Value v) {
        type_ = Type::Object;
        obj_[key] = std::move(v);
    }
    void push(Value v) {
        type_ = Type::Array;
        arr_.push_back(std::move(v));
    }

    // 序列化。pretty 为 true 时缩进，便于人读存档
    std::string dump(int indent = -1) const;

    // 解析。失败时返回 Null 并把错误写进 err
    static Value parse(const std::string& text, std::string* err = nullptr);

private:
    static const std::string& emptyStr() {
        static const std::string s;
        return s;
    }
    Type type_;
    bool bool_ = false;
    double num_ = 0;
    std::string str_;
    Array arr_;
    Object obj_;
};

} // namespace nz::json
