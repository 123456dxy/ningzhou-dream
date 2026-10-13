#include "core/interface/craft_price_views.h"

namespace nz::api::view {

// 用途：输出基价、最终价和额外加成分项；流程：只读价格展示。
// 使用：传领域报价；返回：可复制JSON对象，失败报价保留明确原因，不推测有效价格。
json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote) {
    json::Value Result(json::Object{});
    Result.set("valid", Quote.Valid);
    Result.set("materialCost", Quote.MaterialCost);
    Result.set("standardOutput", Quote.StandardOutput);
    Result.set("baseSellPrice", Quote.BaseSellPrice);
    Result.set("buyPrice", Quote.BuyPrice);
    Result.set("sellPrice", Quote.SellPrice);
    Result.set("saleMultiplier", Quote.SaleMultiplier);
    Result.set("inscriptionMultiplier", Quote.InscriptionMultiplier);
    Result.set("renownSaleMultiplier", Quote.RenownSaleMultiplier);
    Result.set("craftedSaleMultiplier", Quote.CraftedSaleMultiplier);
    Result.set("estimatedMaterials", Quote.UsesEstimatedMaterials);
    Result.set("priceReason", Quote.Reason);
    return Result;
}

} // namespace nz::api::view
