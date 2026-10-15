#include "core/interface/green_bottle_api.h"
#include "core/crafting/green_bottle.h"
#include "core/crafting/garden_rules.h"
#include <algorithm>
#include <cmath>

namespace nz::api::view {
// 用途：编码绿瓶当前/下一品阶和统一报价；流程：洞府纯视图；返回：拥有JSON，不扣费/生成site/改变日历或存档。
json::Value mfGreenBottleView(const Game& State) {
    json::Value Result(json::Object{});
    const bool HasBottle = hasTalent(State.player, "greenBottle");
    Result.set("hasTalent", HasBottle);
    if (!HasBottle) return Result;
    const int Current = State.player.arts.GreenBottleGrade;
    const int Natural = crafting::GardenNaturalDailyUnits[clampInt(State.player.caveTier, 0, 2)];
    Result.set("currentGrade", Current); Result.set("currentDailyBonus", crafting::mfGreenBottleDailyBonus(State.player));
    Result.set("naturalUnitsPerDay", Natural); Result.set("currentUnitsPerDay", Natural + crafting::mfGreenBottleDailyBonus(State.player));
    const int Next = std::min(6, Current + 1);
    Result.set("nextGrade", Next); Result.set("nextDailyBonus", crafting::GreenBottleDailyUnits[clampInt(Next, 0, 6)]);
    Result.set("nextUnitsPerDay", Natural + crafting::GreenBottleDailyUnits[clampInt(Next, 0, 6)]);
    const auto Quote = crafting::mfQuoteGreenBottleUpgrade(State);
    json::Value Upgrade(json::Object{});
    Upgrade.set("enabled", Quote.Enabled); Upgrade.set("reason", Quote.Reason);
    Upgrade.set("targetGrade", Quote.TargetGrade); Upgrade.set("stoneCost", Quote.StoneCost); Upgrade.set("ownedStones", Quote.OwnedStones);
    json::Array Materials;
    for (const auto& Material : Quote.Materials) {
        json::Value Entry(json::Object{}); Entry.set("id", Material.Id); Entry.set("name", Material.Name);
        Entry.set("required", Material.Required); Entry.set("owned", Material.Owned); Entry.set("missing", Material.Missing);
        Materials.push_back(std::move(Entry));
    }
    Upgrade.set("materials", std::move(Materials));
    const auto Before = crafting::mfGreenBottleMaturityDays(State.player.caveTier, clampInt(Current, 0, 6));
    const auto After = crafting::mfGreenBottleMaturityDays(State.player.caveTier, clampInt(Next, 0, 6));
    json::Array BeforeView, AfterView;
    for (std::size_t Index = 0; Index < Before.size(); ++Index) {
        json::Value BeforeRow(json::Object{}); BeforeRow.set("grade", static_cast<int>(Index + 1)); BeforeRow.set("days", Before[Index]);
        json::Value AfterRow(json::Object{}); AfterRow.set("grade", static_cast<int>(Index + 1)); AfterRow.set("days", After[Index]);
        BeforeView.push_back(std::move(BeforeRow)); AfterView.push_back(std::move(AfterRow));
    }
    Upgrade.set("maturityDaysBefore", std::move(BeforeView)); Upgrade.set("maturityDaysAfter", std::move(AfterView));
    Result.set("upgrade", std::move(Upgrade));
    json::Array Dungeons;
    for (const auto& QuoteEntry : crafting::mfGreenBottleDungeonOptions(State)) {
        json::Value Entry(json::Object{}); Entry.set("grade", QuoteEntry.Grade); Entry.set("name", QuoteEntry.Name);
        Entry.set("materialId", QuoteEntry.MaterialId); Entry.set("materialName", QuoteEntry.MaterialName);
        Entry.set("rewardCount", QuoteEntry.RewardCount); Entry.set("minimumBottleGrade", QuoteEntry.MinimumBottleGrade);
        Entry.set("enabled", QuoteEntry.Enabled); Entry.set("reason", QuoteEntry.Reason);
        Dungeons.push_back(std::move(Entry));
    }
    Result.set("dungeons", std::move(Dungeons));
    return Result;
}
}
namespace nz::api {
// 用途：验证机器字段及过期阶位后委托领域命令；流程：绿瓶POST；返回：错误或空，严格拒绝未知键/类型/越界。
std::string mfExecuteGreenBottleCommand(Game& State, RouteId Route, const json::Value& Input) {
    if (!Input.isObject()) return "绿瓶动作需要JSON对象。";
    for (const auto& [Key, Value] : Input.asObject()) {
        static_cast<void>(Value);
        if (Key != "expectedGrade" && !(Route == RouteId::CaveGreenBottleDungeon && Key == "grade")) return "未知绿瓶动作参数。";
    }
    const auto& Expected = Input["expectedGrade"];
    if (!Expected.isNumber() || !std::isfinite(Expected.asNumber()) || Expected.asNumber() != std::floor(Expected.asNumber()) || Expected.asNumber() < 0 || Expected.asNumber() > 6)
        return "必须提供0至6的当前小绿瓶阶位。";
    if (Route == RouteId::CaveGreenBottleUpgrade) return crafting::mfUpgradeGreenBottle(State, Expected.asInt());
    if (Route != RouteId::CaveGreenBottleDungeon) return "未知绿瓶动作。";
    const auto& Grade = Input["grade"];
    if (!Grade.isNumber() || !std::isfinite(Grade.asNumber()) || Grade.asNumber() != std::floor(Grade.asNumber()) || Grade.asNumber() < 4 || Grade.asNumber() > 6)
        return "专属灵材秘境品阶必须为4至6。";
    return crafting::mfEnterGreenBottleDungeon(State, Grade.asInt(), Expected.asInt());
}
}
