#include "core/interface/craft_price_views.h"
#include "core/economy.h"
#include "core/shop.h"

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

json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote, const Game& State,
                           const std::string& Kind, const std::string& ItemId) {
    return mfCraftPriceView(Quote, State, Kind, ItemId, shop::mfMarketSaleContext(State));
}

json::Value mfCraftPriceView(const crafting::CraftPriceQuote& Quote, const Game& State,
                           const std::string& Kind, const std::string& ItemId,
                           const shop::MarketSaleContext& Context) {
    auto Result = mfCraftPriceView(Quote);
    const auto Availability = shop::mfQuoteMarketSale(State, Kind, ItemId, Quote.Valid ? Quote.SellPrice : 0, Context);
    json::Value Market(json::Object{});
    json::Array Locations;
    for (const auto* CityId : ECO_CITIES) {
        const auto* City = findLoc(CityId);
        json::Value Location(json::Object{});
        Location.set("id", CityId);
        Location.set("name", City ? City->name : CityId);
        Locations.push_back(std::move(Location));
    }
    Market.set("locations", json::Value(std::move(Locations)));
    Market.set("inCity", Availability.InCity);
    Market.set("monthBudget", Availability.NextMonthBudget);
    Market.set("remainingCash", Availability.RemainingCash);
    Market.set("nextMonthBudget", Availability.NextMonthBudget);
    Market.set("affordableCount", Availability.AffordableCount);
    Market.set("reason", Availability.Reason);
    Result.set("market", std::move(Market));
    return Result;
}

} // namespace nz::api::view
