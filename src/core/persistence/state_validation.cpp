// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/travel.h"
#include "core/npc_learning.h"
#include "core/location_services.h"
#include "core/dungeon.h"
#include "core/sect_trial.h"
#include "core/sim.h"
#include "core/society.h"
#include <set>
#include <algorithm>

namespace nz::persistence {

namespace {
bool validPerson(const Person& person) {
    return !person.name.empty() && person.stage >= 0 && person.stage < static_cast<int>(REALMS().size()) &&
           person.age >= 0 && person.age <= 100000 && person.lifespan > 0 && person.lifespan <= 100000 &&
           person.health >= 0 && person.health <= 100 && person.xp >= 0 && person.stones >= 0 &&
           person.herbs >= 0 && person.pills >= 0 && person.cores >= 0 && person.body >= 0 &&
           person.apt > 0 && person.insight >= 0 && person.sword >= 0 && person.alchemy >= 0 && findLoc(person.location);
}

bool nonnegativeMap(const map<string, int>& values) {
    for (const auto& [key, value] : values) if (value < 0) return false;
    return true;
}
} // namespace

std::string saveStateError(const Game& game) {
    if (const auto error=tideStateError(game); !error.empty()) return error;
    if (!validPerson(game.player)) return "角色属性、境界或地点无效";
    if (!game.ended && (game.player.health == 0 || game.player.age >= game.player.lifespan))
        return "死亡角色尚未结算";
    if (!findPlan(game.plan) || (!game.player.sect.empty() && !findSect(game.player.sect)) ||
        (!game.player.cave.empty() && !findLoc(game.player.cave))) return "角色计划、宗门或洞府无效";
    if (game.player.caveTier < 0 || game.player.caveTier > 2 ||
        game.player.stoneSupply < 0 || game.player.stoneSupply > 3 ||
        game.player.qiSupply < 0 || game.player.qiSupply > game.player.caveTier + 1) return "洞府或配给档位无效";
    if (game.clock.days < 0 || game.year != game.clock.days / 360 ||
        game.player.age != game.clock.ageBase + game.year) return "日历、年份与年龄不一致";
    if (game.world.herbs < 0 || game.world.coreStock < 0 || game.world.pillStock < 0 ||
        !findSect(game.world.mineOwner) || game.world.market <= 0 || game.world.market > 10)
        return "世界库存、灵脉归属或行情无效";
    const Arts& arts = game.player.arts;
    auto gearError = [](const vector<Gear>& gears, const map<string, string>& equipped) -> string {
        std::set<string> ids;
        for (const auto& gear : gears) {
            if (gear.id.empty() || !ids.insert(gear.id).second || gear.grade < 1 || gear.grade > 6 ||
                gear.bonus < 0 || gear.bonus > 10 ||
                (gear.slot != "weapon" && gear.slot != "armor" && gear.slot != "accessory") ||
                (!gear.shape.empty() && artifactShape(gear).empty())) return "法宝标识、器型或品阶无效";
        }
        for (const auto& [slot, id] : equipped) {
            if (id.empty()) continue;
            const auto gear = std::find_if(gears.begin(), gears.end(), [&](const Gear& x) { return x.id == id && x.slot == slot; });
            if (gear == gears.end()) return "已装备法宝引用无效";
        }
        return "";
    };
    if (const auto error = gearError(arts.gear, arts.equipped); !error.empty()) return error;
    if (!nonnegativeMap(arts.herbs) || !nonnegativeMap(arts.potions) ||
        !nonnegativeMap(arts.materials) || !nonnegativeMap(arts.manuals) || arts.points < 0 ||
        arts.poison < 0 || arts.combatBuff < 0) return "物品、功法或道法数量无效";
    std::set<string> known;
    for (const auto& id : arts.knownSpells)
        if (!battleSpell(id) || !known.insert(id).second) return "已领悟神通无效或重复";
    for (const auto& [id, bond] : game.player.npcBonds) {
        if (id.empty() || bond.credit < 0 || bond.spent < 0 || bond.giftsThisMonth < 0 ||
            bond.discussCount < 0 || bond.discussCount > 2 || bond.learnedThisMonth < 0 ||
            bond.learnedThisMonth > 3 || bond.greetYear < -1 || bond.greetYear > game.year)
            return "同道交往记录无效";
        for (const int month : {bond.talkMonth, bond.giftMonth, bond.discussMonth,
                               bond.travelMonth, bond.sparMonth, bond.learnMonth})
            if (month < -1 || month > game.clock.days / 30) return "同道交往月份无效";
    }
    std::set<string> ids;
    for (const auto& npc : game.npcs) {
        if (npc.id.empty() || !ids.insert(npc.id).second || !validPerson(npc) ||
            (!npc.sect.empty() && !findSect(npc.sect)) || !nonnegativeMap(npc.bag)) return "同道数据无效";
        if (const auto error = gearError(npc.gear, npc.equipped); !error.empty()) return error;
    }
    if (game.npcs.size() > 10000 || game.logs.size() > 10000) return "人口或纪事数据过大";
    if (game.job.has_value()) {
        const Job& job = *game.job;
        static const std::set<string> jobs = {
            "plan", "travel", "join", "breakthrough", "social", "socialElapsed",
            "task", "commission", "hunt", "mine", "arena", "quest", "brew", "forge",
            "learn", "meditate", "expedition", "bond", "wild"
            , "npcLearn", "locationService", "story"
        };
        if (!jobs.count(job.actionType) || job.total <= 0 || job.total > 360000 ||
            job.remaining <= 0 || job.remaining > job.total || job.started < 0 ||
            job.started > game.clock.days || job.cost < 0 || job.fee < 0 || job.gift < 0 || job.gift > 1000 ||
            job.batch <= 0 || job.batch > 1000 ||
            job.chance < 0 || job.chance > 1 || job.breakthroughChance < 0 ||
            job.breakthroughChance > 1 || job.breakthroughFailureLoss < 0 ||
            job.breakthroughFailureLoss > 1) return "在途安排参数无效";
        if (job.craftOutput < 1 || job.craftOutput > 4 || job.travelChecks < 0 ||
            job.travelChecks > job.total - job.remaining || job.travelAmbushes < 0 || job.travelAmbushes > 1)
            return "炼制产出或赶路记录无效";
        if (job.actionType == "brew" &&
            ((job.gift != 1 && job.gift != 3 && job.gift != 5 && job.gift != 10) ||
             job.batch != job.gift)) return "炼丹炉数无效或与批次数不一致";
        if (job.actionType == "social" && job.kind == "gift" && job.gift > game.player.herbs)
            return "赠礼安排的药材不足";
        if (job.actionType == "social" && (!job.paid || (job.kind != "talk" && job.kind != "gift") ||
            (job.kind == "gift" && job.gift <= 0))) return "社交安排无效";
        if (job.actionType == "npcLearn") {
            bool matched = false;
            for (const auto& lesson : npcLessons(game, job.npcId))
                if (lesson.kind == job.kind && lesson.id == job.plan && job.paid &&
                    job.cost == lesson.stoneCost && job.gift == lesson.creditCost && job.total == lesson.days)
                    matched = true;
            if (!matched) return "授业内容、费用或用时无效";
        }
        if (job.actionType == "story") {
            bool matched = false;
            for (const auto& story : data::StoryDef_TABLE()) {
                if (story.id != job.plan) continue;
                const int choice = job.kind == "0" ? 0 : job.kind == "1" ? 1 : -1;
                matched = choice >= 0 && job.paid && job.cost == 0 && job.total == story.days &&
                    game.clock.days - job.started == job.total - job.remaining &&
                    job.origin == game.player.location && storyError(game, &story).empty() &&
                    storyChoiceError(game, story, choice).empty();
            }
            if (!matched) return "在途因缘的内容、选项或用时无效";
        }
        if (job.actionType == "locationService") {
            bool matched = false;
            for (const auto& service : locationServices(game, job.origin))
                if (service.id == job.kind && service.targetTab.empty() && job.paid &&
                    job.cost == service.cost && job.total == service.days) matched = true;
            if (!matched || job.origin != game.player.location) return "当地事务内容或出发地点无效";
        }
        if ((job.actionType == "plan" && !findPlan(job.plan)) ||
            (job.actionType == "travel" && !findLoc(job.destination)) ||
            (job.actionType == "join" && !findSect(job.sect))) return "在途安排的目标无效";
        if (job.actionType == "travel" &&
            (!findLoc(job.origin) || job.origin != game.player.location ||
             !travelModeKnown(job.travelMode) ||
             (job.travelMode == "flight" && game.player.stage < 3) ||
             (job.travelMode == "walk" && (job.origin == "sea" || job.destination == "sea"))))
            return "赶路出发地或方式无效";
    }
    if (arts.battle.has_value()) {
        const Battle& battle = *arts.battle;
        if (battle.tideBattlefield < -1 || battle.tideBattlefield > 3 || battle.tideDaysCharged < 0 || battle.tideDaysCharged > 24 ||
            (!battle.tideEventId.empty() && (battle.kind != "tide" || battle.tideEventId != game.tide.eventId)))
            return "兽潮对局来源或记日无效";
        const bool terminalShadow = battle.shadowPlayer && !battle.shadowOutcome.empty();
        if (battle.shadowPlayer) {
            const Player& shadow = *battle.shadowPlayer;
            if (battle.kind != "tianji" || battle.lethal || !battle.npcId.empty() || !battle.quest.empty() ||
                !battle.advKind.empty() || !battle.dungeonSite.empty() || !battle.wildTask.empty() ||
                !battle.enemyCards.enabled || battle.enemyBeast || shadow.arts.battle || !validPerson(shadow) ||
                (battle.shadowOutcome != "" && battle.shadowOutcome != "win" && battle.shadowOutcome != "loss" &&
                 battle.shadowOutcome != "draw" && battle.shadowOutcome != "retreat")) return "天机留影状态无效";
            if (battle.enemy != shadow.name || battle.enemyStage != shadow.stage || battle.enemySect != shadow.sect ||
                battle.enemyCards.main != shadow.arts.main || battle.enemyCards.aux != shadow.arts.aux ||
                battle.enemyCards.manuals != shadow.arts.manuals || battle.enemyCards.nodes != shadow.arts.nodes ||
                battle.enemyCards.mind != shadow.arts.mind || battle.enemyCards.body != shadow.body)
                return "天机留影构筑不一致";
            if (const auto error = gearError(shadow.arts.gear, shadow.arts.equipped); !error.empty()) return error;
            for (const auto& trait : shadow.traits) if (!findTalent(trait)) return "天机留影天赋无效";
            for (const auto& id : shadow.arts.spells) if (!battleSpell(id)) return "天机留影神通无效";
            if (battle.turn > 24 || battle.enemyCards.turns < 0 || battle.enemyCards.turns > 24 ||
                (!terminalShadow && battle.turn >= 24) ||
                (battle.shadowOutcome == "win" && (battle.enemyHp > 0 || battle.hp <= 0)) ||
                (battle.shadowOutcome == "loss" && battle.hp > 0) ||
                (battle.shadowOutcome == "draw" && !((battle.turn == 24 && battle.hp > 0 && battle.enemyHp > 0) ||
                                                (battle.hp <= 0 && battle.enemyHp <= 0)))) return "天机切磋结算无效";
        }
        if (battle.max <= 0 || battle.enemyMax <= 0 || (!terminalShadow && battle.hp <= 0) || battle.hp > battle.max ||
            (!terminalShadow && battle.enemyHp <= 0) || battle.enemyHp > battle.enemyMax || battle.mana < 0 || battle.maxMana < 0 ||
            battle.mana > 100000 || battle.maxMana > 100000 ||
            battle.manaBonus < 0 || battle.manaBonus > 100000 || battle.manaPenalty < 0 || battle.manaPenalty > 100000 ||
            battle.carryMana < 0 || battle.carryMana > 100000 || battle.carryCap < 0 || battle.carryCap > 100000 ||
            battle.drawNext < 0 || battle.drawNext > 256 || battle.pendingDraw < 0 || battle.pendingDraw > 256 ||
            battle.enemyPower < 0 || battle.turn < 0 || battle.buff < 0 || battle.reduction < 0 || battle.guard < 0 ||
            battle.deck.size() > 256 || battle.hand.size() > 8 || battle.played < 0 || battle.played > 24 ||
            battle.discardPower < 0 || battle.discardPower > 0.3 + 1e-9 ||
            battle.swordSequence < 0 || battle.swordSequence > 24 || battle.comboDraws < 0 || battle.comboDraws > 2)
            return "斗法参数无效";
        if (battle.enemyStage < 0 || battle.enemyStage > 12 || battle.playerSpeed < 0 || battle.enemySpeed < 0)
            return "斗法境界或遁速无效";
        if (battle.poisonDamage < -1 || battle.poisonDamage > BATTLE_BURN_CAP ||
            battle.enemyCards.poisonDamage < -1 || battle.enemyCards.poisonDamage > BATTLE_BURN_CAP)
            return "斗法毒伤数值无效";
        if (battle.poison < 0 || battle.poison > BATTLE_POISON_CAP || battle.poisonTurns < 0 || battle.poisonTurns > 3 ||
            battle.burn < 0 || battle.burn > BATTLE_BURN_CAP || battle.burnTurns < 0 || battle.burnTurns > 3 ||
            battle.playerPoison < 0 || battle.playerPoison > BATTLE_BURN_CAP || battle.playerPoisonTurns < 0 || battle.playerPoisonTurns > 3)
            return "斗法持续伤害状态无效";
        for (const auto& [id, cooldown] : battle.cooldowns)
            if ((!battleSpell(id) && !equippedArtifactAbility(game.player, id)) || cooldown < 0 || cooldown > 24)
                return "神通或法宝冷却无效";
        std::set<string> cards;
        for (const auto& c : battle.deck) {
            if (c.uid.empty() || !battleSpell(c.spell) || !cards.insert(c.uid).second) return "斗法牌组无效";
        }
        std::set<string> placed;
        for (const auto* pile : {&battle.hand, &battle.draw, &battle.discard, &battle.exhausted}) {
            for (const auto& id : *pile) {
                if (!cards.count(id) || !placed.insert(id).second) return "斗法牌堆重复或引用未知牌";
            }
        }
        const EnemyCombat& enemy = battle.enemyCards;
        std::set<string> enemyCards, enemyPlaced, planned;
        if (enemy.deck.size() > 256 || enemy.hand.size() > 8 || enemy.planned.size() > 24) return "对手牌组过大";
        for (const auto& c : enemy.deck) {
            const auto* spell = battleSpell(c.spell);
            if (c.uid.empty() || !spell || !enemyCards.insert(c.uid).second ||
                (enemy.enabled && spell->stage > enemy.stage)) return "对手牌组无效";
        }
        for (const auto* pile : {&enemy.hand, &enemy.draw, &enemy.discard, &enemy.exhausted})
            for (const auto& id : *pile)
                if (!enemyCards.count(id) || !enemyPlaced.insert(id).second) return "对手牌堆引用无效或重复";
        if (enemyPlaced.size() != enemyCards.size()) return "对手牌组存在未归入牌堆的神通";
        for (const auto& id : enemy.planned) {
            if (battle.shadowPlayer) {
                // Draw/redraw may reshuffle and replay a UID. Artifact actions have no card UID.
                if (!enemyCards.count(id) && !equippedArtifactAbility(*battle.shadowPlayer, id))
                    return "留影行动计划引用无效";
            } else if (!planned.insert(id).second ||
                (std::find(enemy.hand.begin(), enemy.hand.end(), id) == enemy.hand.end() &&
                 std::find(enemy.draw.begin(), enemy.draw.end(), id) == enemy.draw.end())) return "对手行动计划无效";
        }
        if (enemy.enabled) {
            const int manaCap = battle.shadowPlayer ? battleManaCap(*battle.shadowPlayer) : 30;
            if (battle.enemyBeast || enemy.deck.empty() || enemy.stage != battle.enemyStage || enemy.speed != battle.enemySpeed ||
                enemy.stage < 0 || enemy.stage > 12 || enemy.mana < 0 || enemy.mana > manaCap || enemy.maxMana <= 0 || enemy.maxMana > manaCap ||
                enemy.carryMana < 0 || enemy.carryCap < 0 || enemy.carryCap > manaCap || enemy.carryMana > enemy.carryCap ||
                enemy.played < 0 || enemy.played > 24 || enemy.drawNext < 0 || enemy.drawNext > 256 ||
                enemy.swordSequence < 0 || enemy.swordSequence > 24 || enemy.comboDraws < 0 || enemy.comboDraws > 2 ||
                enemy.swordQi < 0 || enemy.swordQi > 8 || enemy.water < 0 || enemy.water > 6 || enemy.soul < 0 || enemy.soul > 6 ||
                enemy.poison < 0 || enemy.poison > BATTLE_POISON_CAP || enemy.charge < 0 || enemy.charge > 4 || enemy.lock < 0 || enemy.lock > 2 ||
                enemy.poisonTurns < 0 || enemy.poisonTurns > 3 || enemy.burn < 0 || enemy.burn > BATTLE_BURN_CAP || enemy.burnTurns < 0 || enemy.burnTurns > 3 ||
                enemy.buff < 0 || enemy.buff > 0.3 + 1e-9 || enemy.reduction < 0 || enemy.reduction > 0.2 + 1e-9 ||
                enemy.guard < 0 || enemy.guard > 0.1 + 1e-9 || enemy.discardPower < 0 || enemy.discardPower > 0.3 + 1e-9)
                return "对手斗法参数无效";
            for (const int value : {enemy.speed, enemy.body, enemy.mind, enemy.shuffles, enemy.manaBonus, enemy.manaPenalty, enemy.hpCostPct})
                if (value < 0 || value > 100000) return "对手斗法属性无效";
            for (const auto& [id, cooldown] : enemy.cooldowns)
                if ((!battleSpell(id) && !(battle.shadowPlayer && equippedArtifactAbility(*battle.shadowPlayer, id))) ||
                    cooldown < 0 || cooldown > 24) return "对手神通冷却无效";
            for (const auto& [id, level] : enemy.manuals) {
                ManualInfo info;
                if (!findManualInfo(id, info) || level < 1 || level > 5) return "对手功法无效";
            }
            if (!enemy.manuals.count(enemy.main) && !(battle.shadowPlayer && enemy.main.empty())) return "对手主修功法未领悟";
            std::set<string> aux, nodes;
            for (const auto& id : enemy.aux)
                if (!enemy.manuals.count(id) || !aux.insert(id).second) return "对手辅修功法无效";
            for (const auto& id : enemy.nodes)
                if (!findNode(id) || !nodes.insert(id).second) return "对手悟道节点无效";
        }
    }
    if (game.pending.has_value()) {
        const Pending& person = *game.pending;
        if (person.type != "sectInvitation" && person.type != "valley" && person.type != "adventure")
            return "抉择类型无效";
        if (person.options.empty() || (person.type == "adventure" && !person.adventure.has_value()))
            return "抉择内容不完整";
        std::set<string> choices;
        for (const auto& object : person.options)
            if (object.id.empty() || !choices.insert(object.id).second) return "抉择选项无效";
    }
    if ((game.pending.has_value() && arts.battleActive()) ||
        (game.ended && (game.job.has_value() || game.pending.has_value() || arts.battleActive())))
        return "存档包含互相冲突的行动状态";
    if (const string error = dungeonStateError(game); !error.empty()) return error;
    if (const string error = sectTrialStateError(game); !error.empty()) return error;
    if (arts.battle && ((!arts.battle->dungeonSite.empty() || !arts.battle->dungeonNode.empty()) && !game.dungeons.run))
        return "秘境斗法缺少在途探索状态";
    const auto& checkpoint = game.checkpoint;
    if (checkpoint.days < 0 || checkpoint.days > game.clock.days || checkpoint.lastAutoYear < 0 ||
        checkpoint.lastAutoYear % 10 != 0 || checkpoint.lastAutoYear > game.year || checkpoint.restores < 0 ||
        checkpoint.slot < 1 || checkpoint.slot > 4 || (checkpoint.source != "auto" && checkpoint.source != "manual") ||
        checkpoint.snapshot.size() > 8u * 1024u * 1024u) return "死亡回档记录无效";
    if (checkpoint.enabled != !checkpoint.snapshot.empty()) return "启用死亡回档必须包含完整且非嵌套的存档快照";
    if (checkpoint.enabled) {
        string parseError;
        const Value snapshot = Value::parse(checkpoint.snapshot, &parseError);
        // 先拒绝任何内层快照，再做一次普通读档验证；不会递归遍历嵌套快照。
        if (!parseError.empty() || !snapshot.isObject() || snapshot["lifeId"].asString() != game.lifeId ||
            !snapshot["checkpoint"]["snapshot"].asString().empty() || snapshot["checkpoint"]["enabled"].asBool())
            return "死亡回档快照嵌套、格式错误或属于其他人生";
        Game restored;
        if (!loadFromJson(checkpoint.snapshot, restored, &parseError) || restored.lifeId != game.lifeId || restored.ended ||
            restored.clock.days != checkpoint.days || restored.clock.days > game.clock.days)
            return "死亡回档快照内容无效或晚于当前进度";
    }
    return "";
}

} // namespace nz::persistence
