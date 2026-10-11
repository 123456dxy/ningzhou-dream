// 宁州一梦 · C++ 版 — 剑道境界（对应 JS 版 sword.mjs，Demo 06 新增）
//
// 剑意是永久属性（0–500），按阈值分七重境界。
// 双方都是剑修时，按「剑道境界差」直接影响攻防。
#pragma once

#include <string>

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz {

// 七重剑道境界：初识剑路 / 剑随心动 / 剑气凝锋 / 剑意通明 /
//               剑心圆融 / 剑道化境 / 万剑归一
const vector<data::SwordRealmDef>& swordRealms();

// 当前剑意所处的重数（0..6）
int swordRank(int swordValue);
// 当前境界名
string swordRealmName(int swordValue);

// 是否剑修：金虹弟子、主修剑道功法、或携带两种御剑/人剑类神通
bool swordCultivator(const Player& p);
bool swordCultivatorOf(const class Game& g);

// 剑修对阵修正。仅当双方都是剑修时生效。
struct SwordDuel {
    bool active = false;
    int gap = 0;              // 重数差，已夹到 [-4, 4]
    double attack = 1.0;      // 伤害倍率：1 + gap*0.04
    double reduction = 0.0;   // 来伤修正：gap*0.025
};
SwordDuel swordDuelOf(const Player& p, const Battle& b);

// 剑意增长；跨越境界时记录进境
void swordGain(Game& g, int amount);

} // namespace nz
