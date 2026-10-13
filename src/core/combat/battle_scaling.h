#pragma once

namespace nz {

inline constexpr double BattleDamagePowerMultiplier = 0.3;
inline constexpr double BattleDamageFlatBasis = 18.0;

// 用途：按已保存的基础战力生成伤害基底；流程：真实玩家/NPC/妖兽攻击、毒、灼烧及其预览。
// 参数：BasePower为无单位基础战力，负数按零；返回：战力×0.3+18的未取整值。
// 沿用各效果原分段取整，不提前对DamageBasis取整。
// 副作用：无，不读取游戏状态/RNG，不保存引用；可独立并行调用。气血/护盾/治疗不使用此函数。
[[nodiscard]] double mfBattleDamageBasisFromPower(int BasePower);

// 用途：由境界查询同一伤害基底；流程：神通效果和持续状态；参数：Stage沿既有规则钳位0..12。
// 返回：BATTLE_POWER[Stage]×0.3+18；不额外按神通等级或大境界再乘，不改变任何状态。
[[nodiscard]] double mfBattleDamageBasis(int Stage);

} // namespace nz
