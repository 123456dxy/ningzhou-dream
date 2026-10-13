#pragma once

#include "core/arts.h"

namespace nz::crafting {
// 用途：查询真实炼丹规则允许的最早境界；流程：药材用途难度推导；用法：传稳定丹方ID，检查品阶及可用炉容量；返回：0~12或未知/无法炼制时-1，不写状态。
int mfEarliestRecipeCraftStage(const string& RecipeId);
// 用途：按稳定药材ID查询最低取得难度；流程：情报、入口合法性与采集；用法：取原品阶难度与所有真实丹方最早用途的较小值；返回：0~12或非法ID时-1，纯查询。
int mfHerbAcquisitionStage(const string& HerbId);
// 用途：区分可采草药与妖丹；流程：野外采集与分类检查；用法：传稳定药材ID；返回：合法草药为真，妖丹/未知ID为假，不按中文名称判断。
bool mfIsPlantHerb(const string& HerbId);
// 用途：列出当前地点可采的草药；流程：本地服务、采药计划和药谷奖励；用法：传只读角色与地点ID，高品仅在野外原生境出现、无匹配时仅低品兜底；返回：目录借用指针列表，不推进RNG。
vector<const data::HerbDef*> mfGatherableHerbs(const Player& Character, const string& LocationId);
} // namespace nz::crafting
