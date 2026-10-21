#include "core/interface/alchemy_api.h"
#include <cmath>

namespace nz::api {
json::Value mfAlchemyQuoteView(const crafting::AlchemyQuote& Quote) {
    json::Value Result(json::Object{});
    Result.set("enabled", Quote.Enabled); Result.set("reason", Quote.Reason);
    Result.set("id", Quote.RecipeId); Result.set("batchCount", Quote.BatchCount);
    Result.set("stoneCost", Quote.StoneCost); Result.set("durationDays", Quote.DurationDays);
    Result.set("durabilityCost", Quote.DurabilityCost); Result.set("successProbability", Quote.SuccessProbability);
    Result.set("outputPerSuccess", Quote.OutputPerSuccess); Result.set("expectedOutput", Quote.ExpectedOutput);
    json::Array Ingredients;
    for (const auto& Ingredient : Quote.Ingredients) {
        json::Value Entry(json::Object{});
        Entry.set("id", Ingredient.Id); Entry.set("name", Ingredient.Name);
        Entry.set("required", Ingredient.Required); Entry.set("owned", Ingredient.Owned); Entry.set("missing", Ingredient.Missing);
        Ingredients.push_back(std::move(Entry));
    }
    Result.set("ingredients", std::move(Ingredients)); return Result;
}
std::string mfReadAlchemyQuote(const Game& State, const json::Value& Input, json::Value& Output) {
    if (!Input.isObject()) return "炼丹报价需要JSON对象。";
    for (const auto& [Key, Value] : Input.asObject()) {
        static_cast<void>(Value); if (Key != "id" && Key != "count") return "未知炼丹报价参数。";
    }
    if (!Input["id"].isString() || Input["id"].asString().empty()) return "必须提供丹方ID。";
    const auto& Count = Input["count"];
    if (!Count.isNumber() || !std::isfinite(Count.asNumber()) || Count.asNumber() != std::floor(Count.asNumber()) || (Count.asNumber() != 1 && Count.asNumber() != 3 && Count.asNumber() != 5 && Count.asNumber() != 10)) return "开炉次数必须为1、3、5或10炉。";
    Output = mfAlchemyQuoteView(crafting::mfQuoteAlchemy(State, Input["id"].asString(), Count.asInt()));
    return "";
}
}
