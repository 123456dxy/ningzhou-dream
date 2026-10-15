#include "core/combat/fire_growth.h"
#include "core/arts.h"
#include <algorithm>
#include <limits>

namespace nz::FireGrowth {
// 用途：从合法成长轮数取附加叠焰系数；流程：双方神通结算；返回：功法固定加成加有限轮次成长。
double mfBurnStackBonus(const ArtMods& Modifiers, int GrowthRounds) {
    return std::max(0.0, Modifiers.burnStack) +
        std::clamp(GrowthRounds, 0, MaximumGrowthRounds) * Modifiers.BurnGrowthStackPerRound;
}
// 用途：从合法成长轮数取引爆倍率；流程：双方引爆和预览；返回：基础1加功法、天赋与有限悟道成长。
double mfExplosionMultiplier(const ArtMods& Modifiers, int GrowthRounds) {
    return 1.0 + std::max(0.0, Modifiers.burnExplosion) +
        std::clamp(GrowthRounds, 0, MaximumGrowthRounds) * Modifiers.BurnGrowthExplosionPerRound;
}
// 用途：避免整数先乘溢出，并让伤害和转换治疗使用同一灼烧池；流程：真实施法和只读副本；返回：向最近整数取整并限制到int容量。
int mfExplosionAmount(const ArtMods& Modifiers, int GrowthRounds, int BurnDamage, int RemainingTurns) {
    const double Amount = static_cast<double>(std::max(0, BurnDamage)) * std::max(0, RemainingTurns) *
        mfExplosionMultiplier(Modifiers, GrowthRounds);
    return jsRound(std::clamp(Amount, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
}
// 用途：只让已经兑现的灼烧推进成长；流程：每完整回合状态结算一次；返回：无，最高五轮，本场结束由新Battle归零。
void mfAdvanceAfterBurnTick(const ArtMods& Modifiers, int AppliedBurnDamage, int& GrowthRounds) {
    if (AppliedBurnDamage <= 0 || (Modifiers.BurnGrowthStackPerRound <= 0 && Modifiers.BurnGrowthExplosionPerRound <= 0)) return;
    GrowthRounds = std::min(MaximumGrowthRounds, std::clamp(GrowthRounds, 0, MaximumGrowthRounds) + 1);
}
// 用途：防止火道数值和说明漂移；流程：节点定义；返回：由共享常量格式化的中文用途。
std::string mfNodeDescription(int NodeLevel) {
    const auto Percent = [](double Ratio) { return std::to_string(jsRound(Ratio * 100)); };
    const std::string Limit = std::to_string(MaximumGrowthRounds);
    if (NodeLevel == 2) return "本场灼烧实际结算后，后续新增灼烧每轮+" + Percent(FlameNodeStackGainPerRound) +
        "%，最多成长" + Limit + "轮；每完整回合最多一次，不再开局翻倍。";
    if (NodeLevel == 4) return "薪火随本场已结算灼烧成长：后续新增灼烧每轮额外+" + Percent(KindleNodeStackGainPerRound) +
        "%，最多" + Limit + "轮；灼烧仍天然叠加并刷新3回合。";
    if (NodeLevel == 5) return "爆焰随本场已结算灼烧成长：引爆每轮额外+" + Percent(ExplosionNodeGainPerRound) +
        "%，最多" + Limit + "轮；引爆后清除灼烧，成长保留到本场结束。";
    return {};
}
}
