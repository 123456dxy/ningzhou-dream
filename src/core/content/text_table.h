#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "core/content/game_ids.h"

namespace nz::content {

enum class TextId {
    StrikeDescription, DefendDescription, DrawDescription, ForeseeDescription,
    RedrawDescription, ExhaustDescription, EmpowerDescription, WardDescription,
    ManaDescription, StoreDescription, FlowDescription, BurstDescription,
    FloatDescription, HealDescription, ShieldDescription, GuardExtra, ArrayExtra,
    SwordDescription, HumanDescription, HeavyDescription, ChargeDescription,
    WaterDescription, WaveDescription, PoisonDescription, PoisonBurstDescription,
    FireDescription, FireBurstDescription, SoulDescription, SoulBurstDescription,
    BodyBurstDescription, DrainDescription, StealDescription, AttackDescription,
    CooldownDescription, BattleRules, UnknownDao, ArtifactActivate,
    ArtifactYinQiSword, ArtifactYinMetalSword, ArtifactYinSword,
    ArtifactYangDirectSword, ArtifactYangSword, ArtifactYinQiSeal,
    ArtifactYinMetalSeal, ArtifactYinSeal, ArtifactYangSeal, ArtifactYinArmor,
    ArtifactYangArmor, ArtifactBell, ArtifactHeal, ArtifactWard, ArtifactEmpower,
    ArtifactSuffix, AchievementBrewCondition, AchievementForgeCondition,
    AchievementWinsCondition, AchievementNodesCondition, AchievementManualsCondition,
    AchievementQuestsCondition, AchievementWealthCondition, AchievementReward,
    AchievementClaimedReward, AchievementRewardNote,
    PillCultivationCompleteEffect, PillCultivationUseLimit,
    GardenReserveRequiresBottle, GardenBottleGrowthRule, GardenNoBottleGrowthRule,
    DungeonElapsedPaused, FireSiphonDescription, PoisonBloomDescription, PoisonSiphonDescription,
    DrawToDescription
};

struct TextArg { std::string_view name, value; };
// 用途：获取内容 ID 的中文展示名。
// 参与流程：大道或宗门的界面文案。
// 使用方式：传 DaoId 或 SectId；非法枚举抛 invalid_argument。
// 返回值：对应的中文字符串视图，不用于保存协议。
std::string_view displayName(DaoId id);
// 用途：获取内容 ID 的中文展示名。
// 参与流程：大道或宗门的界面文案。
// 使用方式：传 DaoId 或 SectId；非法枚举抛 invalid_argument。
// 返回值：对应的中文字符串视图，不用于保存协议。
std::string_view displayName(SectId id);
// 用途：读取集中维护的规则文案模板。
// 参与流程：神通说明、战报和法宝描述。
// 使用方式：传有效 TextId；非法 ID 抛 invalid_argument。
// 返回值：不可变文案或包含具名占位符的模板视图。
std::string_view text(TextId id);
// Named parameters are strict: missing, duplicate, unused or malformed arguments
// throw invalid_argument. Inserted values are copied verbatim and never parsed.
// 用途：按 TextId 读取模板并严格代入参数。
// 参与流程：卡牌与法宝说明生成。
// 使用方式：传文案 ID 和可选具名参数。
// 返回值：格式化文案；参数错误按模板规则抛异常。
std::string formatText(TextId id, std::initializer_list<TextArg> arguments = {});
// 用途：严格代入模板中的具名参数。
// 参与流程：集中内容文案格式化底层。
// 使用方式：传模板和参数列表；缺失、重复、未使用或畸形参数会抛错。
// 返回值：完成替换的字符串，插入值不会再次解析。
std::string formatTextTemplate(std::string_view pattern, std::initializer_list<TextArg> arguments = {});

} // namespace nz::content
