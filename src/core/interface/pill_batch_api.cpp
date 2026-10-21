#include "core/interface/pill_batch_api.h"
#include "core/progression/pill_batch_rules.h"
#include <cmath>

namespace nz::api {
std::string mfReadPillBatchRequest(const json::Value& Input, std::string& RecipeId, int& Count) {
    if (!Input.isObject()) return "批量服丹需要JSON对象。";
    for (const auto& [Key, Value] : Input.asObject()) {
        static_cast<void>(Value);
        if (Key != "id" && Key != "count") return "未知批量服丹参数。";
    }
    if (!Input["id"].isString() || Input["id"].asString().empty()) return "必须提供丹药ID。";
    const auto& Number = Input["count"];
    if (!Number.isNumber() || !std::isfinite(Number.asNumber()) || Number.asNumber() != std::floor(Number.asNumber()) || Number.asNumber() < 1 || Number.asNumber() > MaxPillCountPerBatch)
        return "一次服用数量必须为1至1000的整数。";
    RecipeId = Input["id"].asString(); Count = Number.asInt(); return "";
}
json::Value mfPillBatchQuoteView(const Game& State, const std::string& RecipeId, int Count) {
    const auto Quote = mfQuotePillBatch(State, RecipeId, Count);
    json::Value Result(json::Object{});
    Result.set("enabled", Quote.Enabled); Result.set("reason", Quote.Reason);
    Result.set("id", Quote.RecipeId); Result.set("requestedCount", Quote.RequestedCount);
    Result.set("ownedCount", Quote.OwnedCount); Result.set("maxUsableCount", Quote.MaxUsableCount);
    Result.set("technicalLimit", MaxPillCountPerBatch); Result.set("stopReason", Quote.StopReason);
    Result.set("totalEffectValue", Quote.TotalEffectValue); Result.set("effect", Quote.Effect);
    Result.set("isPreparation", Quote.IsPreparation); Result.set("effectUnit", Quote.EffectUnit);
    Result.set("additionalDaoPoints", Quote.AdditionalDaoPoints);
    Result.set("poisonAfter", Quote.PoisonAfter); Result.set("poisonDelta", Quote.PoisonDelta);
    Result.set("usedAfter", Quote.UsedAfter);
    Result.set("toleranceRemainingAfter", Quote.ToleranceRemainingAfter);
    return Result;
}
std::string mfExecutePillBatch(Game& State, const json::Value& Input) {
    std::string RecipeId; int Count = 0;
    const auto Error = mfReadPillBatchRequest(Input, RecipeId, Count);
    return Error.empty() ? mfUsePillBatch(State, RecipeId, Count) : Error;
}
}
