// 宁州一梦 · C++ 版 — 游戏接口（前端通过 HTTP 调用这里的规则）
//
// 这一层是「浏览器界面」与「C++ 规则」之间的桥：
//   前端发 POST /api/<动作>  ->  这里解析参数、调用 core 函数  ->  返回 JSON
//
// 设计原则：这一层只做「翻译」，不写任何规则。
// 所有数值都来自 core，保证和自测覆盖的是同一套代码。
#pragma once

#include <string>

#include "core/http.h"
#include "core/model.h"

namespace nz::api {

// 当前游戏会话（单机单人，全局一份就够）
Game& game();

// 处理 /api/ 请求
http::Response handle(const http::Request& req);

} // namespace nz::api
