#include "core/interface/cave_views.h"
#include "core/interface/view_helpers.h"
#include "core/interface/view_policy.h"
#include "core/interface/routes.h"
#include "core/crafting/cave_garden.h"
#include "core/economy.h"
#include "core/shop.h"
#include "core/travel.h"
#include <algorithm>

namespace nz::api::view {
namespace {
// 用途：编码灵田命令报价；流程：按钮权限与操作预览；返回：独立JSON，不执行报价动作。
json::Value mfGardenQuoteView(const crafting::GardenActionQuote& Quote) {
    json::Value Result(json::Object{});
    Result.set("enabled", Quote.Enabled); Result.set("reason", Quote.Reason);
    Result.set("seedCount", Quote.SeedCount); Result.set("harvestCount", Quote.HarvestCount);
    Result.set("transferStones", Quote.TransferStones); Result.set("discardCount", Quote.DiscardCount);
    return Result;
}
// 用途：编码灵田可复制快照；流程：洞府页的分批成长、种植和储备展示；返回：完整JSON；资格复用真实命令的纯报价，不推进世界日。
json::Value mfGardenView(const Game& State) {
    const auto Info = crafting::mfGardenInfo(State);
    json::Value Result(json::Object{});
    Result.set("plotCountUnlocked", Info.PlotCountUnlocked);
    Result.set("plantsPerPlotLimit", Info.PlantsPerPlotLimit);
    Result.set("harvestMultiplier", Info.HarvestMultiplier);
    Result.set("hasGreenBottle", Info.HasGreenBottle);
    Result.set("accessReason", Info.AccessReason); Result.set("growthCostRule", Info.GrowthCostRule);
    json::Array Options;
    for (const auto& Option : Info.PlantOptions) {
        json::Value Item(json::Object{});
        Item.set("herbId", Option.HerbId); Item.set("name", Option.Name); Item.set("grade", Option.Grade);
        Item.set("ownedCount", Option.OwnedCount); Item.set("baseGrowthDays", Option.BaseGrowthDays);
        Options.push_back(std::move(Item));
    }
    Result.set("plantOptions", std::move(Options));
    json::Array Plots;
    for (const auto& Plot : Info.Plots) {
        json::Value Item(json::Object{});
        Item.set("plotId", Plot.PlotId); Item.set("herbId", Plot.HerbId); Item.set("name", Plot.Name);
        Item.set("grade", Plot.Grade); Item.set("seedCount", Plot.SeedCount);
        Item.set("growingCount", Plot.GrowingCount); Item.set("readyCount", Plot.ReadyCount);
        Item.set("reserveStones", Plot.ReserveStones); Item.set("requiredUnits", Plot.RequiredUnits);
        Item.set("naturalUnitsPerDay", Plot.NaturalUnitsPerDay);
        Item.set("spiritUnitsPerDay", Plot.SpiritUnitsPerDay); Item.set("bottleUnitsPerDay", Plot.BottleUnitsPerDay);
        Item.set("fullyAcceleratedUnitsPerDay", Plot.FullyAcceleratedUnitsPerDay);
        Item.set("effectiveUnitsPerDay", Plot.EffectiveUnitsPerDay);
        Item.set("spiritDailyCostStones", Plot.SpiritDailyCostStones);
        Item.set("bottleDailyCostStones", Plot.BottleDailyCostStones); Item.set("dailyCostStones", Plot.DailyCostStones);
        Item.set("estimatedNaturalDays", Plot.EstimatedNaturalDays);
        Item.set("estimatedSpiritDays", Plot.EstimatedSpiritDays); Item.set("estimatedBottleDays", Plot.EstimatedBottleDays);
        Item.set("estimatedFullyAcceleratedDays", Plot.EstimatedFullyAcceleratedDays);
        Item.set("estimatedEffectiveDays", Plot.EstimatedEffectiveDays); Item.set("accelerationFunded", Plot.AccelerationFunded);
        Item.set("remainingCapacity", std::max(0, Info.PlantsPerPlotLimit - Plot.SeedCount));
        crafting::GardenActionQuote PlantQuote;
        PlantQuote.Reason = Info.AccessReason.empty() ? "没有可种植的药材，或本格已满。" : Info.AccessReason;
        for (const auto& Option : Info.PlantOptions) {
            if (!Plot.HerbId.empty() && Option.HerbId != Plot.HerbId) continue;
            const auto Candidate = crafting::mfQuoteGardenPlant(State, Plot.PlotId, Option.HerbId, 1);
            if (Candidate.Enabled) { PlantQuote = Candidate; break; }
            if (Option.OwnedCount > 0) PlantQuote = Candidate;
        }
        Item.set("plantEnabled", PlantQuote.Enabled); Item.set("plantReason", PlantQuote.Reason);
        Item.set("deposit", mfGardenQuoteView(crafting::mfQuoteGardenReserve(State, Plot.PlotId, 1, true)));
        Item.set("withdraw", mfGardenQuoteView(crafting::mfQuoteGardenReserve(State, Plot.PlotId, 1, false)));
        Item.set("harvest", mfGardenQuoteView(Plot.Harvest)); Item.set("clear", mfGardenQuoteView(Plot.Clear));
        json::Array Cohorts;
        for (const auto& Cohort : Plot.Cohorts) {
            json::Value Row(json::Object{}); Row.set("count", Cohort.Count);
            Row.set("progressUnits", Cohort.ProgressUnits); Row.set("ready", Cohort.Ready);
            Cohorts.push_back(std::move(Row));
        }
        Item.set("cohorts", std::move(Cohorts)); Plots.push_back(std::move(Item));
    }
    Result.set("plots", std::move(Plots)); return Result;
}
} // namespace

// 用途：追加洞府页面的访问条件及训练/灵田快照；流程：state响应；返回：无，只改Cave输出，查询没有副作用。
void mfAppendCaveDetails(const Game& State, json::Value& Cave) {
    const bool AtHome = !State.player.cave.empty() && State.player.cave == State.player.location;
    const auto* Home = findLoc(State.player.cave);
    const auto Reason = crafting::mfGardenAccessError(State);
    if (!Reason.empty() && Cave["upgrade"].isObject()) {
        auto Upgrade = Cave["upgrade"];
        Upgrade.set("enabled", false); Upgrade.set("reason", Reason); Cave.set("upgrade", std::move(Upgrade));
    }
    json::Value Access(json::Object{});
    Access.set("atHome", AtHome); Access.set("homeLocation", State.player.cave);
    Access.set("homeName", Home ? std::string(Home->name) : std::string{}); Access.set("reason", Reason);
    const auto ReturnPlan = Home ? travelPlan(State, State.player.cave) : TravelPlan{};
    const TravelOption* ReturnOption = nullptr;
    for (const auto* Mode : {"walk", "boat", "flight"}) {
        const auto* Candidate = travelOption(ReturnPlan, Mode);
        if (Candidate && Candidate->enabled) { ReturnOption = Candidate; break; }
    }
    const auto TravelBlock = actionBlockReason(State, routePath(RouteId::Travel));
    Access.set("canReturn", Home && !AtHome && ReturnOption && TravelBlock.empty());
    Access.set("returnMode", ReturnOption ? ReturnOption->id : std::string{});
    Access.set("returnName", ReturnOption ? ReturnOption->name : std::string{});
    Access.set("returnDays", ReturnOption ? ReturnOption->days : 0);
    Access.set("returnCost", ReturnOption ? ReturnOption->cost : 0);
    Access.set("returnReason", !TravelBlock.empty() ? TravelBlock : ReturnOption ? std::string{} : "没有可用的返府路线，请在舆图查看交通条件。");
    const auto SettleQuote = shop::mfQuoteCaveMove(State);
    json::Value Settle(json::Object{});
    Settle.set("enabled", SettleQuote.enabled); Settle.set("reason", SettleQuote.reason);
    Settle.set("costStones", SettleQuote.costStones); Access.set("settle", std::move(Settle));
    Cave.set("access", std::move(Access));
    const auto Training = ecoActionInfo(State, "arena", "");
    const auto TrainingError = ecoActionError(State, "arena", "");
    json::Value Arena(json::Object{});
    Arena.set("id", "arena"); Arena.set("name", "演武切磋"); Arena.set("desc", "增进剑道与斗法心得");
    Arena.set("days", Training.days); Arena.set("cost", Training.cost);
    Arena.set("enabled", Training.valid && TrainingError.empty()); Arena.set("reason", TrainingError);
    Cave.set("training", json::Array{std::move(Arena)});
    Cave.set("garden", mfGardenView(State));
}
} // namespace nz::api::view
