#include "core/sword.h"

#include <algorithm>

#include "core/arts.h"
#include "core/sim.h"
#include "data/reference_tables.h"

namespace nz {

// 用途：保留旧剑意称号的阈值表。
// 参与流程：旧存档兼容查询。
// 使用方式：读取静态表，不用于斗法倍率。
// 返回值：七个旧称号定义的常量引用。
const vector<data::SwordRealmDef>& swordRealms() {
    return data::SwordRealmDef_TABLE();
}

// 用途：按旧剑意值查历史称号序号。
// 参与流程：旧剑意名称查询。
// 使用方式：传旧字段值，超最高阈值仍取最后一项。
// 返回值：从零开始的称号序号。
int swordRank(int swordValue) {
    // JS: SWORD_REALMS.filter(x=>value>=x.min).length-1
    int rank = 0;
    const auto& realms = swordRealms();
    for (size_t i = 0; i < realms.size(); ++i)
        if (swordValue >= realms[i].minValue) rank = static_cast<int>(i);
    return rank;
}

// 用途：查旧剑意值对应的历史称号。
// 参与流程：兼容旧剑意展示或迁移诊断。
// 使用方式：传剑意数值；新战斗不依赖此名。
// 返回值：历史称号；表为空时为空字符串。
string swordRealmName(int swordValue) {
    const auto& realms = swordRealms();
    if (realms.empty()) return "";
    return realms[static_cast<size_t>(swordRank(swordValue))].name;
}

// 用途：按宗门、主修或携带剑招识别剑修身份。
// 参与流程：流派身份兼容查询。
// 使用方式：传角色只读状态。
// 返回值：满足任一剑修条件时为真。
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

// 用途：让旧境差查询保持中性，永久剑意不再增减伤。
// 参与流程：旧调用兼容；实际攻防走统一功法与悟道。
// 使用方式：可传任意双方旧剑意值，均不产生修正。
// 返回值：active 为假、gap 和 reduction 为零、attack 为一。
SwordDuel swordDuelOf(const Player& p, const Battle& b) {
    SwordDuel d;
    // 兼容旧调用；攻防由功法、悟道节点和本场卡牌状态决定。
    (void)p; (void)b;
    return d;
}

// 用途：将剑道活动收益计入统一悟道经验。
// 参与流程：剑道成长事件。
// 使用方式：传游戏与收益量，负收益按零处理。
// 返回值：无；更新玩家剑道经验和升级悟道点，保留旧剑意。
void swordGain(Game& g, int amount) {
    artGain(g.player, "剑", std::max(0, amount));
}

} // namespace nz
