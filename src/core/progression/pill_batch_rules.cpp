#include "core/progression/pill_batch_rules.h"
#include "core/arts.h"
#include "core/sim.h"
#include "core/foundation/state_limits.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace nz {
namespace {
string mfPillBatchAccessError(const Game& State) {
    if (State.ended) return "这一世已经结束。";
    if (State.pending) return "请先处理当前事件。";
    if (State.player.arts.battle) return "请先完成斗法。";
    if (State.dungeons.run) return "请先结束当前秘境探索。";
    if (State.job) return "请先完成或中止当前安排。";
    return "";
}
string mfApplyBatchDose(Player& Candidate, const data::RecipeDef& Recipe, double& TotalEffectValue) {
    const auto Info = artPillUseInfo(Candidate, Recipe);
    if (!Info.reason.empty()) return Info.reason;
    const auto Used = Candidate.arts.used.find(Recipe.id);
    if (Used != Candidate.arts.used.end() && Used->second >= StateLimits::MaxPersistedInteger)
        return "服丹次数超出可保存的整数范围。";
    const double Total = TotalEffectValue + Info.effectValue;
    if (!std::isfinite(Info.effectValue) || !std::isfinite(Total) || Total < -StateLimits::MaxPersistedInteger || Total > StateLimits::MaxPersistedInteger)
        return "本批药效超出可保存的整数范围。";
    const string Error = artApplyPill(Candidate, Recipe.id);
    if (Error.empty() && (Candidate.arts.points < 0 || Candidate.arts.points > StateLimits::MaxPersistedInteger))
        return "服丹后的悟道点超出可保存的整数范围。";
    if (Error.empty()) TotalEffectValue = Total;
    return Error;
}
string mfBatchEffectUnit(const data::RecipeDef& Recipe) {
    if (Recipe.kind == "breakthrough" || Recipe.kind == "combat") return "准备";
    return Recipe.kind == "life" ? "年" : "点";
}
string mfSignedEffectValue(double Value) {
    std::ostringstream Text;
    if (Value >= 0) Text << '+';
    Text << std::setprecision(15) << Value;
    return Text.str();
}
string mfDescribeBatchEffect(const Player& Before, const Player& After,
                            const data::RecipeDef& Recipe, double TotalEffectValue) {
    // Preparation effects overwrite one shared preparation slot. The original
    // rule's description expresses that final benefit; adding numeric deltas
    // would incorrectly advertise zero or multiply the same preparation.
    if (Recipe.kind == "breakthrough")
        return "本批最终突破准备：" + artPillUseInfo(Before, Recipe).effect;
    if (Recipe.kind == "combat")
        return "本批最终战斗准备：" + artPillUseInfo(Before, Recipe).effect +
            "；已准备 " + std::to_string(After.arts.CombatPillDoseCount) + "/3枚，同类覆盖不叠加";
    string Label;
    if (Recipe.kind == "xp") Label = "修为";
    else if (Recipe.kind == "heal") Label = "气血";
    else if (Recipe.kind == "detox") Label = "丹毒";
    else if (Recipe.kind == "points") Label = "悟道点";
    else if (Recipe.kind == "dao") Label = Recipe.dao + "道经验";
    else if (Recipe.kind == "life") Label = "寿元";
    else if (Recipe.kind == "apt") Label = "资质";
    else if (Recipe.kind == "insight") Label = "悟性";
    else if (Recipe.kind == "body") Label = "气血上限";
    else if (Recipe.kind == "heart") Label = "心境";
    else if (Recipe.kind == "mind") Label = "神识";
    else if (Recipe.kind == "speed") Label = "额外遁速";
    else return artPillUseInfo(Before, Recipe).effect;
    string Effect = "本批实际" + Label + " " + mfSignedEffectValue(TotalEffectValue) + mfBatchEffectUnit(Recipe);
    if (Recipe.kind == "dao")
        Effect += "；跨级获得悟道点 " + mfSignedEffectValue(static_cast<double>(After.arts.points) - Before.arts.points) + "点";
    return Effect;
}
}
PillBatchQuote mfQuotePillBatch(const Game& State, const string& RecipeId, int Count) {
    PillBatchQuote Quote;
    Quote.RecipeId = RecipeId; Quote.RequestedCount = Count;
    Quote.PoisonAfter = State.player.arts.poison;
    const auto* Recipe = findRecipe(RecipeId);
    if (!Recipe) { Quote.Reason = "没有这张丹方。"; return Quote; }
    Quote.IsPreparation = Recipe->kind == "breakthrough" || Recipe->kind == "combat";
    Quote.EffectUnit = mfBatchEffectUnit(*Recipe);
    const auto Owned = State.player.arts.potions.find(RecipeId);
    Quote.OwnedCount = Owned == State.player.arts.potions.end() ? 0 : Owned->second;
    Quote.Reason = mfPillBatchAccessError(State);
    if (!Quote.Reason.empty()) return Quote;
    if (Count < 1 || Count > MaxPillCountPerBatch) {
        Quote.Reason = "一次服用数量必须为1至1000的整数。"; return Quote;
    }
    Player Candidate = State.player;
    double TotalEffectValue = 0;
    const int Limit = std::min(MaxPillCountPerBatch, std::max(0, Quote.OwnedCount));
    for (int DoseIndex = 0; DoseIndex < Limit; ++DoseIndex) {
        const string Error = mfApplyBatchDose(Candidate, *Recipe, TotalEffectValue);
        if (!Error.empty()) { Quote.StopReason = Error; break; }
        ++Quote.MaxUsableCount;
        if (Quote.MaxUsableCount == Count) {
            Quote.TotalEffectValue = TotalEffectValue;
            Quote.PoisonAfter = Candidate.arts.poison;
            Quote.PoisonDelta = Candidate.arts.poison - State.player.arts.poison;
            Quote.UsedAfter = Candidate.arts.used.at(RecipeId);
            Quote.ToleranceRemainingAfter = artPillUseInfo(Candidate, *Recipe).toleranceRemaining;
            Quote.AdditionalDaoPoints = Recipe->kind == "dao" ? Candidate.arts.points - State.player.arts.points : 0;
            Quote.Effect = mfDescribeBatchEffect(State.player, Candidate, *Recipe, TotalEffectValue) +
                "；服后丹毒 " + std::to_string(Quote.PoisonAfter) + "点";
        }
    }
    if (Quote.StopReason.empty()) Quote.StopReason = Limit == MaxPillCountPerBatch
        ? "一次最多1000枚。" : artPillUseInfo(Candidate, *Recipe).reason;
    Quote.Enabled = Count <= Quote.MaxUsableCount;
    if (!Quote.Enabled) Quote.Reason = Count == 1 ? Quote.StopReason : "本批无法完整服用：" + Quote.StopReason;
    return Quote;
}
string mfUsePillBatch(Game& State, const string& RecipeId, int Count) {
    if (Count < 1 || Count > MaxPillCountPerBatch) return "一次服用数量必须为1至1000的整数。";
    const string AccessError = mfPillBatchAccessError(State);
    if (!AccessError.empty()) return AccessError;
    const auto* Recipe = findRecipe(RecipeId);
    if (!Recipe) return "没有这张丹方。";
    Player Candidate = State.player;
    const auto FirstDose = artPillUseInfo(Candidate, *Recipe);
    double TotalEffectValue = 0;
    for (int DoseIndex = 0; DoseIndex < Count; ++DoseIndex) {
        const string Error = mfApplyBatchDose(Candidate, *Recipe, TotalEffectValue);
        if (!Error.empty()) return Count == 1 ? Error : "本批无法完整服用：" + Error;
    }
    const int PoisonDelta = Candidate.arts.poison - State.player.arts.poison;
    const string Effect = Count == 1 ? FirstDose.effect : mfDescribeBatchEffect(State.player, Candidate, *Recipe, TotalEffectValue);
    State.player = std::move(Candidate);
    logEvent(State, "self", "服下" + Recipe->name + (Count == 1 ? "" : " ×" + std::to_string(Count)) +
        "：" + Effect, true, "",
        "丹毒 " + string(PoisonDelta >= 0 ? "+" : "") + std::to_string(PoisonDelta));
    return "";
}
}
