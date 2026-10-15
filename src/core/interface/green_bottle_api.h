#pragma once
#include "core/model.h"
#include "core/json.h"
#include "core/interface/routes.h"

namespace nz::api::view {
// 用途：输出永久绿瓶升级/真实秘境入口；流程：洞府state；返回：无天赋仅hasTalent=false，纯查询无初始化或返现。
json::Value mfGreenBottleView(const Game& State);
}
namespace nz::api {
// 用途：严格解析绿瓶动作并委托统一领域命令；流程：API薄分发；返回：错误或空，不负责存档，失败不变状态。
std::string mfExecuteGreenBottleCommand(Game& State, RouteId Route, const json::Value& Input);
}
