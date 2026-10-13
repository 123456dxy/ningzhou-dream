#include "core/social/npc_consumables.h"

#include <algorithm>

#include "core/arts.h"
#include "core/content/game_ids.h"

namespace nz {
namespace {
using content::DaoId;

// 丹方源文案可改名；流派限制只绑定稳定丹方 ID。
optional<DaoId> pillAffinity(const data::RecipeDef& recipe) {
    if (!recipe.dao.empty()) return content::parseDaoId(recipe.dao);
    static const map<string, DaoId> affinities = {
        {"5106", DaoId::Fire}, {"5107", DaoId::Water}, {"5108", DaoId::Metal},
        {"5109", DaoId::Wood}, {"5110", DaoId::Earth}, {"5113", DaoId::Sword},
        {"5114", DaoId::Metal}, {"5115", DaoId::Wood}, {"5116", DaoId::Water},
        {"5117", DaoId::Fire}, {"5118", DaoId::Earth},
        {"5214", DaoId::Metal}, {"5215", DaoId::Wood}, {"5216", DaoId::Water},
        {"5217", DaoId::Fire}, {"5218", DaoId::Earth}, {"5219", DaoId::Sword},
        {"5220", DaoId::Metal}, {"5221", DaoId::Wood}, {"5222", DaoId::Water},
        {"5223", DaoId::Fire}, {"5224", DaoId::Earth},
        {"5226", DaoId::Metal}, {"5227", DaoId::Wood}, {"5228", DaoId::Water},
        {"5229", DaoId::Fire}, {"5230", DaoId::Earth},
        {"5310", DaoId::Metal}, {"5311", DaoId::Wood}, {"5312", DaoId::Water},
        {"5313", DaoId::Fire}, {"5314", DaoId::Earth}
    };
    const auto found = affinities.find(recipe.id);
    return found == affinities.end() ? optional<DaoId>{} : found->second;
}

Player pillProfile(const Npc& npc) {
    Player player;
    static_cast<Person&>(player) = static_cast<const Person&>(npc);
    player.talent.clear();
    player.sect = npc.sect;
    player.arts = artNpcLoadout(npc);
    for (const auto& [key, count] : npc.bag)
        if (key.starts_with("potion:") && count > 0)
            player.arts.potions[key.substr(7)] = count;
    return player;
}

int pillPriority(const Player& player, const data::RecipeDef& recipe, bool beforeBattle) {
    const auto& arts = player.arts;
    const auto affinity = pillAffinity(recipe);
    if (affinity && affinity != content::parseDaoId(arts.focus)) return 0;
    if (!recipe.dao.empty() && !affinity) return 0;
    // NPC 不冒险越阶；突破丹按精确境界门槛另行判断。
    if (recipe.grade > artTier(player) && recipe.kind != "breakthrough") return 0;
    const auto info = artPillUseInfo(player, recipe);
    if (!info.reason.empty()) return 0;
    const auto& kind = recipe.kind;
    if (kind == "combat") return beforeBattle && info.effectValue > 0 ? 60 : 0;
    if (kind == "breakthrough")
        return !beforeBattle && arts.breakBuff.empty() && player.stage < 12 &&
               player.xp >= realmOf(player.stage).need ? 85 : 0;
    if (kind == "detox") return info.poisonAfter < arts.poison ? (arts.poison >= 60 ? 110 : 30) : 0;
    if (kind == "repel") return !beforeBattle && arts.repel == 0 ? 20 : 0;
    // 诱妖是主动采集操作，不当作有益的服丹动作。
    if (kind == "lure") return 0;
    if (info.effectValue <= 0) return 0;
    if (kind == "heal") return player.health < 70 ? 100 : 70;
    if (kind == "life") return player.age + 5 >= player.lifespan ? 105 : 65;
    if (kind == "xp") return beforeBattle ? 0 : 80;
    if (kind == "dao") {
        const auto found = arts.exp.find(recipe.dao);
        if (found != arts.exp.end() && found->second >= ART_THRESHOLDS[5]) return 0;
        return 55;
    }
    if (kind == "points") return arts.points < 10 && !artHas(arts, arts.focus, 5) ? 40 : 0;
    // 属性仍有账面空间、最终战斗/突破贡献已封顶时同样不浪费。
    if (kind == "body" && artModsOf(arts).hp >= 2.5) return 0;
    if (kind == "mind" && artModsOf(arts).mana >= 30) return 0;
    if (kind == "speed" && arts.speed >= 200) return 0;
    if (kind == "heart" && arts.heart >= 100) return 0;
    if (kind == "apt" || kind == "insight" || kind == "body" ||
        kind == "heart" || kind == "mind" || kind == "speed") return 50;
    return 0;
}

void spendDaoPoints(Npc& npc) {
    const Arts loadout = artNpcLoadout(npc);
    const int index = daoIndex(loadout.focus);
    if (index < 0) return;
    const auto experience = loadout.exp.find(loadout.focus);
    const int level = experience == loadout.exp.end() ? 0 : artLevel(experience->second);
    for (int next = 1; next <= level; ++next) {
        const string id = std::to_string(index) + "-" + std::to_string(next);
        if (std::find(loadout.nodes.begin(), loadout.nodes.end(), id) != loadout.nodes.end()) continue;
        const auto* node = findNode(id);
        if (!node || npc.pillState.points < node->cost) break;
        npc.pillState.points -= node->cost;
        npc.pillState.nodes.push_back(id);
    }
}

bool applyPill(Npc& npc, Player& player, const data::RecipeDef& recipe) {
    const int pointsBefore = player.arts.points;
    const int expBefore = recipe.kind == "dao" ? player.arts.exp[recipe.dao] : 0;
    if (!artApplyPill(player, recipe.id).empty()) return false;
    static_cast<Person&>(npc) = static_cast<const Person&>(player);
    auto& state = npc.pillState;
    const auto& arts = player.arts;
    state.used = arts.used;
    state.poison = arts.poison; state.aptPills = arts.aptPills;
    state.hpBonus = arts.hpBonus; state.heart = arts.heart;
    state.mind = arts.mind; state.speed = arts.speed;
    state.repel = arts.repel; state.breakBuff = arts.breakBuff;
    state.combatPrep = arts.combatPrep; state.combatBuff = arts.combatBuff;
    state.points = std::clamp(state.points + arts.points - pointsBefore, 0, 100);
    if (recipe.kind == "dao")
        state.daoExp[recipe.dao] = std::clamp(state.daoExp[recipe.dao] +
            arts.exp.at(recipe.dao) - expBefore, 0, 10000);
    --npc.bag.at("potion:" + recipe.id);
    if (std::find(npc.pillsUsed.begin(), npc.pillsUsed.end(), recipe.id) == npc.pillsUsed.end())
        npc.pillsUsed.push_back(recipe.id);
    spendDaoPoints(npc);
    return true;
}
} // namespace

void applyNpcPillState(const Npc& npc, Arts& arts) {
    const auto& state = npc.pillState;
    arts.used = state.used;
    // 旧档记录代表至少服过一次；不重放旧效果，也不增加第二次计数。
    for (const auto& id : npc.pillsUsed)
        if (findRecipe(id)) arts.used[id] = std::max(1, arts.used[id]);
    arts.poison = std::clamp(state.poison, 0, 100);
    arts.aptPills = std::clamp(state.aptPills, 0, 10);
    arts.hpBonus = std::clamp(state.hpBonus, 0, 200);
    arts.heart = std::clamp(state.heart, 0, 500);
    arts.mind = std::clamp(state.mind, 0, 500);
    arts.speed = std::clamp(state.speed, 0, 500);
    arts.points = std::clamp(state.points, 0, 100);
    arts.repel = std::clamp(state.repel, 0, 2);
    arts.breakBuff = state.breakBuff;
    arts.combatPrep = state.combatPrep;
    arts.combatBuff = state.combatBuff;
    for (const auto& [dao, exp] : state.daoExp)
        if (daoIndex(dao) >= 0) arts.exp[dao] = std::min(10000, arts.exp[dao] + std::clamp(exp, 0, 10000));
    for (const auto& id : state.nodes)
        if (findNode(id) && std::find(arts.nodes.begin(), arts.nodes.end(), id) == arts.nodes.end())
            arts.nodes.push_back(id);
}

bool npcUsePill(Npc& npc, const string& id, bool beforeBattle) {
    if (!npc.alive || npc.beast || npc.health <= 0 || npc.stage < 0 || npc.stage > 12) return false;
    const auto inventory = npc.bag.find("potion:" + id);
    if (inventory == npc.bag.end() || inventory->second <= 0) return false;
    const auto* recipe = findRecipe(id);
    if (!recipe) return false;
    Player player = pillProfile(npc);
    return pillPriority(player, *recipe, beforeBattle) > 0 && applyPill(npc, player, *recipe);
}

int npcConsumeUsefulPills(Npc& npc, bool beforeBattle) {
    if (!npc.alive || npc.beast || npc.health <= 0 || npc.stage < 0 || npc.stage > 12) return 0;
    int consumed = 0;
    // 每次重新计算实际收益，满血、满属性或同类战斗准备不再浪费丹药。
    for (int dose = 0; dose < 3; ++dose) {
        Player player = pillProfile(npc);
        const data::RecipeDef* best = nullptr;
        int priority = 0;
        for (const auto& [id, count] : player.arts.potions) {
            const auto* recipe = findRecipe(id);
            if (!recipe || count <= 0) continue;
            const int candidate = pillPriority(player, *recipe, beforeBattle);
            if (candidate > priority) { best = recipe; priority = candidate; }
        }
        if (!best || !applyPill(npc, player, *best)) break;
        ++consumed;
    }
    return consumed;
}

} // namespace nz
