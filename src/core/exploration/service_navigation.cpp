#include "core/exploration/service_navigation.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/dungeon.h"
#include "core/travel.h"
#include <algorithm>
#include <tuple>

namespace nz::Exploration {
namespace {
// 用途：比较同一目标的可用方式；参与导航报价；先耗时后费用和稳定ID，返回确定性顺序，不掷随机数。
bool mfPreferTravel(const TravelOption& First, const TravelOption& Second) {
    return std::tie(First.days, First.cost, First.id) < std::tie(Second.days, Second.cost, Second.id);
}
// 用途：把现有 travelPlan 压为单条路线；参与最近服务选择；传稳定目标ID，返回含实际耗时费用的只读结果。
ServiceRouteQuote mfQuoteDestination(const Game& State, const string& Destination) {
    ServiceRouteQuote Quote;
    Quote.Destination = Destination;
    const auto* Location = findLoc(Destination);
    if (!Location) { Quote.Reason = "服务地点无效。"; return Quote; }
    Quote.DestinationName = Location->name;
    const auto Plan = travelPlan(State, Destination);
    Quote.Distance = Plan.distance;
    Quote.AlreadyHere = State.player.location == Destination;
    if (State.dungeons.run) { Quote.Reason = "请先结束当前秘境探索。"; return Quote; }
    const TravelOption* Best = nullptr;
    for (const auto& Option : Plan.options) {
        if (Option.enabled && (!Best || mfPreferTravel(Option, *Best))) Best = &Option;
    }
    if (Best) {
        Quote.Enabled = true;
        Quote.Mode = Best->id;
        Quote.Days = Best->days;
        Quote.Cost = Best->cost;
    } else if (Quote.AlreadyHere) Quote.Reason = "已在服务地点。";
    else if (!Plan.options.empty()) Quote.Reason = Plan.options.front().reason;
    else Quote.Reason = "无法规划服务路线。";
    return Quote;
}
// 用途：在真正供应服务的候选城市间选择；参与坊市导航；传候选稳定ID，返回最近距离、耗时、费用确定排序的路线。
ServiceRouteQuote mfNearestMarket(const Game& State, const vector<string>& Cities) {
    ServiceRouteQuote Best;
    bool Found = false;
    for (const auto& City : Cities) {
        auto Candidate = mfQuoteDestination(State, City);
        Candidate.TargetTab = "market";
        if (!Found || (Candidate.Enabled || Candidate.AlreadyHere) > (Best.Enabled || Best.AlreadyHere) ||
            ((Candidate.Enabled || Candidate.AlreadyHere) == (Best.Enabled || Best.AlreadyHere) &&
             std::tie(Candidate.Distance, Candidate.Days, Candidate.Cost, Candidate.Destination) <
             std::tie(Best.Distance, Best.Days, Best.Cost, Best.Destination))) {
            Best = std::move(Candidate);
            Found = true;
        }
    }
    if (!Found) Best.Reason = "当前没有可供应所选材料的城市。";
    return Best;
}
}

// 用途：报价最近服务点；参与完整状态和异地按钮；返回已有交通规则的结果，展示不推进活动或消耗随机数。
ServiceRouteQuote mfQuoteServiceRoute(const Game& State, ServiceDestination Service) {
    if (Service == ServiceDestination::Intel) {
        auto Quote = mfQuoteDestination(State, "tianji");
        Quote.TargetTab = "dungeon";
        Quote.TargetSub = "intel";
        return Quote;
    }
    vector<string> Cities;
    for (const auto& Location : LOCATIONS()) if (ecoIsCity(Location.id)) Cities.emplace_back(Location.id);
    return mfNearestMarket(State, Cities);
}

// 用途：根据实际城市货单或天机情报目录匹配缺料；参与工坊一键补料导航；返回地点建议，不改变报价资格和采购门槛。
ServiceRouteQuote mfQuoteMaterialRoute(const Game& State, const crafting::MaterialQuote& Materials, const string& CraftKind) {
    ServiceRouteQuote Quote;
    if (!Materials.valid || (CraftKind != "brew" && CraftKind != "forge")) {
        Quote.Reason = "请选择有效的炼丹或炼器配方。";
        return Quote;
    }
    const bool HasMissing = std::any_of(Materials.materials.begin(), Materials.materials.end(),
        [](const auto& Material) { return Material.missing > 0; });
    if (!HasMissing) { Quote.Reason = "材料已备齐，无需前往购买。"; return Quote; }

    // 只复制采购报价需要的角色、行情和经济；不拷贝NPC或执行全游戏初始化。
    Game MarketState;
    MarketState.player = State.player;
    MarketState.world = State.world;
    MarketState.economy = State.economy;
    vector<string> Suppliers;
    for (const auto& Location : LOCATIONS()) {
        if (!ecoIsCity(Location.id)) continue;
        MarketState.player.location = Location.id;
        const auto Catalog = play::marketList(MarketState);
        const bool CanSupply = std::all_of(Materials.materials.begin(), Materials.materials.end(), [&](const auto& Material) {
            return Material.missing <= 0 || std::any_of(Catalog.begin(), Catalog.end(), [&](const auto& Item) {
                return Item.kind == Material.kind && Item.id == Material.id && Item.price > 0;
            });
        });
        if (CanSupply) Suppliers.emplace_back(Location.id);
    }
    if (!Suppliers.empty()) {
        Quote = mfNearestMarket(State, Suppliers);
        Quote.TargetTab = "workshop";
        Quote.TargetSub = CraftKind;
        return Quote;
    }
    const auto Intel = dungeonIntelOptions(State);
    const bool HasIntel = std::any_of(Materials.materials.begin(), Materials.materials.end(), [&](const auto& Material) {
        return Material.missing > 0 && std::any_of(Intel.begin(), Intel.end(), [&](const auto& Item) {
            return Item.kind == Material.kind && Item.id == Material.id;
        });
    });
    if (HasIntel) {
        Quote = mfQuoteServiceRoute(State, ServiceDestination::Intel);
        if (Quote.Enabled) Quote.Reason = "高阶缺料需购买材料秘境位置，通关后获取，不能直接购齐。";
        return Quote;
    }
    Quote.Reason = "当前境界的城市货单不供应所选缺料，请提升购买资格或从探索、交易取得。";
    return Quote;
}
} // namespace nz::Exploration
