// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/crafting/cave_garden.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/society.h"
#include "core/adventure.h"
#include "core/dungeon.h"
#include "core/sim.h"
#include "core/tide.h"
#include "core/content/npc_combat_profiles.h"
#include <set>
#include <algorithm>

namespace nz::persistence {

void migrateLegacySave(const Value& root, Game& game) {
    // 旧版本两个聚气神通共用 gather，实际执行总会选到持续聚气。
    // 带版本标记的新档保留基础聚气；旧档只迁移已有牌，不重置斗法。
    if (root["combatRulesVersion"].asInt(1) < 2) {
        auto migrateSpell = [](string& id) { if (id == "gather") id = "qiFlow"; };
        for (auto& id : game.player.arts.spells) migrateSpell(id);
        for (auto& id : game.player.arts.knownSpells) migrateSpell(id);
        if (game.player.arts.battle) {
            Battle& battle = *game.player.arts.battle;
            for (auto& card : battle.deck) migrateSpell(card.spell);
            for (auto& id : battle.enemySpells) migrateSpell(id);
            migrateSpell(battle.intentSpell);
            for (auto* cooldowns : {&battle.cooldowns, &battle.enemyCd}) {
                const auto old = cooldowns->find("gather");
                if (old != cooldowns->end()) {
                    (*cooldowns)["qiFlow"] = old->second;
                    cooldowns->erase(old);
                }
            }
            battle.maxMana = battleEnergy(game.player);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 3) {
        auto& arts = game.player.arts;
        const int legacySwordExp = static_cast<int>(std::min(10000LL, std::max(0LL, static_cast<long long>(game.player.sword) - 2) * 2));
        if (legacySwordExp > arts.exp["剑"]) artGain(game.player, "剑", legacySwordExp - arts.exp["剑"]);
        for (const auto& sp : allSpells()) if (sp.stage <= game.player.stage && std::find(arts.knownSpells.begin(), arts.knownSpells.end(), sp.id) == arts.knownSpells.end()) arts.knownSpells.push_back(sp.id);
        if (arts.battle) {
            auto& battle = *arts.battle;
            const auto& savedBattle = root["player"]["arts"]["battle"];
            if (savedBattle["enemyStage"].isNull()) {
                battle.enemyStage = battle.enemyCards.enabled ? battle.enemyCards.stage : game.player.stage;
                for (const auto& npc : game.npcs) if (npc.id == battle.npcId) { battle.enemyStage = npc.stage; battle.enemySect = npc.sect; battle.enemyGender = npc.gender; break; }
            }
            if (savedBattle["playerSpeed"].isNull()) battle.playerSpeed = artEscapeSpeed(game.player);
            if (savedBattle["enemySpeed"].isNull()) battle.enemySpeed = battle.enemyCards.enabled ? battle.enemyCards.speed : 20 + battle.enemyStage * 8;
            for (const char* spell : {"strike", "defend"}) {
                int copies = 0; for (const auto& c : battle.deck) if (c.spell == spell) ++copies;
                for (; copies < battleBasicCopies(); ++copies) {
                    string uid = "migration-" + string(spell) + "-" + std::to_string(copies);
                    while (std::any_of(battle.deck.begin(), battle.deck.end(), [&](const Card& c) { return c.uid == uid; })) uid += "-";
                    battle.deck.push_back({uid, spell}); battle.draw.push_back(uid);
                }
            }
        }
        std::set<string> migratedKnown;
        arts.knownSpells.erase(std::remove_if(arts.knownSpells.begin(), arts.knownSpells.end(),
            [&](const string& id) { return !migratedKnown.insert(id).second; }), arts.knownSpells.end());
    }
    if (root["combatRulesVersion"].asInt(1) < 4 && game.job && game.job->actionType == "locationService" && game.job->kind == "auction") {
        if (game.job->origin == "tianji") {
            const int refund = std::max(0, game.job->cost);
            game.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(game.player.stones) + refund));
            game.job.reset();
            logEvent(game, "self", "旧版天机珍品会已改为定向材料秘境情报，取消在途购买并退回" + std::to_string(refund) + "灵石。", true);
        } else if (game.job->origin == "dongshi") {
            const int refund = std::max(0, game.job->cost - 24);
            game.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(game.player.stones) + refund));
            game.job->cost = 24;
            if (refund > 0) logEvent(game, "self", "伴月楼在途竞购调整为低阶材料交易，退回旧报价差额" + std::to_string(refund) + "灵石。", true);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 5) migrateDungeonRewards(game);
}

void completeLoadedSave(const Value& root, Game& game, bool detached) {
    // 历史版本漏存了结局；恢复摘要时不能再次发放奖励或重复落笔纪事。
    if (game.ended && !game.ending.has_value()) {
        Ending ending;
        ending.cause = "此世已结束"; ending.year = game.year; ending.age = game.player.age;
        ending.realm = realmName(game.player.stage);
        for (const auto& [id, value] : game.player.relations) {
            if (value >= 45) for (const auto& npc : game.npcs) if (npc.id == id) { ++ending.friends; break; }
        }
        ending.score = game.player.highestStage * 120 + game.player.renown * 4 +
                  game.player.insight + game.player.stones / 4;
        game.ending = std::move(ending);
    }

    // 读档后补齐可能缺失的初始化状态
    // A Tianji encounter is deliberately world-free. World initialization can
    // generate NPCs/dungeons, consume RNG, and rewrite a captured loadout.
    if (!detached) {
        ensureArts(game);
        ensureEconomy(game);
        ensureSociety(game);
        ensureAdventure(game);
        ensureDungeons(game);
        if (root["garden"].isNull()) crafting::mfInitializeGarden(game);
    }

    // 老版在途斗法没有保存任务来源，将能够确定的上下文迁移回来。
    if (game.player.arts.battle.has_value()) {
        Battle& battle = *game.player.arts.battle;
        const Value& saved = root["player"]["arts"]["battle"];
        if (battle.kind == "hunt" && saved["huntFee"].isNull()) {
            if (battle.enemy == "任务妖兽") battle.huntFee = 32 + (game.player.stage / 3) * 30;
            else if (battle.enemy == "兽潮先锋" || battle.enemy == "妖将" || battle.enemy == "妖王麾下")
                battle.kind = "tide";
        }
        if (battle.kind == "wild" && battle.advKind.empty() && saved["wildTask"].isNull()) {
            battle.wildTask = "legacy";
            for (const auto& task : data::WildTaskDef_TABLE())
                if (task.name == battle.enemy) { battle.wildTask = task.id; break; }
        }
    }

    if (!detached) {
        // 仅缺失构筑字段的旧NPC名册需要补模板；现代载入保持原样，包括在途对局。
        const auto& roster=root["npcs"].asArray();
        const bool legacyProfiles=std::any_of(roster.begin(),roster.end(),[](const Value& npc) {
            return npc.asObject().find("combatProfileId")==npc.asObject().end();
        });
        if (legacyProfiles) content::ensureChampionNpcs(game);
        tide::ensureTide(game);
    }

}

} // namespace nz::persistence
