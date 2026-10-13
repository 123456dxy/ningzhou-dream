#pragma once

#include "core/json.h"
#include "core/crafting/craft_prices.h"
#include "core/shop.h"

namespace nz::api::view {

// 用途：序列化制作经济权威报价；流程：坊市、工坊和法器行囊视图。
// 参数：仅调用期借用完整报价；返回：拥有内容的JSON，不保存借用。
// 副作用：无；不读取游戏、RNG、日历或文件；调用方负责快照稳定性。
[[nodiscard]] json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote);

// 用途：追加与真实出售共用的五城收购能力；临时法器允许空ID，不检查未炼成物的拥有量。
// 参数：只读游戏与完整后端报价；返回：含market的报价JSON；无活动状态/时间/RNG/文件副作用。
[[nodiscard]] json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote, const Game& State,
                                          const std::string& Kind, const std::string& ItemId);
// 用途：目录同次响应复用预算上下文，避免每条价格重复扫描整个丹方目录；不得跨响应缓存。
[[nodiscard]] json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote, const Game& State,
                                          const std::string& Kind, const std::string& ItemId,
                                          const shop::MarketSaleContext& Context);

} // namespace nz::api::view
