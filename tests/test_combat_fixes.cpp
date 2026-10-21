#include "harness.h"

#include <algorithm>
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/content/beast_portraits.h"
#include "core/crafting/material_supply.h"
#include "core/interface/views.h"
#include "core/npc_learning.h"
#include "core/save.h"
#include "core/sword.h"

using namespace nz;

namespace nztest {
namespace {

Game combatFixture() {
    Game game;
    game.seed = game.rng = 20261012;
    game.player.stage = 12;
    ensureArts(game);
    game.player.arts.main.clear();
    game.player.arts.aux.clear();
    game.player.arts.manuals.clear();
    game.player.arts.equipped.clear();
    game.player.arts.nodes.clear();
    game.player.arts.spells = {"sword", "gather"};
    game.player.health = mfPlayerHealthMaximum(game.player);
    EnemySpec enemy;
    enemy.name = "灵力语义回归";
    enemy.stage = 12;
    enemy.beast = true;
    battleStart(game, enemy);
    Battle& battle = *game.player.arts.battle;
    battle.enemyHp = battle.enemyMax = 1000000;
    battle.BeastActions.clear();
    battle.intentKind = "guard";
    battle.intentRaw = 0;
    return game;
}

void putInHand(Battle& battle, const string& spell) {
    battle.hand.clear();
    battle.draw.clear();
    battle.discard.clear();
    for (const auto& card : battle.deck) {
        if (card.spell == spell && battle.hand.empty()) battle.hand.push_back(card.uid);
        else battle.draw.push_back(card.uid);
    }
}

} // namespace

void runCombatFixTests() {
    std::printf("\n-- 自然回灵总量与即时回灵增量 --\n");
    for (const int current : {0, 2, 11}) {
        Game game = combatFixture();
        Battle& battle = *game.player.arts.battle;
        battle.mana = current;
        battle.maxMana = 5;
        battle.manaBonus = 2;
        battle.manaPenalty = 1;
        battle.lock = 2;
        battle.carryMana = 3;
        battle.played = 3;
        const string before = saveToJson(game);
        const auto preview = api::view::battle(game);
        checkEq(preview["nextMana"].asInt(), 8, "下轮灵力显示含增益、代价、锁定和封存的最终总量");
        checkStr(saveToJson(game), before, "读取下轮灵力预览不改变战斗或随机数");
        check(battleTurn(game, "end").ok, "带剩余灵力的回合可以结束");
        checkEq(battle.mana, preview["nextMana"].asInt(), "自然回灵设为预览总量，不叠加本轮剩余灵力");
        checkEq(battle.carryMana, 0, "自然回灵后封存仅结转一次");
        checkEq(battle.lock, 1, "本轮锁定参与回灵后正常递减");
        check(std::any_of(battle.log.begin(), battle.log.end(), [](const string& line) {
            return line.find("灵力恢复到8点") != string::npos;
        }), "自然回灵战报明确恢复到最终总量");
    }
    {
        Game game = combatFixture();
        Battle& battle = *game.player.arts.battle;
        battle.mana = 10;
        battle.maxMana = 30;
        battle.carryMana = 20;
        const int predicted = api::view::battle(game)["nextMana"].asInt();
        checkEq(predicted, battleManaCap(game.player), "自然回灵总量仍受真实灵力容量限制");
        battleTurn(game, "end");
        checkEq(battle.mana, predicted, "容量截断后的预览与实际恢复到的值一致");
    }
    for (const int current : {2, 11, 12}) {
        Game game = combatFixture();
        Battle& battle = *game.player.arts.battle;
        putInHand(battle, "gather");
        battle.mana = current;
        const Preview preview = battlePreview(game.player, battle, "gather");
        const int expected = std::min(battleManaCap(game.player), current + 1);
        checkEq(preview.energy, expected - current, "聚气牌预览仍是受容量限制的即时增量");
        check(battleTurn(game, "gather").ok, "聚气牌可以施放");
        checkEq(battle.mana, expected, "聚气牌即时恢复在剩余灵力上相加");
        checkEq(battle.mana - current, preview.energy, "聚气牌预览增量与真实出牌一致");
    }

    std::printf("\n-- 旧剑意兼容与统一悟道战斗 --\n");
    {
        Game ordinary = combatFixture();
        Battle& plain = *ordinary.player.arts.battle;
        putInHand(plain, "sword");
        plain.mana = 12;
        plain.intentKind = "attack";
        plain.intentRaw = 100;
        ordinary.player.sect = "jinhong";
        ordinary.player.sword = 0;
        plain.enemySwordCult = true;
        plain.enemySword = 500;
        Game legacyHigh = ordinary;
        legacyHigh.player.sword = 500;
        legacyHigh.player.arts.battle->enemySword = 0;
        checkNear(battleSwordBonus(legacyHigh.player), 0, "旧永久剑意加成入口仍为零");
        const Preview low = battlePreview(ordinary.player, plain, "sword");
        const Preview high = battlePreview(legacyHigh.player, *legacyHigh.player.arts.battle, "sword");
        checkEq(low.damage, high.damage, "旧剑意双方境差不影响剑招伤害预览");
        checkEq(api::view::battle(ordinary)["intentDamage"].asInt(),
                api::view::battle(legacyHigh)["intentDamage"].asInt(), "旧剑意境差不影响敌方来伤预览");
        battleTurn(ordinary, "sword");
        battleTurn(legacyHigh, "sword");
        checkEq(plain.enemyHp, legacyHigh.player.arts.battle->enemyHp, "旧剑意境差不影响真实剑招伤害");
        battleTurn(ordinary, "end");
        battleTurn(legacyHigh, "end");
        checkEq(plain.hp, legacyHigh.player.arts.battle->hp, "旧剑意境差不影响真实来伤");
        const int oldSword = legacyHigh.player.sword;
        const int oldExp = legacyHigh.player.arts.exp["剑"];
        swordGain(legacyHigh, 5);
        checkEq(legacyHigh.player.sword, oldSword, "剑道活动保留旧档剑意值");
        check(legacyHigh.player.arts.exp["剑"] > oldExp, "剑道活动仍给予统一悟道经验");
    }

    std::printf("\n-- 斗法肖像与真实 NPC 身份一致 --\n");
    {
        Game game = combatFixture();
        Npc npc;
        npc.name = "同一位修士";
        npc.gender = "男";
        npc.sect = "jinhong";
        for (int index = 0; index < 100; ++index) {
            npc.id = "portrait-regression-" + std::to_string(index);
            if (npcPortrait(npc) != personPortrait(npc.gender, npc.sect)) break;
        }
        check(npcPortrait(npc) != personPortrait(npc.gender, npc.sect), "真实 NPC 回归样本选中已生成的画像变体");
        game.npcs.push_back(npc);
        Battle& battle = *game.player.arts.battle;
        battle.enemyBeast = false;
        battle.npcId = npc.id;
        battle.enemyGender = npc.gender;
        battle.enemySect = npc.sect;
        checkStr(api::view::battle(game)["enemyPortrait"].asString(), npcPortrait(npc), "斗法肖像与同一 NPC 的稳定画像一致");
        battle.enemyGender = "女";
        battle.enemySect = "lihuo";
        checkStr(api::view::battle(game)["enemyPortrait"].asString(), npcPortrait(npc), "真实 NPC 肖像优先使用名册实体，兼容旧战斗缺失属性");
        battle.enemyBeast = true;
        checkStr(api::view::battle(game)["enemyPortrait"].asString(),
                 Appearance::mfBeastPortraitPath(battle.enemyDao.empty() ? battle.style : battle.enemyDao,
                                               battle.enemyStage),
                 "妖兽斗法肖像按真实风格与妖兽境界选择");
    }

    std::printf("\n-- 年度炼器保留真实配料来源 --\n");
    {
        Game game;
        game.seed = game.rng = 1;
        game.year = 1;
        game.player.health = 100;
        game.player.stones = 10000;
        ensureArts(game);
        game.player.arts.materials["iron"] = 10;
        artAnnual(game, "forge", "iron|iron|iron|sword");
        checkEq(game.player.arts.gear.size(), 1, "固定种子年度炼器成功生成一件法器");
        checkEq(game.player.arts.materials["iron"], 0, "同材料三角色仍按原玩法总消耗十份");
        if (!game.player.arts.gear.empty()) {
            const auto quote = crafting::salvageQuote(game, game.player.arts.gear.front().id);
            check(quote.enabled && !quote.estimated, "年度炼器的新法器可按真实配料分解");
            checkEq(quote.materials.size(), 1, "分解合并同材料的三种炼器角色");
            if (!quote.materials.empty()) {
                checkStr(quote.materials.front().id, "iron", "分解返还原配料");
                checkEq(quote.materials.front().required, 5, "真实十份同材料配料返还五份");
            }
        }
    }
}

} // namespace nztest
