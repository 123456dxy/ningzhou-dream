#pragma once
#include "core/model.h"

namespace nz::PoisonRules {

struct BloomQuote {
    bool Allowed = false;
    string Error;
    int BeforeLayers = 0;
    int AfterLayers = 0;
    int AddedLayers = 0;
    int BeforeDamage = 0;
    int AfterDamage = 0;
};

struct SiphonQuote {
    bool Allowed = false;
    string Error;
    int PoisonLayers = 0;
    int HealPotential = 0;
    int Healing = 0;
};

inline constexpr double SiphonHealPerLayer = 0.04;
inline constexpr int SiphonMaximumHealthPercent = 12;

// 用途：计算普通新增毒层的精确跳伤；流程：施毒/自然增长；返回：合计取整，不改状态。
int mfPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount);
// 用途：只给新增毒层计价，保留混合毒与清毒余量；流程：真实/预览；返回：封顶后的每跳毒伤。
int mfAddedPoisonTick(double DamageBasis, int BeforeLayers, int AfterLayers, int ExistingDamage);
// 用途：解释缺少缓存的历史毒；流程：敌方视角兼容；返回：旧系数跳伤，不改状态。
int mfLegacyPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount);
// 用途：解释玩家缺少缓存的历史毒；流程：持续跳伤/预览；返回：冻结旧曲线跳伤。
int mfLegacyPlayerPoisonTickFromLayers(int Stage, int PoisonLayerCount);
// 用途：集中报价滋毒诀；流程：玩家/敌方施放、预览、AI；返回：实际增层和同比跳伤或拒绝，不改状态/RNG。
BloomQuote mfQuoteBloom(int ActorStage, const Battle& State);
// 用途：集中报价借毒回春本术；流程：双方施放/预览/AI；返回：潜力与实际治疗，不改毒缓存、状态或RNG。
SiphonQuote mfQuoteSiphon(int ActorStage, const Battle& State);

} // namespace nz::PoisonRules
