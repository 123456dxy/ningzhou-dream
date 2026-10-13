#include "core/combat/battle_scaling.h"
#include "core/battle.h"
#include <algorithm>

namespace nz {

// 用途：集中应用可维护的两个伤害参数；流程：所有按基础战力增长的伤害；返回：未取整伤害基底。
double mfBattleDamageBasisFromPower(int BasePower) {
    return std::max(0, BasePower) * BattleDamagePowerMultiplier + BattleDamageFlatBasis;
}

// 用途：将合法境界基数转换为统一伤害尺度；流程：玩家/NPC神通与只读预览；返回：纯数值。
double mfBattleDamageBasis(int Stage) {
    return mfBattleDamageBasisFromPower(BATTLE_POWER[clampInt(Stage, 0, 12)]);
}

} // namespace nz
