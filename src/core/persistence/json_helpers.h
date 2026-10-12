#pragma once

#include "core/json.h"
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace nz::persistence {
using json::Value;

inline Value num(int value) { return Value(static_cast<double>(value)); }
inline Value num(double value) { return Value(value); }
inline Value str(const std::string& value) { return Value(value); }

// map<string, 整数/浮点> 的互转
template <typename T>
inline Value mapToJson(const std::map<std::string, T>& values) {
    Value object(json::Object{});
    for (const auto& [key, value] : values) object.set(key, Value(static_cast<double>(value)));
    return object;
}
template <typename T>
inline std::map<std::string, T> jsonToMap(const Value& value) {
    std::map<std::string, T> out;
    if (!value.isObject()) return out;
    for (const auto& [key, entry] : value.asObject())
        if constexpr (std::is_integral_v<T>) out[key] = entry.asInt();
        else out[key] = static_cast<T>(entry.asNumber());
    return out;
}

// map<string,string> 专用（法器装备槽）
inline Value strMapToJson(const std::map<std::string, std::string>& values) {
    Value object(json::Object{});
    for (const auto& [key, value] : values) object.set(key, str(value));
    return object;
}
inline std::map<std::string, std::string> jsonToStrMap(const Value& value) {
    std::map<std::string, std::string> out;
    if (!value.isObject()) return out;
    for (const auto& [key, entry] : value.asObject()) out[key] = entry.asString();
    return out;
}

// vector<pair<string,int>> 的互转（成就用）
inline Value pairsToJson(const std::vector<std::pair<std::string, int>>& value) {
    json::Array array;
    for (const auto& [name, year] : value) {
        Value record(json::Object{});
        record.set("name", str(name));
        record.set("year", num(year));
        array.push_back(std::move(record));
    }
    return Value(std::move(array));
}

inline std::vector<std::string> jsonToStringVec(const Value& value) {
    std::vector<std::string> out;
    if (!value.isArray()) return out;
    for (const auto& item : value.asArray()) out.push_back(item.asString());
    return out;
}

inline Value stringVectorToJson(const std::vector<std::string>& values) {
    json::Array array;
    for (const auto& value : values) array.push_back(str(value));
    return Value(std::move(array));
}

} // namespace nz::persistence
