#pragma once

#include "core/arts.h"

namespace nz {

// 天赋叠加在玩家现有功法与悟道之上，查询本身不改变角色。
// 用途：在功法和悟道系数上叠加天赋机制。
// 参与流程：战斗、炼丹炼器与遁速的统一属性查询。
// 使用方式：传角色，从 artModsOf 复制后追加天赋并限制生活收益。
// 返回值：汇总 ArtMods 值，不改变角色或基础缓存。
ArtMods talentMods(const Player& player);
// Positive, deliberate social interactions only; callers must not reward rejection or repeat greetings.
int talentAffinityBonus(const Player& player);
// 用途：查询先天道体的修炼速度乘数。
// 参与流程：基础修炼速度汇总。
// 使用方式：传角色只读状态。
// 返回值：有先天道体时 1.10，否则 1.0。
double talentCultivationMul(const Player& player);
// 用途：查询筑基后先天道体提供的灵力容量。
// 参与流程：斗法容量汇总。
// 使用方式：传角色，要求先天道体且 stage 至少三。
// 返回值：额外两点容量，否则为零。
int talentManaBonus(const Player& player);
// 用途：检查建号天赋之间的互斥组合。
// 参与流程：角色创建校验。
// 使用方式：传待选天赋 ID 列表，检查体质和五行灵根互斥。
// 返回值：错误文案；可共同选择时为空。
string talentConflict(const vector<string>& traits);

// 只在新建角色时发放初始传承；读档不会重新发放。
// 用途：只发放一次建号天赋初始收益。
// 参与流程：新建角色初始化。
// 使用方式：传游戏，talentBirth 标记阻止读档重复发放。
// 返回值：无；修改悟道点、神识、基础功法、已知神通和标记。
void talentBirth(Game& game);
// 用途：保留既有月度天赋兼容入口；流程：旧日历调用；返回：无且不修改状态。
// 神秘绿瓶真实效果移到灵田日tick，按已播种批次和格内储备加速，不复制背包草药。
void talentMonthTick(Game& game);
// 用途：为不灭道心每次新大境界奖励悟道点。
// 参与流程：大境界突破成功后的天赋结算。
// 使用方式：传游戏及是否大突破，标记防止同境重复奖励。
// 返回值：无；增加玩家悟道点并更新已奖励的大境界标记。
void talentBreakthrough(Game& game, bool major);

} // namespace nz
