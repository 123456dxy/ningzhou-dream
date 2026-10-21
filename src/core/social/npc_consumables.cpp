#include "core/social/npc_consumables.h"

#include <algorithm>

#include "core/arts.h"
#include "core/combat/battle_scaling.h"
#include "core/content/game_ids.h"
#include "core/crafting/craft_prices.h"

namespace nz {
namespace {
using content::DaoId;

NpcLifePillUrgency mfLifeUrgency(int RemainingYears) {
    if (RemainingYears <= 5) return NpcLifePillUrgency::Critical;
    if (RemainingYears <= 10) return NpcLifePillUrgency::Urgent;
    if (RemainingYears <= 25) return NpcLifePillUrgency::Priority;
    if (RemainingYears <= 50) return NpcLifePillUrgency::Regular;
    return NpcLifePillUrgency::None;
}

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
    player.health = mfHealthFromPercent(npc.health, mfHealthMaximum(player, 1.45));
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
    const auto info = artPillUseInfo(player, recipe, 1.45);
    if (!info.reason.empty()) return 0;
    const auto& kind = recipe.kind;
    if (kind == "combat") return beforeBattle && info.effectValue > 0 ? 60 : 0;
    if (beforeBattle) return 0;
    const auto Urgency = mfLifeUrgency(player.lifespan - player.age);
    if ((Urgency == NpcLifePillUrgency::Urgent || Urgency == NpcLifePillUrgency::Critical) &&
        kind != "life" && kind != "detox") return 0;
    if (kind == "breakthrough")
        return !beforeBattle && arts.breakBuff.empty() && player.stage < 12 &&
               player.xp >= realmOf(player.stage).need ? 85 : 0;
    if (kind == "detox") return info.poisonAfter < arts.poison ? (arts.poison >= 40 ? 150 : 0) : 0;
    if (info.effectValue <= 0) return 0;
    if (kind == "heal") {
        const int Maximum = mfHealthMaximum(player, 1.45);
        if (mfHealthToPercent(player.health + static_cast<int>(info.effectValue), Maximum) ==
            mfHealthToPercent(player.health, Maximum)) return 0;
        return player.health < Maximum * 0.7 ? 100 : 70;
    }
    if (kind == "life") {
        switch (Urgency) {
        case NpcLifePillUrgency::Critical: return 140;
        case NpcLifePillUrgency::Urgent: return 130;
        case NpcLifePillUrgency::Priority: return 120;
        case NpcLifePillUrgency::Regular: return 65;
        case NpcLifePillUrgency::None: return 0;
        }
    }
    if (kind == "xp") return beforeBattle ? 0 : 80;
    if (kind == "dao") {
        const auto found = arts.exp.find(recipe.dao);
        if (found != arts.exp.end() && found->second >= ART_THRESHOLDS[5]) return 0;
        return 55;
    }
    if (kind == "points") return arts.points < 10 && !artHas(arts, arts.focus, 5) ? 40 : 0;
    // 属性仍有账面空间、最终战斗/突破贡献已封顶时同样不浪费。
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
    const int HealthPercentBefore = npc.health;
    if (!artApplyPill(player, recipe.id, 1.45).empty()) return false;
    const int HealthPercent = recipe.kind == "heal" ?
        mfHealthToPercent(player.health, mfHealthMaximum(player, 1.45)) : HealthPercentBefore;
    static_cast<Person&>(npc) = static_cast<const Person&>(player);
    npc.health = HealthPercent;
    auto& state = npc.pillState;
    const auto& arts = player.arts;
    state.used = arts.used;
    state.poison = arts.poison; state.aptPills = arts.aptPills;
    state.hpBonus = arts.hpBonus; state.heart = arts.heart;
    state.BaseHealthBonus = arts.BaseHealthBonus;
    state.mind = arts.mind; state.speed = arts.speed;
    state.repel = arts.repel; state.breakBuff = arts.breakBuff;
    state.BreakthroughPillStage = arts.BreakthroughPillStage;
    state.combatPrep = arts.combatPrep; state.combatBuff = arts.combatBuff;
    state.CombatPillDoseCount = arts.CombatPillDoseCount;
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
    arts.aptPills = std::max(0, state.aptPills);
    arts.hpBonus = std::clamp(state.hpBonus, 0, 200);
    arts.BaseHealthBonus = std::max(0, state.BaseHealthBonus);
    arts.heart = std::clamp(state.heart, 0, 500);
    arts.mind = std::clamp(state.mind, 0, 500);
    arts.speed = std::clamp(state.speed, 0, 500);
    arts.points = std::clamp(state.points, 0, 100);
    arts.repel = 0;
    arts.breakBuff = state.breakBuff;
    arts.BreakthroughPillStage = state.BreakthroughPillStage;
    arts.combatPrep = state.combatPrep;
    arts.combatBuff = state.combatBuff;
    arts.CombatPillDoseCount = state.CombatPillDoseCount;
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
    const auto Quote = artPillUseInfo(player, *recipe, 1.45);
    if (!beforeBattle && recipe->kind != "detox" &&
        (player.arts.poison >= 40 || Quote.poisonAfter > 40)) return false;
    return pillPriority(player, *recipe, beforeBattle) > 0 && applyPill(npc, player, *recipe);
}

int npcConsumeUsefulPills(Npc& npc, bool beforeBattle) {
    if (!npc.alive || npc.beast || npc.health <= 0 || npc.stage < 0 || npc.stage > 12) return 0;
    int consumed = 0;
    bool DetoxPhase = !beforeBattle;
    while (!beforeBattle || consumed < 3) {
        Player player = pillProfile(npc);
        const data::RecipeDef* best = nullptr;
        int priority = 0;
        for (const auto& [id, count] : player.arts.potions) {
            const auto* recipe = findRecipe(id);
            if (!recipe || count <= 0) continue;
            const auto Quote = artPillUseInfo(player, *recipe, 1.45);
            if (!beforeBattle) {
                if (DetoxPhase && player.arts.poison >= 40) {
                    if (recipe->kind != "detox") continue;
                } else {
                    if (recipe->kind == "detox" || player.arts.poison >= 40 || Quote.poisonAfter > 40) continue;
                }
            }
            const int candidate = pillPriority(player, *recipe, beforeBattle);
            if (candidate > priority) { best = recipe; priority = candidate; }
        }
        if (!best || !applyPill(npc, player, *best)) break;
        ++consumed;
        // 一次动作只允许开头化毒；之后到40即停，不消耗下一枚化毒绕开节奏。
        if (!beforeBattle && best->kind != "detox") DetoxPhase = false;
        else if (!beforeBattle && npc.pillState.poison < 40) DetoxPhase = false;
    }
    return consumed;
}

NpcLifePillUrgency mfNpcLifePillUrgency(const Npc& Person) {
    return mfLifeUrgency(Person.lifespan - Person.age);
}

int mfAcquireNpcSurvivalPills(Game& State, Npc& Person) {
    if (!Person.alive || Person.beast || Person.health <= 0) return 0;
    int PurchasedCount = 0;
    const auto Acquire = [&](const string& Kind) {
        Player Profile = pillProfile(Person);
        Game Pricing;
        Pricing.world.market = State.world.market;
        Pricing.player = Profile;
        for (const auto& [Id, Count] : Profile.arts.potions) {
            const auto* Recipe = findRecipe(Id);
            if (Recipe && Count > 0 && Recipe->kind == Kind &&
                artPillUseInfo(Profile, *Recipe, 1.45).toleranceRemaining != 0) return false;
        }
        const data::RecipeDef* Selected = nullptr;
        int SelectedPrice = 0;
        for (const auto& Recipe : data::RecipeDef_TABLE()) {
            if (Recipe.kind != Kind || Recipe.grade > artTier(Profile)) continue;
            const auto Info = artPillUseInfo(Profile, Recipe, 1.45);
            if (Info.toleranceRemaining == 0 || Info.effectValue == 0) continue;
            const auto Price = crafting::mfQuotePotionPrice(Pricing, Recipe.id);
            if (!Price.Valid || Price.BuyPrice <= 0 || Price.BuyPrice > Person.stones) continue;
            if (!Selected || Price.BuyPrice < SelectedPrice) { Selected = &Recipe; SelectedPrice = Price.BuyPrice; }
        }
        if (!Selected) return false;
        Person.bag["potion:" + Selected->id] += 1;
        Person.stones -= SelectedPrice;
        return true;
    };
    if (Person.pillState.poison >= 40 && Acquire("detox")) ++PurchasedCount;
    if (mfNpcLifePillUrgency(Person) != NpcLifePillUrgency::None && Person.PillLifeBonus < 500 && Acquire("life")) ++PurchasedCount;
    return PurchasedCount;
}

} // namespace nz
