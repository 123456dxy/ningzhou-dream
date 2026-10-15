#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/crafting/garden_rules.h"

namespace nz::persistence {
// 用途：编码新灵田规则及仅用于旧资产待返的事实；流程：保存；返回：拥有JSON，不返还资源。
Value mfGardenToJson(const CaveGardenState& Garden) {
    Value Result(json::Object{});
    Result.set("rulesVersion", num(Garden.RulesVersion)); Result.set("initialized", Value(Garden.Initialized));
    Result.set("lastGrowthDay", num(Garden.LastGrowthDay));
    Result.set("pendingHerbReturns", mapToJson(Garden.PendingHerbReturns));
    json::Array Plots;
    for (const auto& Plot : Garden.Plots) {
        Value Entry(json::Object{}); Entry.set("herbId", str(Plot.HerbId));
        Entry.set("legacyReserveStones", num(Plot.ReserveStones));
        json::Array Cohorts;
        for (const auto& Cohort : Plot.Cohorts) {
            Value Batch(json::Object{}); Batch.set("count", num(Cohort.Count)); Batch.set("progressUnits", num(Cohort.ProgressUnits));
            Cohorts.push_back(std::move(Batch));
        }
        Entry.set("cohorts", Value(std::move(Cohorts))); Plots.push_back(std::move(Entry));
    }
    Result.set("plots", Value(std::move(Plots))); return Result;
}
// 用途：将已校验JSON解到候选状态；流程：加载；返回：无，缺总字段为新版空默认，旧规则字段缺省1供显式迁移。
void mfGardenFromJson(const Value& Data, CaveGardenState& Garden) {
    Garden = CaveGardenState{};
    if (!Data.isObject()) return;
    Garden.RulesVersion = Data["rulesVersion"].asInt(1);
    Garden.Initialized = Data["initialized"].asBool(); Garden.LastGrowthDay = Data["lastGrowthDay"].asInt(-1);
    Garden.PendingHerbReturns = jsonToMap<int>(Data["pendingHerbReturns"]);
    for (const auto& Entry : Data["plots"].asArray()) {
        GardenPlot Plot; Plot.HerbId = Entry["herbId"].asString();
        Plot.ReserveStones = Entry["legacyReserveStones"].isNull() ? Entry["reserveStones"].asInt() : Entry["legacyReserveStones"].asInt();
        for (const auto& Batch : Entry["cohorts"].asArray()) Plot.Cohorts.push_back({Batch["count"].asInt(), Batch["progressUnits"].asInt()});
        Garden.Plots.push_back(std::move(Plot));
    }
}
// 用途：校验新旧灵田结构/整数范围；流程：候选加载之前；返回：合法真，禁止类型截断和非法版本。
bool mfGardenShapeValid(const Value& Data) {
    if (Data.isNull()) return true;
    if (!Data.isObject() || !Data["initialized"].isBool() || !saveInteger(Data["lastGrowthDay"], -1, 1000000000) ||
        !Data["plots"].isArray() || Data["plots"].asArray().size() > 8) return false;
    const auto Version = Data.asObject().find("rulesVersion");
    if (Version != Data.asObject().end() && !saveInteger(Version->second, 1, crafting::GardenRulesVersion)) return false;
    const bool IsModern = Data["rulesVersion"].asInt(1) >= crafting::GardenRulesVersion;
    const auto Pending = Data.asObject().find("pendingHerbReturns");
    if (Pending != Data.asObject().end()) {
        if (!Pending->second.isObject() || Pending->second.asObject().size() > 8) return false;
        for (const auto& [Name, Count] : Pending->second.asObject()) {
            if (Name.empty() || !saveInteger(Count, 1, 2376)) return false;
        }
    }
    for (const auto& Plot : Data["plots"].asArray()) {
        if (!Plot.isObject() || !Plot["herbId"].isString() || !Plot["cohorts"].isArray() ||
            Plot["cohorts"].asArray().size() > (IsModern ? 12u : 99u)) return false;
        for (const char* Key : {"reserveStones", "legacyReserveStones"}) {
            const auto Field = Plot.asObject().find(Key);
            if (Field != Plot.asObject().end() && !saveInteger(Field->second, 0, 1000000000)) return false;
        }
        if (Plot.asObject().count("reserveStones") && Plot.asObject().count("legacyReserveStones")) return false;
        for (const auto& Cohort : Plot["cohorts"].asArray())
            if (!Cohort.isObject() || !saveInteger(Cohort["count"], 1, IsModern ? 12 : 99) ||
                !saveInteger(Cohort["progressUnits"], 0, IsModern ? crafting::GardenMaturityUnits.back() : 96000)) return false;
    }
    return true;
}
}