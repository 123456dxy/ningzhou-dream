#pragma once
#include "core/json.h"
#include "core/exploration/service_navigation.h"
#include "core/crafting/material_supply.h"
namespace nz::api::view {
// 用途：编码权威路线；参与服务/材料报价HTTP响应；传纯值，返回前端协议JSON。
json::Value mfServiceRouteView(const Exploration::ServiceRouteQuote& Quote);
// 用途：追加坊市/情报导航；参与完整state；传只读人生和输出对象，返回无，不改变游戏。
void mfAppendServiceRoutes(const Game& State, json::Value& View);
// 用途：合并缺料报价和适用供应路线；参与既有工坊接口；返回报价JSON，不创建新路由或动作。
json::Value mfMaterialSupplyView(const Game& State, const crafting::MaterialQuote& Materials, const string& CraftKind);
}
