// 宁州一梦 · C++ 版 — 剑道境界实现（对应 JS 版 sword.mjs）
#include "core/sword.h"

#include <algorithm>

#include "core/arts.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nz {

const vector<data::SwordRealmDef>& swordRealms() {
    return data::SwordRealmDef_TABLE();
}

int swordRank(int swordValue) {
    // JS: SWORD_REALMS.filter(x=>value>=x.min).length-1
    int rank = 0;
    const auto& realms = swordRealms();
    for (size_t i = 0; i < realms.size(); ++i)
        if (swordValue >= realms[i].minValue) rank = static_cast<int>(i);
    return rank;
}

string swordRealmName(int swordValue) {
    const auto& realms = swordRealms();
    if (realms.empty()) return "";
    return realms[static_cast<size_t>(swordRank(swordValue))].name;
}

bool swordCultivator(const Player& p) {
    // 金虹弟子
    if (p.sect == "jinhong") return true;

    // 主修功法属剑道
    for (const auto& m : data::BaseManualDef_TABLE())
        if (m.id == p.arts.main && m.dao == "剑") return true;

    // 携带两种御剑/人剑类神通
    static const char* swordSpells[4] = {"sword", "rain", "humanSword",
                                         "swordRelease"};
    int count = 0;
    for (const auto& id : p.arts.spells)
        for (const char* s : swordSpells)
            if (id == s) ++count;
    return count >= 2;
}

SwordDuel swordDuelOf(const Player& p, const Battle& b) {
    SwordDuel d;
    // 兼容旧调用；攻防由功法、悟道节点和本场卡牌状态决定。
    (void)p; (void)b;
    return d;
}

void swordGain(Game& g, int amount) {
    artGain(g.player, "剑", std::max(0, amount));
}

} // namespace nz
