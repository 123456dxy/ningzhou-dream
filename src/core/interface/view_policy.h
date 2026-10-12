#pragma once
#include "core/model.h"
#include "core/sim.h"

namespace nz::api::view {
// Exact action guard order, shared by commands and authoritative previews.
// 用途：按路由策略检查秘境结局战斗事件工作阻塞。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const std::string& path，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string actionBlockReason(const Game& game, const std::string& path);
// 用途：检查突破修为灵石伤势及全局阻塞。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const BreakthroughInfo& info，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string breakthroughReason(const Game& game, const BreakthroughInfo& info);
}
