#pragma once
#include <string>

namespace nz {
struct SpellDef;
struct Player;
struct Battle;
struct ArtMods;

namespace ElementalSword {
inline constexpr int ManaReturnLimitPerTurn = 3;
inline constexpr int DrawLimitPerTurn = 2;
inline constexpr int SwordChainCycleLength = 3;
inline constexpr double MasteryPerManualLevel = 0.2;
inline constexpr int MaximumEnhancementLayers = 5;
inline constexpr double EnhancementPerMastery = 0.2;
inline constexpr double WoodLifeStealFraction = 0.25;
inline constexpr double FireComboDamageMultiplier = 1.25;
inline constexpr double EarthComboGuardGain = 0.05;
inline constexpr double TemporaryGuardCap = 0.20;

// 用途：集中白名单识别五属性剑；流程：真实效果、说明及构筑；返回：是否为注册的五剑ID，不按展示文字推规则。
[[nodiscard]] bool mfIsElementalSword(const SpellDef& Spell);
// 用途：识别会推进/替换五行轨迹的有效五行牌；流程：双方连击；返回：金木水火土为真，气/神/普通牌等保持透明。
[[nodiscard]] bool mfIsElementalSpell(const SpellDef& Spell);
// 用途：读取最近有效五行与当前牌的相生关系；流程：真实效果/预览共用；返回：纯判定，跨回合保留的轨迹不被中性牌覆盖。
[[nodiscard]] bool mfHasGeneratingCombo(const Battle& State, const SpellDef& Spell);
// 用途：推进独立五行剑三剑周期；流程：双方真实施术/预览副本；返回：本剑是否接上相生剑链；中性牌不重置，有效五行错序照常重开。
[[nodiscard]] bool mfAdvanceSwordChain(Battle& State, const SpellDef& Spell);
// 用途：积累本场相生强化；流程：双方真实施术与预览副本；使用：传已确认的五剑接链事实；返回：无，有效功法下加一层，最多五层，跨回合保留。
void mfAdvanceEnhancement(Battle& State, const ArtMods& Modifiers, bool HasSwordCombo);
// 用途：读取功法修习决定的相生附加收益倍率；流程：伤害、吸血、守势及界面共用；返回：1至2，主修五层每强化层+20%，辅修减半。
[[nodiscard]] double mfEnhancementMultiplier(const ArtMods& Modifiers, const Battle& State);
// 用途：计算通用相生直伤倍率；流程：真实施术与预览；使用：仅白名单五剑放大相生额外部分；返回：基础伤害不被强化重复乘算。
[[nodiscard]] double mfGenerationDamageMultiplier(const ArtMods& Modifiers, const Battle& State,
                                                  const SpellDef& Spell, bool HasGeneratingCombo);
// 用途：查询火剑相生额外直伤倍率；流程：共享伤害路径；返回：1+25%乘相生强化倍率，其余为1，不修改状态。
[[nodiscard]] double mfDamageMultiplier(const SpellDef& Spell, bool HasGeneratingCombo, double EnhancementMultiplier = 1.0);
// 用途：兑现水剑固有剑气及五行剑诀的返灵与补牌；流程：双方真实施术和预览副本，功法本身不再送剑气。
// 使用：只在白名单五剑调用，HasSwordCombo要求最近有效五行招也是相生剑，允许跨回合/穿插中性牌；返回：无，回灵3次/轮、补牌共享2张/轮。
void mfApplyResources(const Player& PlayerState, const ArtMods& Modifiers, Battle& State,
                      const SpellDef& Spell, bool HasSwordCombo);
// 用途：结算木剑实际吸血和土剑当轮守势；流程：真实命中与预览。
// 使用：ActualHealthLoss只传被击中目标仍有气血的实际损失；返回：无，不复活反震致死的施术者。
void mfApplyHitBenefits(Battle& State, const SpellDef& Spell, bool HasGeneratingCombo, int ActualHealthLoss,
                        double EnhancementMultiplier = 1.0);
// 用途：展示五剑与有效功法的条件说明；流程：卡牌/目录/试验场；返回：由同一参数生成的中文说明，不推进状态。
[[nodiscard]] std::string mfRulesDescription(const SpellDef& Spell, const ArtMods& Modifiers);
} // namespace ElementalSword
} // namespace nz
