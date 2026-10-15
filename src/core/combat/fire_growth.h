#pragma once
#include <string>

namespace nz {
struct ArtMods;
namespace FireGrowth {
inline constexpr int MaximumGrowthRounds = 5;
inline constexpr double FlameNodeStackGainPerRound = 0.10;
inline constexpr double KindleNodeStackGainPerRound = 0.07;
inline constexpr double ExplosionNodeGainPerRound = 0.10;

// 用途：汇总功法与本场已成长的新增灼烧加成；流程：真实施术/预览；返回：附加比例，不改状态。
[[nodiscard]] double mfBurnStackBonus(const ArtMods& Modifiers, int GrowthRounds);
// 用途：汇总功法与本场引爆成长；流程：引爆真实伤害/说明；返回：含基础1的倍率，不推进轮次。
[[nodiscard]] double mfExplosionMultiplier(const ArtMods& Modifiers, int GrowthRounds);
// 用途：统一折算可引爆的灼烧总量；流程：引焰爆/噬焰回生与预览；返回：非负整数，不含神通直伤或目标防御。
[[nodiscard]] int mfExplosionAmount(const ArtMods& Modifiers, int GrowthRounds, int BurnDamage, int RemainingTurns);
// 用途：按一次真实回合灼烧结算积累成长；流程：双方共同状态结算；返回：无，无有效灼烧或无成长悟道时不增长。
void mfAdvanceAfterBurnTick(const ArtMods& Modifiers, int AppliedBurnDamage, int& GrowthRounds);
// 用途：用同一规则常量说明火道节点；流程：悟道目录；返回：对应2/4/5层说明，其余为空。
[[nodiscard]] std::string mfNodeDescription(int NodeLevel);
}
}
