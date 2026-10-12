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
#include "core/json.h"

namespace nz::api {

// 一个玩家的游戏会话。多人服务端为每个玩家准备一份，
// 处理请求前用 bindSession 切换「当前」会话（请求处理是串行的）。
struct Session {
    Game g;
    bool started = false;
    std::string lastAutomatic;
    std::string writeError;
};

// 切换当前会话；nullptr 表示使用内置的默认会话（单机/自测）
void bindSession(Session* s);

// 从存档 JSON 恢复到会话（等同 /api/load 的处理）
bool restoreGame(Session& s, const std::string& text, std::string* err);

// 当前游戏会话
Game& game();

// 只读斗法视图：不触发存档、时钟或其他游戏副作用。
json::Value battleView(const Game& g);
json::Value equipmentView(const Player& player, const Gear& gear);

// 处理 /api/ 请求
http::Response handle(const http::Request& req);

} // namespace nz::api
