#pragma once
#include "core/json.h"
#include "core/model.h"
namespace nz::api {
// 用途：汇总地图秘境与爬塔状态预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value dungeonView(const Game& g);
// 用途：汇总洞府等级配给与升档门槛。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value caveView(const Game& g);
// 用途：汇总可恢复的手动和十年检查点。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value checkpointView(const Game& g);
}
