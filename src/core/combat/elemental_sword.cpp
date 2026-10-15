#include "core/combat/elemental_sword.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/content/game_ids.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace nz::ElementalSword {
namespace {
inline constexpr std::array<std::string_view, 5> ElementalSwordIds{
    "metalSword", "waterSword", "woodSword", "fireSword", "earthSword"};
}

// 用途：将封闭的五剑ID映射为身份；流程：双方共享战斗；返回：白名单成员判定，无状态副作用。
bool mfIsElementalSword(const SpellDef& Spell) {
    return std::find(ElementalSwordIds.begin(), ElementalSwordIds.end(), Spell.id) != ElementalSwordIds.end();
}
// 用途：以稳定大道枚举识别有效五行牌；流程：双方相生轨迹；返回：非五行牌为假，不依赖名称或效果类型。
bool mfIsElementalSpell(const SpellDef& Spell) {
    const auto Dao = content::parseDaoId(Spell.dao);
    return Dao && (*Dao == content::DaoId::Metal || *Dao == content::DaoId::Wood ||
        *Dao == content::DaoId::Water || *Dao == content::DaoId::Fire || *Dao == content::DaoId::Earth);
}
// 用途：判定持久五行轨迹是否相生；流程：伤害/状态/返灵共用；返回：纯布尔，保留既有木火复合印规则。
bool mfHasGeneratingCombo(const Battle& State, const SpellDef& Spell) {
    const auto Current = content::parseDaoId(Spell.dao);
    const auto Previous = content::parseDaoId(State.last);
    return (Previous && Current && content::generates(*Previous, *Current)) ||
        (State.last == "木火" && Current == content::DaoId::Fire);
}
// 用途：保存五行剑独立三剑进度；流程：真实双方及只读预演；返回：接链真，错序以当前剑为第一剑，非剑五行牌终止剑链，中性牌透明。
bool mfAdvanceSwordChain(Battle& State, const SpellDef& Spell) {
    if (!mfIsElementalSpell(Spell)) return false;
    if (!mfIsElementalSword(Spell)) { State.ElementalSwordSequence = 0; return false; }
    const bool HasCombo = State.ElementalSwordSequence > 0 && mfHasGeneratingCombo(State, Spell);
    State.ElementalSwordSequence = HasCombo ? State.ElementalSwordSequence % SwordChainCycleLength + 1 : 1;
    return HasCombo;
}

// 用途：相生接链积累本场强化；流程：双方真实施术与隔离预览；返回：无，错序或中性牌不清空已得到的强化。
void mfAdvanceEnhancement(Battle& State, const ArtMods& Modifiers, bool HasSwordCombo) {
    if (HasSwordCombo && Modifiers.ElementalSwordMastery > 0)
        State.ElementalSwordEnhancementLayers = std::min(MaximumEnhancementLayers, State.ElementalSwordEnhancementLayers + 1);
}

// 用途：将有效功法层数和本场强化换算成有界倍率；流程：真实效果与只读展示；返回：1至2，只放大相生附加收益。
double mfEnhancementMultiplier(const ArtMods& Modifiers, const Battle& State) {
    return 1.0 + std::clamp(Modifiers.ElementalSwordMastery, 0.0, 1.0) * EnhancementPerMastery *
        std::clamp(State.ElementalSwordEnhancementLayers, 0, MaximumEnhancementLayers);
}

// 用途：在基础相生之上放大五剑额外部分；流程：双方施法及预览；返回：未相生为1，非五剑维持原倍率。
double mfGenerationDamageMultiplier(const ArtMods& Modifiers, const Battle& State,
                                    const SpellDef& Spell, bool HasGeneratingCombo) {
    if (!HasGeneratingCombo) return 1.0;
    const double Enhancement = mfIsElementalSword(Spell) ? mfEnhancementMultiplier(Modifiers, State) : 1.0;
    return 1.0 + (0.15 + std::min(0.15, Modifiers.combo)) * Enhancement;
}

// 用途：表达火剑相生的额外段；流程：减伤护盾前原始伤害；返回：仅25%额外部分吃强化，不重复结算护盾。
double mfDamageMultiplier(const SpellDef& Spell, bool HasGeneratingCombo, double EnhancementMultiplier) {
    return Spell.id == "fireSword" && HasGeneratingCombo
        ? 1.0 + (FireComboDamageMultiplier - 1.0) * EnhancementMultiplier : 1.0;
}

// 用途：兑现水剑剑气与五行剑诀连续相生资源；流程：真实施法与只读副本；返回：无，真实资源与计数同步。
void mfApplyResources(const Player& PlayerState, const ArtMods& Modifiers, Battle& State,
                      const SpellDef& Spell, bool HasSwordCombo) {
    if (Spell.id == "waterSword") State.swordQi = std::min(8, State.swordQi + 1);
    if (!HasSwordCombo || Modifiers.ElementalSwordMastery <= 0) return;
    const int ManaGain = jsFloor(Modifiers.ElementalSwordMastery);
    if (ManaGain > 0 && State.ElementalSwordManaReturns < ManaReturnLimitPerTurn) {
        State.mana = std::min(battleManaCap(PlayerState), State.mana + ManaGain);
        ++State.ElementalSwordManaReturns;
    }
    if (State.ElementalSwordSequence == SwordChainCycleLength && State.comboDraws < DrawLimitPerTurn) {
        ++State.pendingDraw;
        ++State.comboDraws;
    }
}

// 用途：按命中后事实处理木剑与土剑身份效果；流程：双方真实命中及预览；返回：无，吸血按有效损血且守势当轮消散。
void mfApplyHitBenefits(Battle& State, const SpellDef& Spell, bool HasGeneratingCombo, int ActualHealthLoss,
                        double EnhancementMultiplier) {
    if (Spell.id == "woodSword" && State.hp > 0)
        State.hp = std::min(State.max, State.hp + jsRound(std::max(0, ActualHealthLoss) * WoodLifeStealFraction *
            (HasGeneratingCombo ? EnhancementMultiplier : 1.0)));
    if (Spell.id == "earthSword" && HasGeneratingCombo)
        State.guard = std::min(TemporaryGuardCap, State.guard + EarthComboGuardGain * EnhancementMultiplier);
}

// 用途：按真实五剑参数解释相生、剑气、吸血和守势；流程：后端预览说明；返回：纯文案，规则不从文字反推。
std::string mfRulesDescription(const SpellDef& Spell, const ArtMods& Modifiers) {
    if (!mfIsElementalSword(Spell)) return "";
    std::string Description = "五行剑顺序：土→金→水→木→火→土；进度可跨回合，穿插气、神、普通攻击/防御等非五行牌不打断；剑气仍封顶8层。";
    if (Spell.id == "waterSword") Description += "水剑额外获得1层剑气。";
    if (Spell.id == "woodSword") Description += "木剑按目标实际气血损失的" + std::to_string(jsRound(WoodLifeStealFraction * 100)) + "%吸血；盾挡和溢出伤害不吸血。";
    if (Spell.id == "fireSword") Description += "火剑相生时追加" + std::to_string(jsRound((FireComboDamageMultiplier - 1) * 100)) + "%直接伤害。";
    if (Spell.id == "earthSword") Description += "土剑相生时增加" + std::to_string(jsRound(EarthComboGuardGain * 100)) + "%当轮守势，下一轮消散。";
    if (Modifiers.ElementalSwordMastery > 0) {
        Description += "五行剑诀：五剑成功相生立即获得1层本场相生强化（本次即生效），最多" + std::to_string(MaximumEnhancementLayers) +
            "层；当前功法使每层强化提高" + std::to_string(jsRound(std::min(1.0, Modifiers.ElementalSwordMastery) * EnhancementPerMastery * 100)) +
            "%相生附加收益，仅强化五剑相生增伤、火剑追加伤害、木剑相生吸血与土剑守势，不放大整段基础伤害、剑气、返灵或抽牌。强化跨回合保留，新战斗归零；功法不额外送剑气。返灵" + std::to_string(jsFloor(Modifiers.ElementalSwordMastery)) +
            "点（每轮最多" + std::to_string(ManaReturnLimitPerTurn) + "次）；每相生第3剑补1张，所有相生补牌每轮最多2张。有效五行错序重开剑链；非剑五行牌结束剑链，中性牌透明。";
    }
    return Description;
}
} // namespace nz::ElementalSword
