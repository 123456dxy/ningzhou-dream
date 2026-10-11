#include "harness.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
namespace {
nz::Game combatGame() {
    nz::CreateOptions options;
    options.seed = 20261011;
    nz::Game g = nz::createGame(options);
    g.pending.reset();
    g.player.stage = g.player.highestStage = 6;
    g.player.lifespan = nz::realmOf(6).life + g.player.lifeBonus;
    g.player.arts.spells = {"purge", "gather", "skim"};
    nz::ensureArts(g);
    nz::EnemySpec enemy;
    enemy.name = "存档回归靶子";
    nz::battleStart(g, enemy);
    return g;
}
void putInHand(nz::Battle& b, const std::string& id) {
    b.hand.clear(); b.draw.clear(); b.discard.clear(); b.exhausted.clear();
    bool selected = false;
    for (const auto& card : b.deck) {
        if (!selected && card.spell == id) {
            b.hand.push_back(card.uid); selected = true;
        } else b.draw.push_back(card.uid);
    }
}
}

void runBattleStateTests() {
    using namespace nz;
    std::printf("\n--- 新战斗规则存档回归 ---\n");
    Game g = combatGame();
    putInHand(*g.player.arts.battle, "purge");
    g.player.arts.battle->mana = 10;
    check(battleTurn(g, "purge", "strike").ok, "移除普通攻击可正常执行");
    Battle& b = *g.player.arts.battle;
    checkEq(b.exhausted.size(), 5, "所有普通攻击副本均进入本场移除堆");
    b.discardPower = 0.125;
    b.swordSequence = 2;
    b.comboDraws = 1;
    Game back;
    std::string err;
    check(loadFromJson(saveToJson(g), back, &err), "新战斗四个牌堆可读回：" + err);
    if (back.player.arts.battle) {
        const Battle& restored = *back.player.arts.battle;
        check(restored.exhausted == b.exhausted, "读档保留本场移除的全部副本");
        checkNear(restored.discardPower, 0.125, "弃牌增伤小数保存完整");
        checkEq(restored.swordSequence, 2, "读档保留连续剑牌计数");
        checkEq(restored.comboDraws, 1, "读档保留当回合相生触发次数");
        check(saveToJson(back) == saveToJson(g), "新战斗全部字段再次序列化稳定");
        battleDraw(back, 50);
        for (const auto& uid : back.player.arts.battle->hand) {
            const auto found = std::find_if(back.player.arts.battle->deck.begin(),
                back.player.arts.battle->deck.end(), [&](const Card& c) { return c.uid == uid; });
            check(found == back.player.arts.battle->deck.end() || found->spell != "strike",
                  "存读后洗牌不会抽回本场已移除神通");
        }
    }

    auto corrupt = json::Value::parse(saveToJson(g));
    auto player = corrupt["player"];
    auto arts = player["arts"];
    auto battle = arts["battle"];
    json::Array duplicate;
    duplicate.push_back(json::Value(b.hand.front()));
    battle.set("exhausted", json::Value(std::move(duplicate)));
    arts.set("battle", battle); player.set("arts", arts); corrupt.set("player", player);
    const std::string before = saveToJson(back);
    check(!loadFromJson(corrupt.dump(), back, &err), "同一张牌不能同时在手牌和移除堆");
    check(saveToJson(back) == before, "损坏移除堆不会替换当前角色");
    battle.set("exhausted", json::Value("not an array"));
    arts.set("battle", battle); player.set("arts", arts); corrupt.set("player", player);
    check(!loadFromJson(corrupt.dump(), back, &err), "移除堆字段类型错误被拒绝");

    Game legacy = combatGame();
    legacy.player.arts.battle->cooldowns["gather"] = 2;
    auto document = json::Value::parse(saveToJson(legacy));
    check(loadFromJson(document.dump(), back, &err), "新规则聚气存档可读回");
    check(std::find(back.player.arts.spells.begin(), back.player.arts.spells.end(), "gather") !=
          back.player.arts.spells.end(), "新档基础聚气不会被误转成持续聚气");
    document.asObject().erase("combatRulesVersion");
    const bool migrated = loadFromJson(document.dump(), back, &err);
    check(migrated, "旧聚气存档可迁移：" + err);
    check(std::find(back.player.arts.spells.begin(), back.player.arts.spells.end(), "qiFlow") !=
          back.player.arts.spells.end(), "旧聚气保留原持续加灵效果");
    check(back.player.arts.battle && back.player.arts.battle->cooldowns.count("qiFlow"),
          "旧斗法聚气冷却与牌组同步迁移");
}
}
