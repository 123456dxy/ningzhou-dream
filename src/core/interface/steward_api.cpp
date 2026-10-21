#include "core/interface/steward_api.h"
#include "core/arts.h"
#include <cmath>
#include <limits>

namespace nz::api::view {
json::Value mfTianjiPurchaseQuoteView(const crafting::TianjiShopQuote& Quote) {
    json::Value Result(json::Object{});
    Result.set("enabled", Quote.Enabled); Result.set("reason", Quote.Reason);
    Result.set("npc", Quote.NpcId); Result.set("offer", Quote.OfferId); Result.set("count", Quote.Count);
    Result.set("fixedUnitPrice", Quote.FixedUnitPrice); Result.set("totalCost", Quote.TotalCost);
    Result.set("owned", Quote.OwnedCount); Result.set("maxPurchaseCount", Quote.MaxPurchaseCount);
    return Result;
}
json::Value mfTianjiStewardView(const Game& State, const string& NpcId, int Count) {
    json::Value Result(json::Object{});
    json::Array Stewards, Offers;
    string SelectedId = NpcId;
    for (const auto& Steward : social::mfTianjiStewards()) {
        json::Value Entry(json::Object{});
        Entry.set("id", Steward.Id); Entry.set("name", Steward.Name); Entry.set("location", Steward.Location);
        string LocationName = Steward.Location;
        for (const auto& Location : LOCATIONS()) if (Location.id == Steward.Location) { LocationName = Location.name; break; }
        Entry.set("locationName", LocationName); Entry.set("stage", Steward.Stage); Entry.set("realm", realmName(Steward.Stage));
        Entry.set("title", "天机阁驻城执事"); Entry.set("isHere", State.player.location == Steward.Location);
        if (SelectedId.empty() && State.player.location == Steward.Location) SelectedId = Steward.Id;
        Stewards.push_back(std::move(Entry));
    }
    const auto* Selected = social::mfFindTianjiSteward(SelectedId);
    for (const auto& Offer : crafting::mfTianjiOffers()) {
        json::Value Entry(json::Object{});
        Entry.set("id", Offer.OfferId); Entry.set("kind", Offer.ItemKind); Entry.set("itemId", Offer.ItemId);
        Entry.set("name", Offer.Name); Entry.set("description", Offer.Description); Entry.set("grade", Offer.Grade);
        Entry.set("requiredStage", Offer.RequiredStage); Entry.set("fixedUnitPrice", Offer.FixedUnitPrice);
        Entry.set("quote", mfTianjiPurchaseQuoteView(crafting::mfQuoteTianjiPurchase(State, SelectedId, Offer.OfferId, Count)));
        Offers.push_back(std::move(Entry));
    }
    Result.set("stewards", std::move(Stewards)); Result.set("offers", std::move(Offers)); Result.set("npc", SelectedId);
    Result.set("isHere", Selected && State.player.location == Selected->Location);
    Result.set("fixedPriceNote", "固定价无限货架；不受行情、声望、商贾或本地折扣影响。可提前购买，服丹境界照常校验。");
    Result.set("methodNote", "突破秘法尚未开放：效果与修习规则待定义，不出售无效果秘籍。");
    return Result;
}
}
namespace nz::api {
string mfReadTianjiPurchaseQuote(const Game& State, const json::Value& Input, json::Value& Output) {
    if (!Input.isObject()) return "执事报价需要JSON对象。";
    for (const auto& [Key, Value] : Input.asObject()) {
        static_cast<void>(Value);
        if (Key != "npc" && Key != "offer" && Key != "count") return "未知执事报价参数。";
    }
    if (!Input["npc"].isString() || !Input["offer"].isString()) return "执事与商品必须使用有效ID。";
    const auto& Count = Input["count"];
    if (!Count.isNumber() || !std::isfinite(Count.asNumber()) || Count.asNumber() != std::floor(Count.asNumber()) ||
        Count.asNumber() < 1 || Count.asNumber() > 1000) return "报价数量须为1至1000的整数。";
    if (!social::mfFindTianjiSteward(Input["npc"].asString())) return "没有这位天机阁执事。";
    if (!crafting::mfFindTianjiOffer(Input["offer"].asString())) return "天机阁没有这件货品。";
    Output = view::mfTianjiPurchaseQuoteView(crafting::mfQuoteTianjiPurchase(State,
        Input["npc"].asString(), Input["offer"].asString(), Count.asInt()));
    return {};
}
string mfExecuteTianjiStewardPurchase(Game& State, const json::Value& Input) {
    if (!Input.isObject()) return "执事购买需要JSON对象。";
    for (const auto& [Key, Value] : Input.asObject()) {
        static_cast<void>(Value);
        if (Key != "npc" && Key != "offer" && Key != "count" && Key != "expectedCost") return "未知执事购买参数。";
    }
    if (!Input["npc"].isString() || !Input["offer"].isString()) return "执事与商品必须使用有效ID。";
    for (const string Key : {"count", "expectedCost"}) {
        const auto& Value = Input[Key];
        if (!Value.isNumber() || !std::isfinite(Value.asNumber()) || Value.asNumber() != std::floor(Value.asNumber()) ||
            Value.asNumber() < (Key == "count" ? 1 : 0) || Value.asNumber() > (Key == "count" ? 1000 : std::numeric_limits<int>::max()))
            return "购买数量或报价金额无效。";
    }
    return crafting::mfBuyTianjiOffer(State, Input["npc"].asString(), Input["offer"].asString(), Input["count"].asInt(), Input["expectedCost"].asInt());
}
}
