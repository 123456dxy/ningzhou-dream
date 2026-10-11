#pragma once

#include "core/arts.h"

namespace nz {

// 天赋叠加在玩家现有功法与悟道之上，查询本身不改变角色。
ArtMods talentMods(const Player& player);
double talentCultivationMul(const Player& player);
int talentManaBonus(const Player& player);
string talentConflict(const vector<string>& traits);

// 只在新建角色时发放初始传承；读档不会重新发放。
void talentBirth(Game& game);
void talentMonthTick(Game& game);
void talentBreakthrough(Game& game, bool major);

} // namespace nz
