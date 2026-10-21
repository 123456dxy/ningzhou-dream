#pragma once
#include "core/json.h"
#include "core/model.h"
#include "core/crafting/tianji_steward.h"

namespace nz::api::view {
// 所有执事资料/当前分号货架/指定执事与数量的报价；纯读取，无初始化或存档副作用。
[[nodiscard]] json::Value mfTianjiStewardView(const Game& State, const string& NpcId = "", int Count = 1);
[[nodiscard]] json::Value mfTianjiPurchaseQuoteView(const crafting::TianjiShopQuote& Quote);
}
namespace nz::api {
// 严格解析只读报价 {npc,offer,count}；无动作副作用。错误时保持Output不变。
[[nodiscard]] string mfReadTianjiPurchaseQuote(const Game& State, const json::Value& Input, json::Value& Output);
// 严格解析 {npc,offer,count,expectedCost}，拒绝未知参数与非整数金额；成功保存由现有API边界执行。
[[nodiscard]] string mfExecuteTianjiStewardPurchase(Game& State, const json::Value& Input);
}
