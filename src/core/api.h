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
// 用途：绑定当前线程的独立角色会话。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Session* s，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void bindSession(Session* s);

// 从存档 JSON 恢复到会话（等同 /api/load 的处理）
// 用途：校验并恢复账号游戏文本。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Session& s, const std::string& text, std::string* err，按本模块入口调用。
// 返回值：校验成功为真并替换会话；失败为假且保留旧游戏。
bool restoreGame(Session& s, const std::string& text, std::string* err);

// 当前游戏会话
// 用途：取得当前会话游戏并初始化缺失状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：当前账号游戏的可写引用。
Game& game();

// 只读斗法视图：不触发存档、时钟或其他游戏副作用。
// 用途：公开当前斗法预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value battleView(const Game& g);
// 用途：公开法器能力和装备效果。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value equipmentView(const Player& player, const Gear& gear);

// 处理 /api/ 请求
// 用途：按稳定路由分派请求并校验状态与参数。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const http::Request& req，按本模块入口调用。
// 返回值：包含状态码与JSON内容的HTTP响应。
http::Response handle(const http::Request& req);

} // namespace nz::api
