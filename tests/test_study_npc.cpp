#include "harness.h"

#include <algorithm>
#include <limits>

#include "core/arts.h"
#include "core/battle.h"
#include "core/cultivate.h"
#include "core/combat/battle_scaling.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/progression/manual_study.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/social/npc_consumables.h"

using namespace nz;

namespace nztest {
namespace {
Game studyGame() {
    Game game = createGame(CreateOptions{});
    game.pending.reset(); game.npcs.clear();
    game.player.stage = game.player.highestStage = 6;
    game.player.lifespan = realmOf(6).life + game.player.lifeBonus;
    game.player.insight = 10; game.player.traits.clear(); game.player.talent.clear();
    game.player.stones = 10000; game.player.arts.manuals.clear();
    game.economy.books = {"tuna", "water", "zhou"};
    return game;
}

Npc pillNpc(int stage = 6) {
    Npc npc;
    npc.id = "pill-test"; npc.name = "试丹道友";
    npc.stage = stage; npc.lifespan = realmOf(stage).life;
    npc.dao = "水";
    return npc;
}
} // namespace

void runStudyNpcTests() {
    std::printf("\n-- 功法参悟与 NPC 服丹 --\n");
    {
        Game game = studyGame();
        const string before = saveToJson(game);
        const auto base = manualStudyInfo(game.player, "tuna");
        const auto tier2 = manualStudyInfo(game.player, "water");
        const auto tier3 = manualStudyInfo(game.player, "zhou");
        checkEq(base.days, 40, "一阶第一层参悟基准四十天");
        checkEq(base.stones, 24, "一阶第一层参悟基准二十四灵石");
        checkEq(tier2.days, base.days * 2, "相邻功法品阶耗时翻倍");
        checkEq(tier3.stones, tier2.stones * 2, "相邻功法品阶灵石翻倍");
        checkStr(saveToJson(game), before, "参悟报价不改状态或随机数");
        for (int level = 0; level < 5; ++level) {
            game.player.arts.manuals["tuna"] = level;
            const auto quote = manualStudyInfo(game.player, "tuna");
            checkEq(quote.days, static_cast<int>(std::ceil(40 * std::pow(1.2, level) - 1e-9)), "功法逐层耗时指数增长");
            checkEq(quote.stones, static_cast<int>(std::ceil(24 * std::pow(1.2, level) - 1e-9)), "功法逐层灵石指数增长");
        }
        game.player.arts.manuals["tuna"] = 2;
        const auto quote = ecoActionInfo(game, "learn", "tuna");
        const int stones = game.player.stones;
        ecoBegin(game, "learn", "tuna");
        check(game.job.has_value(), "参悟创建真实安排");
        if (game.job) {
            const Job job = *game.job;
            checkEq(job.total, quote.days, "开工耗时等于只读报价");
            checkEq(stones - game.player.stones, quote.cost, "开工真实扣报价灵石");
            const int paid = game.player.stones;
            ecoComplete(game, job);
            checkEq(game.player.arts.manuals["tuna"], 3, "完成只提升一层");
            checkEq(game.player.stones, paid, "完成时不会二次扣灵石");
        }
    }
    {
        Game game = studyGame();
        game.player.arts.manuals["tuna"] = std::numeric_limits<int>::max();
        const auto capped = manualStudyInfo(game.player, "tuna");
        check(capped.maxed && capped.days == 0 && capped.stones == 0, "异常超大层数封顶而不进入无界指数");
        check(!ecoActionError(game, "learn", "tuna").empty(), "满层参悟明确拒绝");
        game.player.arts.manuals["tuna"] = std::numeric_limits<int>::min();
        checkEq(manualStudyInfo(game.player, "tuna").stones, 24, "负层数安全钳制到第一层报价");
        game.player.insight = std::numeric_limits<int>::min();
        check(cultivate::learnDays(game.player, std::numeric_limits<int>::max(),
                                  std::numeric_limits<int>::max()) < 3000, "异常悟性与品阶不会溢出");
        game.player.arts.manuals["tuna"] = 0; game.player.insight = 10;
        const int regular = manualStudyInfo(game.player, "tuna").days;
        game.player.traits = {"savvy"};
        checkEq(manualStudyInfo(game.player, "tuna").days, 32, "聪颖天赋仍提供八折时间");
        game.player.insight = 50;
        check(manualStudyInfo(game.player, "tuna").days < regular, "悟性折扣仍真实降低时间");
        check(!manualStudyInfo(game.player, "missing-manual").valid, "未知功法没有报价");
    }
    {
        Npc npc = pillNpc(); npc.health = 50;
        npc.bag = {{"potion:5001", 20}, {"potion:missing", 2}, {"potion:5002", -3}};
        const int Taken = npcConsumeUsefulPills(npc);
        check(Taken > 3 && Taken <= 20, "一次真实日常动作不再受三枚上限限制");
        checkEq(npc.health, 100, "疗伤按实际缺口补至满血");
        checkEq(npc.bag["potion:5001"], 20 - Taken, "服药真正扣NPC行囊对应枚数");
        checkEq(npc.pillState.used["5001"], Taken, "疗伤丹允许耐药范围内多次使用");
        checkEq(npc.pillState.poison, Taken, "NPC服丹同步记录丹毒");
        checkEq(npcConsumeUsefulPills(npc), 0, "满血、负库存与未知丹药不会浪费或崩溃");
        npc.health = 50; npc.pillState.poison = 80;
        check(!npcUsePill(npc, "5001"), "高丹毒禁止继续服普通丹药");
        npc.bag["potion:5316"] = 2; npc.bag["potion:5001"] = 20;
        check(npcConsumeUsefulPills(npc) > 2, "高丹毒先连续化毒至节奏线内，再处理伤势");
        checkEq(npc.bag["potion:5316"], 0, "化毒真实扣库存");
        check(npc.pillState.poison > 0 && npc.pillState.poison <= 40, "化毒后普通服药仍受40硬边界");
    }
    {
        Npc npc = pillNpc(12); npc.apt = 190;
        npc.bag = {{"potion:5416", 2}, {"potion:5514", 2}, {"potion:5207", 4}};
        check(npcUsePill(npc, "5416"), "资质丹可实际生效");
        check(npcUsePill(npc, "5514"), "另一种资质丹不受旧累计额度限制");
        checkEq(npc.pillState.aptPills, 21, "NPC保留真实累计提升统计，不限制为10");
        checkEq(npc.apt, 211, "NPC资质丹可超过旧总属性200");
        check(!npcUsePill(npc, "5416"), "每味自己的耐药仍然生效");
        npc.pillState.used["5207"] = 3;
        check(!npcUsePill(npc, "5207"), "已达到耐药次数的丹药保留库存");
        npc.lifeBonus = npc.PillLifeBonus = 500; npc.lifespan = realmOf(12).life + 500;
        npc.age = npc.lifespan - 4;
        npc.bag["potion:5517"] = 1;
        check(!npcUsePill(npc, "5517"), "丹药寿元500年上限时不浪费续命丹");
        npc.age = 16;
        npc.insight = INSIGHT_MAX; npc.bag["potion:5513"] = 1;
        check(npcUsePill(npc, "5513") && npc.insight > INSIGHT_MAX, "悟性丹不再被旧总属性上限截断");
    }
    {
        Npc npc = pillNpc(8);
        npc.bag = {{"potion:5407", 1}, {"potion:5409", 1}, {"potion:5209", 1}};
        check(!npcUsePill(npc, "5407"), "水流派不服金道丹");
        check(npcUsePill(npc, "5409"), "水流派优先吸收水道丹");
        checkEq(npc.pillState.daoExp["水"], 60, "悟道丹记录真实追加经验");
        const int MaximumBefore = mfNpcHealthMaximum(npc);
        check(npcUsePill(npc, "5209"), "通用体魄丹可以服用");
        check(mfNpcHealthMaximum(npc) > MaximumBefore && npc.pillState.BaseHealthBonus == 3,
              "NPC体魄丹实际增加基础值与战斗气血上限");
    }
    {
        Game game = studyGame(); game.player.arts.speed = 500;
        Npc npc = pillNpc(3); npc.sect = "xinghe";
        npc.bag = {{"potion:5106", 1}, {"potion:5107", 2}, {"potion:5008", 1}};
        game.npcs.push_back(npc);
        const string before = saveToJson(game);
        artNpcLoadout(game.npcs[0]); artNpcLoadout(game.npcs[0]);
        checkStr(saveToJson(game), before, "NPC构筑预览不服药、不扣库存、不耗随机数");
        checkEq(npcConsumeUsefulPills(game.npcs[0]), 0, "日常行动保留战斗丹");
        EnemySpec enemy; enemy.id = npc.id; enemy.name = npc.name; enemy.stage = npc.stage;
        enemy.sect = npc.sect; enemy.style = npc.dao;
        battleStart(game, enemy);
        checkEq(game.npcs[0].bag["potion:5106"], 1, "水修士战前保留不适用火丹");
        checkEq(game.npcs[0].bag["potion:5107"], 1, "同类准备不叠加，战前仅用一枚水丹");
        checkEq(game.npcs[0].bag["potion:5008"], 0, "战前实际使用通用护盾丹");
        check(game.player.arts.battle && game.player.arts.battle->enemyCards.buff > 0 &&
              game.player.arts.battle->enemyShield > 0, "战斗丹收益进入真实敌方伤害和护盾");
        check(game.npcs[0].pillState.combatPrep.empty(), "开局准备被消费，不能免费留到下一场");
        Game restored; string error;
        check(loadDetachedFromJson(saveToJson(game), restored, &error), "含NPC服丹的在战存档可读取：" + error);
        if (!restored.npcs.empty()) {
            checkEq(restored.npcs[0].pillState.used["5107"], 1, "存档保留NPC耐药次数");
            checkEq(restored.npcs[0].bag["potion:5107"], 1, "读档不再次消费NPC战斗丹");
            checkNear(restored.player.arts.battle->enemyCards.buff, game.player.arts.battle->enemyCards.buff,
                      "在战读档保留已结算丹药伤害加成");
        }
    }
    {
        Game game = studyGame(); game.rng = 314159;
        game.player.arts.speed = 0; game.player.arts.hpBonus = 200;
        game.player.arts.nodes.clear(); game.player.arts.gear.clear(); game.player.arts.equipped.clear();
        Npc npc = pillNpc(0); npc.dao = "火";
        npc.bag = {{"potion:5008", 2}};
        game.npcs.push_back(npc);
        EnemySpec enemy; enemy.id = npc.id; enemy.name = npc.name;
        enemy.stage = npc.stage; enemy.style = npc.dao; enemy.speed = 1000;
        battleStart(game, enemy);
        check(game.player.arts.battle && !game.player.arts.battle->playerFirst,
              "快NPC真实开局抢先行动");
        if (game.player.arts.battle) {
            const auto& battle = *game.player.arts.battle;
            checkEq(battle.enemyCards.turns, 1, "首轮抢先连招只执行一次");
            checkEq(battle.enemyShield, jsRound(battle.enemyMax * 0.2),
                    "抢先出牌保留聚甲丹护盾供玩家首轮攻击消耗");
            checkEq(game.npcs[0].bag["potion:5008"], 1, "快NPC开局仅消费一枚聚甲丹");
            checkEq(game.npcs[0].pillState.used["5008"], 1, "快NPC开局耐药次数仅计一次");
            const string beforePreview = saveToJson(game);
            battleEnemyPreview(game); battleEnemyPreview(game);
            checkStr(saveToJson(game), beforePreview, "快NPC意图预览不改护盾、药量或随机数");
            Game restored; string error;
            check(loadDetachedFromJson(beforePreview, restored, &error), "快NPC首轮护盾可读档：" + error);
            if (restored.player.arts.battle) {
                checkEq(restored.player.arts.battle->enemyShield, battle.enemyShield,
                        "读档保留抢先行动后的丹药护盾");
                // Isolate turn-start expiry from any new shield-granting cards.
                for (int turn = 2; turn <= 3; ++turn) {
                    restored.player.arts.battle->enemyCards.planned.clear();
                    const auto preview = battleEnemyPreview(restored);
                    checkEq(preview.shield, 0, "后续空连招预览清除一次性开局护盾");
                    const auto result = battleTurn(restored, "end");
                    check(result.ok && restored.player.arts.battle.has_value(), "后续真实回合正常推进");
                    if (!restored.player.arts.battle) break;
                    checkEq(restored.player.arts.battle->enemyCards.turns, turn, "敌方每次仅推进一轮");
                    checkEq(restored.player.arts.battle->enemyShield, preview.shield,
                            "后续实战重置与预览一致，不重新发放聚甲丹护盾");
                }
                checkEq(restored.npcs[0].bag["potion:5008"], 1, "在战读档和重复回合不额外服丹");
            }
        }
    }
    {
        Game game = studyGame(); Npc npc = pillNpc(3);
        npc.age = npc.lifespan - 1; npc.bag["potion:5119"] = 1;
        game.npcs.push_back(npc);
        worldYear(game);
        const auto found = std::find_if(game.npcs.begin(), game.npcs.end(), [&](const Npc& value) { return value.id == npc.id; });
        check(found != game.npcs.end() && found->alive && found->lifeBonus == 10,
              "年度行动先服行囊寿元丹，再判定寿终");
    }
    {
        Game healthy = studyGame(); Npc npc = pillNpc(3);
        npc.personality = "潜心问道"; npc.stones = 10000;
        healthy.npcs.push_back(npc);
        Game poisoned = healthy;
        poisoned.npcs[0].pillState.poison = 81;
        worldYear(healthy); worldYear(poisoned);
        check(poisoned.npcs[0].xp < healthy.npcs[0].xp,
              "NPC高丹毒实际降低年度修炼收益");
        checkNear(artPoisonCultivationMultiplier(80), 0.625, "玩家和NPC共享丹毒效率曲线");
        checkNear(artPoisonCultivationMultiplier(std::numeric_limits<int>::max()), 0.5,
                  "极端丹毒安全封顶至五折");
    }
    {
        Game game = studyGame(); Npc npc = pillNpc();
        npc.pillsUsed = {"5208"}; npc.apt = 56;
        game.npcs.push_back(npc);
        auto oldSave = json::Value::parse(saveToJson(game));
        oldSave.asObject()["npcs"].asArray()[0].asObject().erase("pillState");
        Game restored; string error;
        check(loadDetachedFromJson(oldSave.dump(), restored, &error), "兼容没有pillState的旧NPC存档：" + error);
        if (!restored.npcs.empty()) {
            checkEq(restored.npcs[0].apt, 56, "旧档不重复发放已经结算的资质收益");
            checkEq(restored.npcs[0].pillState.used["5208"], 1, "旧服药ID迁移为一次耐药记录");
            checkEq(restored.npcs[0].pillState.aptPills, 1, "旧资质丹计入本世提升额度");
        }
        auto corrupt = json::Value::parse(saveToJson(game));
        corrupt.asObject()["npcs"].asArray()[0].asObject()["pillState"].set("poison", json::Value(101.0));
        const string previous = saveToJson(restored);
        check(!loadDetachedFromJson(corrupt.dump(), restored, &error), "越界NPC丹毒存档被拒绝");
        checkStr(saveToJson(restored), previous, "拒绝损坏存档时不部分覆盖当前状态");
    }
}
} // namespace nztest
