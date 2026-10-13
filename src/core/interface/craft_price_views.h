#pragma once

#include "core/json.h"
#include "core/crafting/craft_prices.h"

namespace nz::api::view {

// 用途：序列化制作经济权威报价；流程：坊市、工坊和法器行囊视图。
// 参数：仅调用期借用完整报价；返回：拥有内容的JSON，不保存借用。
// 副作用：无；不读取游戏、RNG、日历或文件；调用方负责快照稳定性。
[[nodiscard]] json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote);

} // namespace nz::api::view
