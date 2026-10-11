#pragma once

#include "core/model.h"

namespace nz {

// 普通坊市、采药、寻矿和常规悬赏共用的产出上限。
// 高阶材料仍可保有和转让，主要新增来源是野外秘境与周期副本。
int ordinaryHerbGradeLimit(const Player& player);
int ordinaryMaterialGradeLimit(const Player& player);
string ordinaryHerbError(const Player& player, int grade);
string ordinaryMaterialError(const Player& player, int grade);

} // namespace nz
