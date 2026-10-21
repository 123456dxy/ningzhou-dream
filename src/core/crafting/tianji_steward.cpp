#include "core/crafting/tianji_steward.h"
#include "core/arts.h"
#include "core/content/forge_content.h"
#include "core/foundation/state_limits.h"
#include "core/sim.h"
#include <algorithm>
#include <cstdint>
#include <limits>

namespace nz::crafting {
namespace {
constexpr int MaximumPurchaseCount = 1000;
constexpr int MaterialPriceMultiplier = 3;
// 合法存档行情最高10，普通成丹售值最高约原料8.25倍（含逐份舍入）；12倍覆盖该边界，固定价不看实时行情。
constexpr int BreakthroughPillPriceMultiplier = 12;
int mfHeld(const map<string, int>& Inventory, const string& ItemId) {
    const auto Found = Inventory.find(ItemId);
    return Found == Inventory.end() ? 0 : Found->second;
}
int mfStablePillPrice(const data::RecipeDef& Recipe) {
    std::int64_t BaseCost = 0;
    for (const auto& [Name, Count] : parseIngredients(Recipe.ingredients)) {
        const auto* Herb = findHerbByName(Name);
        if (!Herb || Herb->price <= 0 || Count <= 0) return 0;
        BaseCost += static_cast<std::int64_t>(Herb->price) * Count;
    }
    const auto Price = BaseCost * BreakthroughPillPriceMultiplier;
    return Price > 0 && Price <= std::numeric_limits<int>::max() ? static_cast<int>(Price) : 0;
}
}
std::span<const TianjiOfferDefinition> mfTianjiOffers() {
    static const vector<TianjiOfferDefinition> Offers = [] {
        vector<TianjiOfferDefinition> Result;
        for (const auto& Recipe : data::RecipeDef_TABLE()) {
            if (Recipe.grade < 3 || Recipe.grade > 5 || Recipe.kind != "breakthrough" || !mfBreakthroughPillStage(Recipe.id)) continue;
            const int Price = mfStablePillPrice(Recipe);
            if (Price <= 0) continue;
            Result.push_back({"potion-" + Recipe.id, "potion", Recipe.id, Recipe.name,
                mfCompletePillEffect(Recipe).text + "；可提前购买，服用仍需对应突破境界。", Recipe.grade, Price, 0});
        }
        for (const auto& Herb : data::HerbDef_TABLE()) {
            if (Herb.grade < 3 || Herb.grade > 5) continue;
            Result.push_back({"herb-" + Herb.id, "herb", Herb.id, Herb.name, "高阶炼丹药材；固定价无限货架。",
                Herb.grade, Herb.price * MaterialPriceMultiplier, 0});
        }
        for (const auto& Material : content::mfForgeMaterials()) {
            if (Material.Source != content::ForgeMaterialSource::TianjiSteward || Material.AcquisitionPending) continue;
            Result.push_back({"material-" + Material.Material.id, "material", Material.Material.id, Material.Material.name,
                Material.Material.element + " · " + Material.Material.polarity + " · " + Material.FamilyName,
                Material.Material.grade, Material.Material.price * MaterialPriceMultiplier, 0});
        }
        return Result;
    }();
    return Offers;
}
const TianjiOfferDefinition* mfFindTianjiOffer(std::string_view OfferId) {
    for (const auto& Offer : mfTianjiOffers()) if (Offer.OfferId == OfferId) return &Offer;
    return nullptr;
}
TianjiShopQuote mfQuoteTianjiPurchase(const Game& State, const string& NpcId, const string& OfferId, int Count) {
    TianjiShopQuote Quote;
    Quote.NpcId = NpcId; Quote.OfferId = OfferId; Quote.Count = Count;
    const auto* Steward = social::mfFindTianjiSteward(NpcId);
    const auto* Offer = mfFindTianjiOffer(OfferId);
    if (!Steward) { Quote.Reason = "没有这位天机阁执事。"; return Quote; }
    if (!Offer) { Quote.Reason = "天机阁没有这件货品。"; return Quote; }
    Quote.FixedUnitPrice = Offer->FixedUnitPrice;
    const auto& PlayerState = State.player;
    const auto& Inventory = Offer->ItemKind == "potion" ? PlayerState.arts.potions
        : Offer->ItemKind == "herb" ? PlayerState.arts.herbs : PlayerState.arts.materials;
    const auto* Herb = Offer->ItemKind == "herb" ? findHerbById(Offer->ItemId) : nullptr;
    Quote.OwnedCount = mfHeld(Inventory, Herb ? Herb->name : Offer->ItemId);
    if (State.ended || PlayerState.health <= 0) Quote.Reason = "此世已结束或伤势过重。";
    else if (State.pending || State.job || PlayerState.arts.battle || State.dungeons.run ||
        (PlayerState.Zhuji && (PlayerState.Zhuji->Status == ZhujiStatus::Active || PlayerState.Zhuji->Status == ZhujiStatus::Preparing)))
        Quote.Reason = "请先完成当前安排、事件、斗法或秘境。";
    else if (PlayerState.location != Steward->Location) Quote.Reason = "请到执事驻守的天机阁分号当面交易。";
    else if (PlayerState.stage < Offer->RequiredStage) Quote.Reason = "尚未满足该商品的购买境界。";
    else if (Count < 1 || Count > MaximumPurchaseCount) Quote.Reason = "购买数量须为1至1000。";
    else if (Offer->FixedUnitPrice <= 0) Quote.Reason = "货架固定价格无效。";
    if (!Quote.Reason.empty()) return Quote;
    const std::int64_t TotalCost = static_cast<std::int64_t>(Offer->FixedUnitPrice) * Count;
    if (TotalCost > std::numeric_limits<int>::max()) { Quote.Reason = "购买总额超过支持范围。"; return Quote; }
    Quote.TotalCost = static_cast<int>(TotalCost);
    std::int64_t Capacity = static_cast<std::int64_t>(StateLimits::MaxPersistedInteger) - Quote.OwnedCount;
    if (Herb) {
        std::int64_t HerbCount = 0;
        for (const auto& [Name, Amount] : PlayerState.arts.herbs) { static_cast<void>(Name); HerbCount += Amount; }
        Capacity = std::min({Capacity, static_cast<std::int64_t>(StateLimits::MaxPersistedInteger) - HerbCount,
            static_cast<std::int64_t>(StateLimits::MaxPersistedInteger) - PlayerState.herbs});
    }
    Quote.MaxPurchaseCount = static_cast<int>(std::max<std::int64_t>(0, std::min({static_cast<std::int64_t>(MaximumPurchaseCount), Capacity,
        static_cast<std::int64_t>(PlayerState.stones / Offer->FixedUnitPrice)})));
    if (Capacity < Count) Quote.Reason = "背包已无法收存所选数量。";
    else if (PlayerState.stones < Quote.TotalCost) Quote.Reason = "灵石不足，无法购买所选数量。";
    Quote.Enabled = Quote.Reason.empty();
    return Quote;
}
string mfBuyTianjiOffer(Game& State, const string& NpcId, const string& OfferId, int Count, int ExpectedCost) {
    const auto Quote = mfQuoteTianjiPurchase(State, NpcId, OfferId, Count);
    if (!Quote.Enabled) return Quote.Reason;
    if (ExpectedCost != Quote.TotalCost) return "执事报价已过期，请刷新报价后重试。";
    const auto& Offer = *mfFindTianjiOffer(OfferId);
    Game Candidate = State;
    auto& PlayerState = Candidate.player;
    if (Offer.ItemKind == "potion") PlayerState.arts.potions[Offer.ItemId] += Count;
    else if (Offer.ItemKind == "herb") {
        PlayerState.arts.herbs[findHerbById(Offer.ItemId)->name] += Count;
        PlayerState.herbs += Count;
    } else PlayerState.arts.materials[Offer.ItemId] += Count;
    PlayerState.stones -= Quote.TotalCost;
    logEvent(Candidate, "self", "向天机阁执事" + social::mfFindTianjiSteward(NpcId)->Name + "购入" + Offer.Name + " ×" + std::to_string(Count) + "。",
        false, "", "灵石 −" + std::to_string(Quote.TotalCost));
    // 仅提交本动作实际修改的库存、金额和纪事。Game整体移动在MSVC下不保证noexcept；
    // map/vector的具体成员swap经编译期核验后，提交阶段不再分配或移动其他世界状态。
    auto& ActiveInventory = Offer.ItemKind == "potion" ? State.player.arts.potions
        : Offer.ItemKind == "herb" ? State.player.arts.herbs : State.player.arts.materials;
    auto& CandidateInventory = Offer.ItemKind == "potion" ? PlayerState.arts.potions
        : Offer.ItemKind == "herb" ? PlayerState.arts.herbs : PlayerState.arts.materials;
    static_assert(noexcept(ActiveInventory.swap(CandidateInventory)));
    static_assert(noexcept(State.logs.swap(Candidate.logs)));
    ActiveInventory.swap(CandidateInventory);
    State.logs.swap(Candidate.logs);
    State.player.stones = PlayerState.stones;
    State.player.herbs = PlayerState.herbs;
    State.eventSeq = Candidate.eventSeq;
    return {};
}
}
