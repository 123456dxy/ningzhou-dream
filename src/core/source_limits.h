#pragma once

#include "core/model.h"

namespace nz {

// 普通坊市、采药、寻矿和常规悬赏共用的产出上限。
// 高阶材料仍可保有和转让，主要新增来源是野外秘境与周期副本。
// 用途：计算普通坊市出售的药材最高品阶。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ordinaryHerbGradeLimit(const Player& player);
// 用途：计算普通坊市出售的器材最高品阶。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ordinaryMaterialGradeLimit(const Player& player);
// 用途：说明药材受品阶与境界限制的购买原因。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& player, int grade，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ordinaryHerbError(const Player& player, int grade);
// 用途：说明器材受品阶与境界限制的购买原因。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& player, int grade，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ordinaryMaterialError(const Player& player, int grade);

} // namespace nz
