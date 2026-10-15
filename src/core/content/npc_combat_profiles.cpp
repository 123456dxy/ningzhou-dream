#include "core/content/npc_combat_profiles.h"
#include <algorithm>
#include <array>
#include "core/arts.h"

namespace nz::content {
// 用途：集中登记六宗完整构筑；流程：真实 NPC 斗法、常态授业与评分；用法：由只读查询取表；返回：不随随机数变化的模板。
std::span<const NpcCombatProfile> npcCombatProfiles() {
    static const std::array<NpcCombatProfile, 6> profiles{{
        {"jinhong-complete", "金虹弃牌锐势", "jinhong",
         {"gold", "bladecare", "mind", "heavy", "goldenrain"},
         {"metalSeal", "goldslash", "goldblade", "goldcover", "metalRedraw", "refresh", "mind", "gold"}, "metal-discard"},
        {"xinghe-complete", "星河叠浪回流", "xinghe",
         {"wave", "rainform", "ocean", "water", "zhou"},
         {"waterSeal", "wavehit", "stormSword", "waterfall", "water", "softwater", "waterDraw", "skim"}, "water-wave"},
        {"zhushan-complete", "竹山毒生回春", "zhushan",
         {"venom", "turtle", "wood", "evergreen", "longlife"},
         {"corrode", "poisonBurst", "toxinRain", "wood", "woodwall", "woodDraw", "skim", "gather"}, "wood-poison"},
        {"lihuo-complete", "离火积焰引爆", "lihuo",
         {"playfire", "flame", "kindle", "sunwheel", "fire"},
         {"fireSeal", "spitfire", "fire", "fireburst", "fireSiphon", "fireDraw", "gather", "hoard"}, "fire-stack"},
        {"huachen-complete", "化尘厚土蓄势", "huachen",
         {"mountain", "earth", "jadecloud", "gravity", "heavy"},
         {"collapse", "mountainSeal", "goldslash", "stoneSkin", "earth", "earthDraw", "hoard", "refresh"}, "earth-charge"},
        {"xuandao-complete", "玄道五行归剑", "xuandao",
         {"fiveSword", "array", "sword", "zhou", "swordcraft"},
         {"metalSword", "waterSword", "woodSword", "fireSword", "earthSword", "skim", "swordDraw"}, "five-elements"}
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
            return npc.alive && npc.health > 0 && npc.age < npc.lifespan && findNpcCombatProfile(npc) == &profile;
        });
        if (alreadyAssigned) continue;
        Npc* chosen = nullptr;
        for (auto& npc : game.npcs) {
            if (!npc.alive || npc.health <= 0 || npc.age >= npc.lifespan || npc.beast || npc.sect != profile.sect || npc.id.empty() || !npc.combatProfileId.empty()) continue;
            if (!chosen || npc.stage > chosen->stage || (npc.stage == chosen->stage && npc.id < chosen->id)) chosen = &npc;
        }
        if (chosen) chosen->combatProfileId = profile.id;
    }
}

// 用途：补齐常态宗门传承入口；流程：新局、年度更新及缺第六宗的读档初始化。
// 用法：传所属游戏的可写状态，已有存活成员优先承接；返回：无，不投骰，不发玩家资源、不修改在途斗法。
void mfEnsureSectTransmissions(Game& State) {
    constexpr int TeacherStage = 9;
    for (const auto& Profile : npcCombatProfiles()) {
        State.world.sects.try_emplace(Profile.sect, SectState{});
        const bool HasLivingMember = std::any_of(State.npcs.begin(), State.npcs.end(), [&](const Npc& Person) {
            return Person.alive && !Person.beast && Person.health > 0 && Person.age < Person.lifespan &&
                Person.sect == Profile.sect && !Person.id.empty();
        });
        if (HasLivingMember) continue;
        Npc Teacher;
        Teacher.id = "transmission-" + Profile.sect + "-" + std::to_string(State.year);
        while (std::any_of(State.npcs.begin(), State.npcs.end(), [&](const Npc& Person) { return Person.id == Teacher.id; }))
            Teacher.id += "-successor";
        Teacher.name = string(findSect(Profile.sect)->name) + "·传承执教";
        if (std::any_of(State.npcs.begin(), State.npcs.end(), [&](const Npc& Person) { return Person.name == Teacher.name; }))
            Teacher.name += "·" + std::to_string(State.year);
        Teacher.sect = Teacher.location = Profile.sect;
        Teacher.gender = Profile.sect == "xinghe" ? "女" : "男";
        Teacher.stage = TeacherStage;
        Teacher.age = 240;
        Teacher.lifespan = realmOf(TeacherStage).life;
        Teacher.apt = 74;
        Teacher.insight = 60;
        Teacher.stones = 600;
        Teacher.dao = string(findSect(Profile.sect)->element);
        Teacher.personality = "潜心问道";
        Teacher.goal = "传授完整构筑";
        Teacher.combatProfileId = Profile.id;
        State.npcs.push_back(std::move(Teacher));
        State.world.sects.at(Profile.sect).members = 1 + (State.player.sect == Profile.sect && !State.ended ? 1 : 0);
    }
    ensureChampionNpcs(State);
}
} // namespace nz::content
