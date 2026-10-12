#pragma once
#include "core/json.h"
#include <iomanip>
#include <sstream>

namespace nz::api::view {
using json::Value;
// 用途：把数值封装为JSON数值。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入int value，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
inline Value num(int value) { return Value(value); }
// 用途：把数值封装为JSON数值。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入double value，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
inline Value num(double value) { return Value(value); }
// 用途：把数值封装为JSON数值。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入long long value，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
inline Value num(long long value) { return Value(static_cast<double>(value)); }
// 用途：把文字封装为JSON字符串。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const std::string& value，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
inline Value str(const std::string& value) { return Value(value); }
// 用途：把文字封装为JSON字符串。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const char* value，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
inline Value str(const char* value) { return Value(std::string(value ? value : "")); }
// 用途：按固定一位小数格式化界面数值。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入double value，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
inline std::string decimal(double value) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(1) << value;
    return output.str();
}
} // namespace nz::api::view
