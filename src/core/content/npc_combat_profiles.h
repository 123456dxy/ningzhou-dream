#pragma once

#include <string_view>
#include <span>
#include "core/model.h"

namespace nz::content {
struct NpcCombatProfile {
    string id, name, sect;
    vector<string> manuals, spells;
};
// 用途：查询完整流派模板；流程：NPC 构筑和兽潮评分；用法：遍历只读表；返回：稳定模板视图。
std::span<const NpcCombatProfile> npcCombatProfiles();
// 用途：验证 NPC 的完整构筑标识；流程：真实战斗与兽潮精英判定；用法：传 NPC；返回：匹配且宗门相符的模板或空。
const NpcCombatProfile* findNpcCombatProfile(const Npc& npc);
// 用途：识别真实完整传承修士；流程：界面标记与日积分；用法：传 NPC；返回：合法模板存在时为真。
bool isChampion(const Npc& npc);
// 用途：给每宗一位稳定 NPC 赋予完整传承；流程：新局和旧档迁移；用法：传游戏；返回：无，保留已有合法模板且不消耗随机数。
void ensureChampionNpcs(Game& game);
} // namespace nz::content
