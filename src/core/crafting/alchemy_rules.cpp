#include "core/crafting/alchemy_rules.h"
#include "core/arts.h"
#include "core/economy.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::crafting {
AlchemyQuote mfQuoteAlchemy(const Game& State, const string& RecipeId, int BatchCount) {
    AlchemyQuote Quote;
    Quote.RecipeId = RecipeId; Quote.BatchCount = BatchCount;
    if (BatchCount != 1 && BatchCount != 3 && BatchCount != 5 && BatchCount != 10) {
        Quote.Reason = "开炉次数必须为1、3、5或10炉。"; return Quote;
    }
    const auto* Recipe = findRecipe(RecipeId);
    if (!Recipe) { Quote.Reason = "无此丹方。"; return Quote; }
    const auto Brew = artBrewInfo(State.player, RecipeId);
    if (!Brew.valid) { Quote.Reason = "无此丹方。"; return Quote; }
    Quote.Valid = true; Quote.Name = "炼制" + Brew.recipeName;
    Quote.StoneCost = Brew.cost * BatchCount;
    Quote.DurationDays = ecoCraftDays(State.player, "brew", Recipe->grade) * BatchCount;
    Quote.SuccessProbability = Brew.chance; Quote.OutputPerSuccess = Brew.output;
    Quote.ExpectedOutput = Brew.chance * Brew.output * BatchCount;
    const auto& Player = State.player;
    const data::FurnaceDef* Furnace = nullptr;
    for (const auto& Entry : data::FurnaceDef_TABLE()) if (Entry.id == Player.arts.furnace) Furnace = &Entry;
    Quote.DurabilityCost = artHas(Player.arts, "丹", 4) && Furnace && Furnace->grade > Recipe->grade ? 0 : BatchCount;
    for (const auto& [HerbName, RequiredPerBatch] : parseIngredients(Recipe->ingredients)) {
        const auto Existing = std::find_if(Quote.Ingredients.begin(), Quote.Ingredients.end(),
            [&](const AlchemyIngredient& Ingredient) { return Ingredient.Name == HerbName; });
        if (Existing != Quote.Ingredients.end()) {
            Existing->Required += RequiredPerBatch * BatchCount;
            Existing->Missing = std::max(0, Existing->Required - Existing->Owned);
            continue;
        }
        AlchemyIngredient Ingredient;
        Ingredient.Name = HerbName;
        for (const auto& Herb : data::HerbDef_TABLE()) if (Herb.name == HerbName) Ingredient.Id = Herb.id;
        Ingredient.Required = RequiredPerBatch * BatchCount;
        const auto Owned = Player.arts.herbs.find(HerbName);
        Ingredient.Owned = Owned == Player.arts.herbs.end() ? 0 : Owned->second;
        Ingredient.Missing = std::max(0, Ingredient.Required - Ingredient.Owned);
        Quote.Ingredients.push_back(std::move(Ingredient));
    }
    if (State.ended) Quote.Reason = "这一世已经结束。";
    else if (State.pending) Quote.Reason = "请先处理当前事件。";
    else if (Player.arts.battle) Quote.Reason = "请先完成斗法。";
    else if (State.dungeons.run) Quote.Reason = "请先结束当前秘境探索。";
    else if (State.job) Quote.Reason = "请先完成或中止当前安排。";
    else if (Player.stage < 0 || Recipe->grade > artTier(Player)) Quote.Reason = "此境界只能炼制低阶丹药。";
    else if (!Furnace || Furnace->grade < Recipe->grade || Furnace->capacity < Brew.count) Quote.Reason = "丹炉品阶或容量不足。";
    else if (Player.arts.durability < 1 || Player.arts.durability < Quote.DurabilityCost) Quote.Reason = "丹炉耐久不足以完成整批。";
    else if (Player.stones < Quote.StoneCost) Quote.Reason = "本次行动灵石不足。";
    else for (const auto& Ingredient : Quote.Ingredients) {
        if (Ingredient.Id.empty() || Ingredient.Missing > 0) {
            Quote.Reason = "指定药材不足以完成整批：" + Ingredient.Name + "需 " + std::to_string(Ingredient.Required) + " 份。"; break;
        }
    }
    Quote.Enabled = Quote.Reason.empty();
    return Quote;
}
}
