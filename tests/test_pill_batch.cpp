#include "harness.h"
#include "core/progression/pill_batch_rules.h"
#include "core/interface/pill_batch_api.h"
#include "core/arts.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"
#include <limits>
#include <type_traits>

namespace nztest {
namespace {
nz::Game mfPillBatchGame() {
    nz::CreateOptions Options; Options.seed = 20261018; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.logs.clear(); State.npcs.clear();
    State.player.arts = nz::Arts{};
    State.player.stage = State.player.highestStage = 12;
    State.player.traits.clear(); State.player.health = 1;
    return State;
}
}
void mfRunPillBatchTests() {
    using namespace nz;
    static_assert(std::is_same_v<decltype(PillBatchQuote{}.TotalEffectValue), double>);
    for (int Scenario = 0; Scenario < 3; ++Scenario) {
        auto RejectedSingle = mfPillBatchGame();
        RejectedSingle.player.arts.potions["5001"] = Scenario == 0 ? 0 : 2;
        if (Scenario == 1) RejectedSingle.player.arts.poison = 80;
        if (Scenario == 2) RejectedSingle.player.arts.used["5001"] = findRecipe("5001")->tolerance;
        const auto SingleBefore = saveToJson(RejectedSingle);
        const auto SinglePreview = artPillUseInfo(RejectedSingle.player, *findRecipe("5001"));
        check(!SinglePreview.reason.empty(), "缺货、丹毒或耐药的单枚预览明确拒绝");
        checkStr(mfQuotePillBatch(RejectedSingle, "5001", 1).Reason, SinglePreview.reason, "单枚批量报价与既有单枚预览原因完全一致");
        checkStr(artUsePill(RejectedSingle, "5001"), SinglePreview.reason, "旧单枚入口保留原拒绝文案");
        checkStr(mfUsePillBatch(RejectedSingle, "5001", 1), SinglePreview.reason, "批量入口count1不附加批量失败前缀");
        check(mfUsePillBatch(RejectedSingle, "5001", 2).starts_with("本批无法完整服用："), "多枚拒绝保留整批失败说明");
        checkStr(saveToJson(RejectedSingle), SingleBefore, "单枚与多枚拒绝均不改变任何状态");
    }
    auto State = mfPillBatchGame(); State.player.arts.potions["5001"] = 100;
    const auto Before = saveToJson(State);
    const auto Quote = mfQuotePillBatch(State, "5001", 3);
    checkStr(saveToJson(State), Before, "批量服丹报价无状态与随机副作用");
    check(Quote.Enabled, "三枚同种丹药可完整报价");
    auto Sequential = State;
    for (int DoseIndex = 0; DoseIndex < 3; ++DoseIndex)
        check(artApplyPill(Sequential.player, "5001").empty(), "逐枚参考结算成功");
    check(mfUsePillBatch(State, "5001", 3).empty(), "单次批量提交成功");
    checkEq(State.logs.size(), 1, "整批只记一条纪事");
    checkEq(Quote.PoisonAfter, State.player.arts.poison, "本批报价丹毒与真实结算一致");
    checkEq(State.eventSeq, Sequential.eventSeq + 1, "单条批量纪事仅推进一个纪事序号");
    auto BatchSnapshot = json::Value::parse(saveToJson(State));
    auto SequentialSnapshot = json::Value::parse(saveToJson(Sequential));
    // artApplyPill is intentionally log-free; the batch command records one
    // event. Exclude only that journal and its counter, retaining every other
    // saved field (including RNG, clock, inventory and the full player state).
    for (const char* Key : {"logs", "eventSeq"}) {
        BatchSnapshot.asObject().erase(Key);
        SequentialSnapshot.asObject().erase(Key);
    }
    checkStr(BatchSnapshot.dump(), SequentialSnapshot.dump(), "批量结果逐枚一致（仅排除纪事及序号）");
    State.player.arts.poison = 79;
    const auto Limit = mfQuotePillBatch(State, "5001", 2);
    checkEq(Limit.MaxUsableCount, 1, "丹毒中途到限时最多可服一枚");
    const auto AtLimit = saveToJson(State);
    check(!mfUsePillBatch(State, "5001", 2).empty(), "超出中途资格整笔拒绝");
    checkStr(saveToJson(State), AtLimit, "拒绝不部分扣丹或增加丹毒");
    for (const int Count : {0, -1, 1001, std::numeric_limits<int>::max()}) {
        check(!mfUsePillBatch(State, "5001", Count).empty(), "非法数量明确拒绝");
        checkStr(saveToJson(State), AtLimit, "非法数量无副作用");
    }
    auto Tolerance = mfPillBatchGame(); Tolerance.player.arts.potions["5209"] = 100;
    const auto* Recipe = findRecipe("5209");
    Tolerance.player.arts.used[Recipe->id] = Recipe->tolerance - 1;
    const auto ToleranceBefore = saveToJson(Tolerance);
    checkEq(mfQuotePillBatch(Tolerance, Recipe->id, 2).MaxUsableCount, 1, "逐枚耐药限制最大数量");
    check(!mfUsePillBatch(Tolerance, Recipe->id, 2).empty(), "耐药不足整笔拒绝");
    checkStr(saveToJson(Tolerance), ToleranceBefore, "耐药不足未提交候选状态");
    auto Stock = mfPillBatchGame(); Stock.player.arts.potions["5001"] = 1;
    const auto StockBefore = saveToJson(Stock);
    check(!mfUsePillBatch(Stock, "5001", 2).empty(), "过期库存整笔拒绝");
    checkStr(saveToJson(Stock), StockBefore, "库存变化不部分提交");
    auto Overflow = mfPillBatchGame(); Overflow.player.arts.potions["5414"] = 1;
    Overflow.player.arts.points = StateLimits::MaxPersistedInteger;
    const auto OverflowBefore = saveToJson(Overflow);
    check(!mfUsePillBatch(Overflow, "5414", 1).empty(), "真实药效超出持久数值上限时拒绝");
    checkStr(saveToJson(Overflow), OverflowBefore, "真实药效越界不部分消耗库存");
    auto Detox = mfPillBatchGame(); Detox.player.arts.potions["5111"] = 1001;
    check(mfQuotePillBatch(Detox, "5111", 1000).MaxUsableCount <= MaxPillCountPerBatch, "大库存服丹仍受技术和耐药上限约束");
    check(mfUsePillBatch(Detox, "5111", 2).empty(), "零收益化毒丹保持原可服规则");
    checkEq(Detox.player.arts.potions["5111"], 999, "零收益批量仍真实扣除所选数量");
    auto DetoxUnits = mfPillBatchGame(); DetoxUnits.player.arts.potions["5111"] = 2;
    DetoxUnits.player.arts.poison = 25;
    const auto DetoxQuote = mfQuotePillBatch(DetoxUnits, "5111", 2);
    checkNear(DetoxQuote.TotalEffectValue, -20, "化毒汇总保留真实有符号变化");
    check(DetoxQuote.Effect.find("丹毒 -20点") != string::npos, "化毒批量报价显示丹毒点数单位");
    check(mfUsePillBatch(DetoxUnits, "5111", 2).empty(), "有收益化毒批量实际服用");
    check(DetoxUnits.logs.back().text.find("丹毒 -20点") != string::npos, "化毒批量纪事保留用途和单位");
    auto LifeUnits = mfPillBatchGame(); LifeUnits.player.arts.potions["5119"] = 2;
    LifeUnits.player.lifespan = 1000; LifeUnits.player.PillLifeBonus = 0;
    const auto LifeQuote = mfQuotePillBatch(LifeUnits, "5119", 2);
    check(LifeQuote.Effect.find("寿元 +20年") != string::npos && LifeQuote.EffectUnit == "年", "寿元批量报价显示真实年数");
    check(mfUsePillBatch(LifeUnits, "5119", 2).empty(), "寿元批量实际服用");
    checkEq(LifeUnits.player.lifespan, 1020, "寿元有单位报价与真实增加年数一致");
    auto DaoUnits = mfPillBatchGame(); DaoUnits.player.arts.potions["5407"] = 2;
    const auto DaoQuote = mfQuotePillBatch(DaoUnits, "5407", 2);
    check(DaoQuote.AdditionalDaoPoints > 0 && DaoQuote.Effect.find("跨级获得悟道点") != string::npos, "大道经验报价保留额外跨级悟道点");
    const int PointsBefore = DaoUnits.player.arts.points;
    check(mfUsePillBatch(DaoUnits, "5407", 2).empty(), "大道经验批量实际服用");
    checkEq(DaoUnits.player.arts.points - PointsBefore, DaoQuote.AdditionalDaoPoints, "跨级悟道点报价与实际一致");
    auto GoldenPreparation = mfPillBatchGame(); GoldenPreparation.player.stage = 5;
    GoldenPreparation.player.apt = GoldenPreparation.player.insight = 0;
    GoldenPreparation.player.pills = GoldenPreparation.player.cores = 0;
    GoldenPreparation.player.arts.potions["5220"] = 2;
    const auto GoldenQuote = mfQuotePillBatch(GoldenPreparation, "5220", 1);
    check(GoldenQuote.Enabled && GoldenQuote.IsPreparation && GoldenQuote.Effect.find("+6个百分点") != string::npos,
        "凝金丹单枚报价显示最终突破准备六个百分点，不称零实际药效");
    const double ChanceBefore = breakthroughChance(GoldenPreparation);
    check(mfUsePillBatch(GoldenPreparation, "5220", 2).empty(), "凝金丹同类批量覆盖准备成功");
    checkNear(breakthroughChance(GoldenPreparation) - ChanceBefore, 0.06, "凝金丹真实最终准备提升六个百分点且同类不叠加");
    check(GoldenPreparation.logs.back().text.find("+6个百分点") != string::npos && GoldenPreparation.logs.back().text.find("最终突破准备") != string::npos,
        "多枚突破丹纪事描述最终真实准备");
    auto CombatPreparation = mfPillBatchGame(); CombatPreparation.player.arts.potions["5005"] = 2;
    const auto CombatQuote = mfQuotePillBatch(CombatPreparation, "5005", 2);
    check(CombatQuote.Effect.find("最终战斗准备") != string::npos && CombatQuote.Effect.find("同类覆盖不叠加") != string::npos,
        "战斗丹报价说明最终准备而非无单位合计");
    auto DungeonState = mfPillBatchGame(); DungeonState.player.arts.potions["5001"] = 1;
    DungeonState.dungeons.run.emplace();
    const auto DungeonBefore = saveToJson(DungeonState);
    check(!mfQuotePillBatch(DungeonState, "5001", 1).Enabled && !mfUsePillBatch(DungeonState, "5001", 1).empty(), "秘境进行中领域报价和直调动作均拒绝服丹");
    checkStr(saveToJson(DungeonState), DungeonBefore, "秘境阻塞不改变角色或秘境状态");
    auto Breakthrough = mfPillBatchGame();
    for (const auto& Entry : data::RecipeDef_TABLE()) if (mfBreakthroughPillStage(Entry.id) == 2) {
        Breakthrough.player.stage = 2; Breakthrough.player.arts.potions[Entry.id] = 2;
        check(mfQuotePillBatch(Breakthrough, Entry.id, 2).Enabled, "同类突破准备沿用覆盖不叠加的原有资格");
        check(mfUsePillBatch(Breakthrough, Entry.id, 2).empty(), "同类突破准备真实逐枚结算");
        checkEq(Breakthrough.player.arts.BreakthroughPillStage, 2, "突破准备仍是同一个目标阶段");
        checkEq(Breakthrough.player.arts.poison, 8, "两枚突破丹逐枚累计丹毒");
        break;
    }
    std::string RecipeId; int Count = 0;
    for (const char* Input : {"{\"id\":\"5001\",\"count\":1.5}", "{\"id\":\"5001\",\"count\":2147483648}", "{\"id\":\"5001\",\"count\":true}", "{\"id\":\"5001\",\"count\":1,\"extra\":0}"})
        check(!api::mfReadPillBatchRequest(json::Value::parse(Input), RecipeId, Count).empty(), "HTTP数量类型、溢出和未知键拒绝");
}
}
