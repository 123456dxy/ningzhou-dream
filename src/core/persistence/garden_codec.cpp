#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"

namespace nz::persistence {
// 用途：编码灵田完整批次/储备/日期；流程：保存；返回：独立JSON值，键保持小驼峰，不写游戏。
Value mfGardenToJson(const CaveGardenState& Garden) {
    Value Result(json::Object{}); Result.set("initialized", Value(Garden.Initialized)); Result.set("lastGrowthDay", num(Garden.LastGrowthDay));
    json::Array Plots;
    for (const auto& Plot : Garden.Plots) {
        Value Entry(json::Object{}); Entry.set("herbId", str(Plot.HerbId)); Entry.set("reserveStones", num(Plot.ReserveStones));
        json::Array Cohorts;
        for (const auto& Cohort : Plot.Cohorts) {
            Value Batch(json::Object{}); Batch.set("count", num(Cohort.Count)); Batch.set("progressUnits", num(Cohort.ProgressUnits));
            Cohorts.push_back(std::move(Batch));
        }
        Entry.set("cohorts", Value(std::move(Cohorts))); Plots.push_back(std::move(Entry));
    }
    Result.set("plots", Value(std::move(Plots))); return Result;
}
// 用途：解码已校验灵田到临时Game；流程：加载；返回：无，缺字段为合法空默认，不做历史生长。
void mfGardenFromJson(const Value& Data, CaveGardenState& Garden) {
    Garden = CaveGardenState{};
    if (!Data.isObject()) return;
    Garden.Initialized = Data["initialized"].asBool(); Garden.LastGrowthDay = Data["lastGrowthDay"].asInt(-1);
    for (const auto& Entry : Data["plots"].asArray()) {
        GardenPlot Plot; Plot.HerbId = Entry["herbId"].asString(); Plot.ReserveStones = Entry["reserveStones"].asInt();
        for (const auto& Batch : Entry["cohorts"].asArray()) Plot.Cohorts.push_back({Batch["count"].asInt(), Batch["progressUnits"].asInt()});
        Garden.Plots.push_back(std::move(Plot));
    }
}
// 用途：解码前校验灵田JSON类型/整数/数量上限；流程：加载结构验证；返回：合法为真，不做目录和日期语义修复。
bool mfGardenShapeValid(const Value& Data) {
    if (Data.isNull()) return true;
    if (!Data.isObject() || !Data["initialized"].isBool() || !saveInteger(Data["lastGrowthDay"], -1, 1000000000) ||
        !Data["plots"].isArray() || Data["plots"].asArray().size() > 8) return false;
    for (const auto& Plot : Data["plots"].asArray()) {
        if (!Plot.isObject() || !Plot["herbId"].isString() || !saveInteger(Plot["reserveStones"], 0, 1000000000) ||
            !Plot["cohorts"].isArray() || Plot["cohorts"].asArray().size() > 99) return false;
        for (const auto& Cohort : Plot["cohorts"].asArray())
            if (!Cohort.isObject() || !saveInteger(Cohort["count"], 1, 99) || !saveInteger(Cohort["progressUnits"], 0, 96000)) return false;
    }
    return true;
}
}
