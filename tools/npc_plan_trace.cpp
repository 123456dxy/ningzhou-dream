// 仅诊断：真实普通PvE逐轮记录，不运行产品服务，不读写玩家存档。
#include "core/arts.h"
#include "core/battle.h"
#include "core/content/npc_combat_profiles.h"
#include "core/json.h"
#include <fstream>
#include <iostream>

namespace {
using namespace nz;
using Json = json::Value;
// 用途：记录敌方真实手牌/计划的神通与费用；流程：诊断边界；返回：JSON，不补抽或猜测洗牌。
Json mfCards(const EnemyCombat& Enemy, const vector<string>& Uids, const Player& Actor) {
    json::Array Result;
    for (const auto& Uid : Uids) for (const auto& Card : Enemy.deck) if (Card.uid == Uid) {
        const auto* Spell = battleSpell(Card.spell); if (!Spell) break;
        Json Item(json::Object{}); Item.set("uid", Uid); Item.set("id", Card.spell); Item.set("kind", Spell->kind);
        Item.set("cost", battleCost(Actor, *Spell)); Result.push_back(std::move(Item)); break;
    }
    return Json(std::move(Result));
}
}
// 用途：自然抽牌的普通PvE追踪；流程：NPC后段少动作复现；返回：0完成，不判输赢数值。
int main(int ArgumentCount, char** Arguments) {
    using namespace nz;
    string OutputPath = "tmp/npc-plan-trace-current.json";
    if (ArgumentCount == 3 && string(Arguments[1]) == "--output") OutputPath = Arguments[2];
    else if (ArgumentCount != 1) return 2;
    json::Array Rows;
    std::optional<Game> ShuffleCase;
    for (const auto& Profile : content::npcCombatProfiles()) for (const bool Complete : {false,true}) {
        Game Encounter; Encounter.seed = Encounter.rng = 31;
        Encounter.player.name = "计划追踪靶"; Encounter.player.stage = Encounter.player.highestStage = 12;
        Encounter.player.lifespan = realmOf(12).life; Encounter.player.talent.clear(); Encounter.player.traits.clear();
        Encounter.player.arts.main.clear(); Encounter.player.arts.speed = 200; Encounter.clock.ageBase = 16;
        Npc Opponent; Opponent.id = "npc-trace"; Opponent.name = Profile.name; Opponent.stage = 12;
        Opponent.sect = Profile.sect; Opponent.dao = findSect(Profile.sect)->element;
        Opponent.lifespan = realmOf(12).life; if (Complete) Opponent.combatProfileId = Profile.id;
        Encounter.npcs.push_back(Opponent);
        EnemySpec Enemy; Enemy.id = Opponent.id; Enemy.name = Opponent.name; Enemy.stage = 12; Enemy.sect = Opponent.sect; Enemy.style = Opponent.dao; Enemy.speed = 0;
        battleStart(Encounter, Enemy);
        // 只延长诊断生存，不用于胜率；角色不出牌，不改变敌方抽牌/RNG，不额外发牌。
        Encounter.player.arts.battle->max = Encounter.player.arts.battle->hp = 100000000;
        if (Profile.sect == "xuandao" && Complete) ShuffleCase = Encounter;
        Player Actor; Actor.stage = 12; Actor.talent.clear(); Actor.traits.clear(); Actor.arts = artNpcLoadout(Opponent);
        for (int Round = 0; Round < 18 && battleActive(Encounter); ++Round) {
            const auto& State = *Encounter.player.arts.battle;
            Json Row(json::Object{}); Row.set("sect", Profile.sect); Row.set("complete", Complete); Row.set("turn", State.turn);
            Row.set("mana", State.enemyCards.mana); Row.set("maxMana", State.enemyCards.maxMana);
            Row.set("manaPenalty", State.enemyCards.manaPenalty); Row.set("manaBonus", State.enemyCards.manaBonus);
            Row.set("drawSize", static_cast<int>(State.enemyCards.draw.size())); Row.set("discardSize", static_cast<int>(State.enemyCards.discard.size()));
            Row.set("shuffles", State.enemyCards.shuffles); Row.set("cap", mfBattleEnemyActionLimit(Encounter, State));
            Row.set("hand", mfCards(State.enemyCards, State.enemyCards.hand, Actor));
            Row.set("planned", mfCards(State.enemyCards, State.enemyCards.planned, Actor));
            const auto Preview = battleEnemyPreview(Encounter); json::Array Spells;
            for (const auto& Id : Preview.spells) Spells.push_back(Id);
            Row.set("previewSpells", Json(std::move(Spells)));
            const auto Turn = battleTurn(Encounter, "end"); Row.set("ok", Turn.ok);
            if (Encounter.player.arts.battle) {
                Row.set("afterShuffles", Encounter.player.arts.battle->enemyCards.shuffles);
                Row.set("nextMana", Encounter.player.arts.battle->enemyCards.mana);
            }
            Rows.push_back(std::move(Row));
        }
    }
    if (ShuffleCase) {
        auto& Encounter = *ShuffleCase;
        auto& Enemy = Encounter.player.arts.battle->enemyCards;
        Enemy.hand.clear(); Enemy.draw.clear(); Enemy.discard.clear(); Enemy.planned.clear();
        int Guards = 0, DrawCards = 0;
        for (const auto& Card : Enemy.deck) {
            if ((Card.spell == "defend" && Guards < 4) || (Card.spell == "skim" && DrawCards < 1)) {
                Enemy.draw.push_back(Card.uid);
                if (Card.spell == "defend") ++Guards; else ++DrawCards;
            } else Enemy.discard.push_back(Card.uid);
        }
        battleTurn(Encounter, "end"); // 合法下一轮抽完五张，使draw恰好为空而discard有可抽攻击。
        Player Actor; Actor.stage = 12; Actor.talent.clear(); Actor.traits.clear();
        Npc Opponent; Opponent.sect = "xuandao"; Opponent.stage = 12; Opponent.combatProfileId = "xuandao-complete";
        Actor.arts = artNpcLoadout(Opponent);
        const auto& Before = *Encounter.player.arts.battle;
        Json Row(json::Object{}); Row.set("case", "draw-cross-shuffle"); Row.set("mana", Before.enemyCards.mana);
        Row.set("drawSize", static_cast<int>(Before.enemyCards.draw.size())); Row.set("hand", mfCards(Before.enemyCards, Before.enemyCards.hand, Actor));
        Row.set("planned", mfCards(Before.enemyCards, Before.enemyCards.planned, Actor));
        Row.set("shuffles", Before.enemyCards.shuffles); Row.set("discardSize", static_cast<int>(Before.enemyCards.discard.size()));
        const auto Preview = battleEnemyPreview(Encounter); json::Array PreviewSpells;
        for (const auto& Id : Preview.spells) PreviewSpells.push_back(Id);
        Row.set("previewSpells", Json(std::move(PreviewSpells))); Row.set("previewDamage", Preview.damage);
        const int HpBefore = Before.hp;
        battleTurn(Encounter, "end");
        const auto& After = *Encounter.player.arts.battle;
        Row.set("actualDamage", HpBefore - After.hp);
        Row.set("afterShuffles", After.enemyCards.shuffles); Row.set("afterDiscard", mfCards(After.enemyCards, After.enemyCards.discard, Actor));
        Rows.push_back(std::move(Row));
    }
    std::ofstream Output(OutputPath, std::ios::binary); Output << Json(std::move(Rows)).dump();
    return Output ? 0 : 1;
}
