#include "core/content/npc_combat_profiles.h"
#include <algorithm>
#include <array>

namespace nz::content {
// 用途：集中登记五宗完整构筑；流程：真实 NPC 斗法与评分；用法：由只读查询取表；返回：不随随机数变化的模板。
std::span<const NpcCombatProfile> npcCombatProfiles() {
    static const std::array<NpcCombatProfile, 5> profiles{{
        {"jinhong-complete", "金虹弃牌锐势", "jinhong",
         {"gold", "bladecare", "mind", "heavy", "hidden"},
         {"refresh", "mind", "goldcover", "goldslash", "gold", "metalSeal", "skim", "hoard"}},
        {"xinghe-complete", "星河叠浪回流", "xinghe",
         {"wave", "rainform", "ocean", "water", "zhou"},
         {"waterSeal", "wavehit", "water", "softwater", "refresh", "skim", "hoard", "scheme"}},
        {"zhushan-complete", "竹山毒生回春", "zhushan",
         {"venom", "turtle", "wood", "evergreen", "longlife"},
         {"corrode", "poisonBurst", "wood", "woodwall", "refresh", "skim", "gather", "scheme"}},
        {"lihuo-complete", "离火积焰引爆", "lihuo",
         {"playfire", "flame", "kindle", "sunwheel", "fire"},
         {"fireSeal", "spitfire", "fire", "fireburst", "sun", "skim", "gather", "hoard"}},
        {"huachen-complete", "化尘厚土蓄势", "huachen",
         {"mountain", "earth", "jadecloud", "bull", "heavy"},
         {"collapse", "mountainSeal", "goldslash", "aegis", "earth", "gather", "hoard", "refresh"}}
    }};
    return profiles;
}
// 用途：校验完整传承与宗门归属；流程：斗法和评分；用法：传NPC；返回：合法模板或空。
const NpcCombatProfile* findNpcCombatProfile(const Npc& npc) {
    if (npc.beast || npc.combatProfileId.empty()) return nullptr;
    for (const auto& profile : npcCombatProfiles())
        if (profile.id == npc.combatProfileId && profile.sect == npc.sect) return &profile;
    return nullptr;
}
// 用途：共享精英身份判定；流程：斗法、兽潮与人物卡；用法：传 NPC；返回：合法完整流派身份。
bool isChampion(const Npc& npc) { return findNpcCombatProfile(npc) != nullptr; }
// 用途：给每宗一位修士赋稳定传承；流程：建号与迁移；用法：传游戏；返回：无，已有模板保留且不投骰。
void ensureChampionNpcs(Game& game) {
    for (const auto& profile : npcCombatProfiles()) {
        const bool alreadyAssigned = std::any_of(game.npcs.begin(), game.npcs.end(), [&](const Npc& npc) {
            return npc.alive && findNpcCombatProfile(npc) == &profile;
        });
        if (alreadyAssigned) continue;
        Npc* chosen = nullptr;
        for (auto& npc : game.npcs) {
            if (!npc.alive || npc.beast || npc.sect != profile.sect || npc.id.empty() || !npc.combatProfileId.empty()) continue;
            if (!chosen || npc.id < chosen->id) chosen = &npc;
        }
        if (chosen) chosen->combatProfileId = profile.id;
    }
}
} // namespace nz::content
