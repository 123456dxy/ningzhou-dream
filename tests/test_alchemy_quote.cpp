#include "harness.h"
#include "core/crafting/alchemy_rules.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
void mfRunAlchemyQuoteTests() {
    using namespace nz;
    CreateOptions Options; Options.seed = 20261018; Options.traitsExplicit = true;
    auto State = createGame(Options); State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.stage = State.player.highestStage = 12;
    State.player.stones = 1000000; State.player.arts.durability = 40;
    const auto& Recipe = data::RecipeDef_TABLE().front();
    State.player.arts.furnace = data::FurnaceDef_TABLE().back().id;
    for (const auto& [Name, Required] : parseIngredients(Recipe.ingredients)) State.player.arts.herbs[Name] = Required * 10;
    const auto Before = saveToJson(State);
    const auto Quote = crafting::mfQuoteAlchemy(State, Recipe.id, 3);
    checkStr(saveToJson(State), Before, "整批炼丹报价无存档、随机或时钟变化");
    check(Quote.Enabled, "完整材料可炼三炉");
    const int StonesBefore = State.player.stones, DurabilityBefore = State.player.arts.durability;
    ecoBegin(State, "brew", Recipe.id, 3);
    check(State.job.has_value(), "真实开炉创建冻结任务");
    checkEq(StonesBefore - State.player.stones, Quote.StoneCost, "报价与整批真实灵石扣除一致");
    checkEq(DurabilityBefore - State.player.arts.durability, Quote.DurabilityCost, "报价与整批耐久扣除一致");
    if (State.job) {
        checkEq(State.job->total, Quote.DurationDays, "总耗时冻结自同一报价");
        checkNear(State.job->chance, Quote.SuccessProbability, "成丹概率冻结一致");
        checkEq(State.job->craftOutput, Quote.OutputPerSuccess, "成功产出冻结一致");
    }
    for (const auto& Ingredient : Quote.Ingredients)
        checkEq(State.player.arts.herbs.at(Ingredient.Name), Ingredient.Owned - Ingredient.Required, "开炉一次投入本批真实材料");
    State.job.reset();
    State.player.arts.herbs.clear();
    const auto Missing = crafting::mfQuoteAlchemy(State, Recipe.id, 3);
    check(!Missing.Enabled, "缺料整批报价阻止开炉");
    const auto MissingBefore = saveToJson(State);
    ecoBegin(State, "brew", Recipe.id, 3);
    checkStr(saveToJson(State), MissingBefore, "过期报价缺料时不部分扣灵石或材料");
    for (const auto& Ingredient : Quote.Ingredients) State.player.arts.herbs[Ingredient.Name] = Ingredient.Required;
    check(crafting::mfQuoteAlchemy(State, Recipe.id, 3).Enabled, "补料后恢复可炼资格");
    check(!crafting::mfQuoteAlchemy(State, Recipe.id, 2).Valid, "非白名单炉数拒绝");
    check(!crafting::mfQuoteAlchemy(State, "invalid", 1).Valid, "非法丹方拒绝");
    for (const auto& [Name, Required] : parseIngredients(findRecipe("5223")->ingredients)) State.player.arts.herbs[Name] = 100;
    State.player.arts.herbs["灯心草"] = 2;
    const auto DuplicateQuote = crafting::mfQuoteAlchemy(State, "5223", 1);
    check(!DuplicateQuote.Enabled, "丹方中重复药材先合计需求再校验库存");
    const auto DuplicateBefore = saveToJson(State);
    ecoBegin(State, "brew", "5223", 1);
    checkStr(saveToJson(State), DuplicateBefore, "重复药材不足不会部分开工或扣为负数");
    State.dungeons.run.emplace();
    const auto DungeonBefore = saveToJson(State);
    const auto DungeonQuote = crafting::mfQuoteAlchemy(State, Recipe.id, 1);
    check(!DungeonQuote.Enabled && DungeonQuote.Reason.find("秘境") != string::npos, "秘境进行中炼丹领域报价明确拒绝开炉");
    ecoBegin(State, "brew", Recipe.id, 1);
    checkStr(saveToJson(State), DungeonBefore, "秘境进行中直调开炉不扣材料或灵石");
}
}
