#pragma once

namespace nz {

struct Player;
struct Npc;
struct Game;

inline constexpr int HealthGateMinimum = 30;
inline constexpr double HealthMonthlyRecoverFraction = 0.03;
inline constexpr double HealthRestRecoverFraction = 0.10;
inline constexpr double NpcEncounterHealthMultiplier = 1.45;
inline constexpr double HealthFlatBasis = 110.0;
inline constexpr double HealthPowerMultiplier = 2.4;

// 用途：查询同一权威气血公式；基础丹药/筑基奖励先加战力基数，再经2.4、功法和体魄乘区。
// 参数：ActorScale为正有限遭遇倍率，玩家1、普通NPC1.45；返回：气血点数，不改变状态/RNG。
[[nodiscard]] int mfHealthMaximum(const Player& PlayerState, double ActorScale = 1.0);
[[nodiscard]] int mfHealthMaximumFromPower(int BasePower, double BaseHealthBonus,
    double HpMultiplier, int Body, double ActorScale = 1.0);
[[nodiscard]] int mfPlayerHealthMaximum(const Player& PlayerState);
[[nodiscard]] int mfNpcHealthMaximum(const Npc& PersonState);
// 用途：在兼容百分比边界与绝对气血间转换；0保留死亡，百分比钳位0..100，不改变状态。
[[nodiscard]] int mfHealthPercentAmount(int Maximum, int Percent);
[[nodiscard]] int mfHealthFromPercent(int Percent, int Maximum);
[[nodiscard]] int mfHealthToPercent(int Current, int Maximum);
// 用途：纯计算封顶恢复和月/疗养恢复量；不复活0气血，不消耗余数、时间或RNG。
[[nodiscard]] int mfRecoveredHealth(int Current, int Maximum, int Amount);
[[nodiscard]] int mfMonthlyHealthRecovery(int Maximum);
[[nodiscard]] int mfRestExtraHealthRecovery(int Maximum);
// 用途：真实日历动作恢复当前气血，普通斗法中更新战斗值；留影和死亡状态不恢复。
// 返回：本次实际恢复点数；无IO/RNG，调用方负责日历与保存边界。
int mfRecoverPlayerHealth(Game& State, int Amount);

inline constexpr double BattleDamagePowerMultiplier = 0.3;
inline constexpr double BattleDamageFlatBasis = 18.0;
inline constexpr double PoisonTickBasisCoefficient = 0.045;
inline constexpr double BurnTickBasisCoefficient = 0.08;
inline constexpr int CommonSectActionBase = 2;
inline constexpr int ChampionSectActionBase = 3;
inline constexpr int NpcSectActionLimit = 8;
inline constexpr double SoulBurstPerLayerBonus = 0.25;
inline constexpr double SoulBurstPiercePerLayer = 0.05;
inline constexpr double SoulBurstMaximumPierceFraction = 0.30;
// 已在途且缺少精确每跳字段的毒按当时系数解释；动作边界转换后不再回退为整层伤害。
inline constexpr double LegacyPoisonTickBasisCoefficient = 0.035;

// 用途：按已保存的基础战力生成伤害基底；流程：真实玩家/NPC/妖兽攻击、毒、灼烧及其预览。
// 参数：BasePower为无单位基础战力，负数按零；返回：战力×0.3+18的未取整值。
// 沿用各效果原分段取整，不提前对DamageBasis取整。
// 副作用：无，不读取游戏状态/RNG，不保存引用；可独立并行调用。气血/护盾/治疗不使用此函数。
[[nodiscard]] double mfBattleDamageBasisFromPower(int BasePower);

// 用途：由境界查询同一伤害基底；流程：神通效果和持续状态；参数：Stage沿既有规则钳位0..12。
// 返回：BATTLE_POWER[Stage]×0.3+18；不额外按神通等级或大境界再乘，不改变任何状态。
[[nodiscard]] double mfBattleDamageBasis(int Stage);

} // namespace nz
