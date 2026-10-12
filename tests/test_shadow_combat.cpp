#include "harness.h"
#include "core/artifact.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/json.h"
#include "core/save.h"
#include "core/talents.h"
#include <algorithm>

using namespace nz;
namespace nztest {
namespace {
Player shadowFixture(const string& name, int stage = 6) {
    Player p;
    p.name = name; p.stage = p.highestStage = stage;
    p.lifespan = realmOf(stage).life;
    p.arts.main = "tuna"; p.arts.manuals["tuna"] = 3;
    p.arts.spells = {"gold", "wood", "skim", "fire"};
    p.arts.knownSpells = p.arts.spells;
    return p;
}
Game shadowFight(const Player& p, const Player& q, uint32_t seed = 71023) {
    Game g; battleStartShadow(g, p, q, seed); return g;
}
void shadowHand(Battle& b, const string& spell) {
    auto& e = b.enemyCards;
    e.hand.clear(); e.draw.clear(); e.discard.clear(); e.exhausted.clear(); e.planned.clear();
    for (const auto& card : e.deck) {
        if (card.spell == spell && e.hand.empty()) e.hand.push_back(card.uid);
        else e.draw.push_back(card.uid);
    }
    e.planned = e.hand; e.mana = 12;
}
bool shadowRoundTrip(const Game& game, Game& loaded) {
    string error;
    const bool ok = loadDetachedFromJson(saveToJson(game), loaded, &error);
    if (!ok) std::printf("Shadow roundtrip error: %s\n", error.c_str());
    return ok;
}
}

void runShadowCombatTests() {
    std::printf("\n-- 天机影录战斗隔离与对等 --\n");
    Player p = shadowFixture("挑战者"), q = shadowFixture("影录修士");
    p.arts.speed = 200;
    q.traits = {"dragonBlood", "water", "daoBody"};
    q.body = 7; q.arts.hpBonus = 14; q.arts.mind = 9; q.arts.speed = 17;
    q.sect = "jinhong";
    q.arts.main = "bull"; q.arts.manuals["bull"] = 4; q.arts.aux = {"tuna"};
    q.arts.nodes = {"2-1", "4-2", "11-3"};
    Gear weapon;
    weapon.id = "shadow-sword"; weapon.name = "影录灵剑";
    weapon.slot = "weapon"; weapon.shape = "sword"; weapon.element = "火";
    weapon.polarity = "天阳"; weapon.grade = 3; weapon.bonus = .17;
    weapon.crafted = true; weapon.inscribed = true;
    q.arts.gear.push_back(weapon); q.arts.equipped["weapon"] = weapon.id;
    const auto reference = battleReferenceState(q);
    Game g = shadowFight(p, q);
    Battle& b = *g.player.arts.battle;
    check(b.shadowPlayer && b.enemyCards.enabled && b.kind == "tianji", "影录使用独立真人战斗入口");
    checkEq(b.enemyMax, reference.max, "真人影录气血与玩家完全同公式，无NPC1.45倍率");
    checkEq(b.enemyHp, reference.hp, "影录初始气血保留快照状态");
    checkEq(b.enemySpeed, battleSpeed(q), "永久遁速与装备悟道正确进入影录");
    check(b.shadowPlayer->traits == q.traits && b.shadowPlayer->arts.manuals == q.arts.manuals &&
          b.shadowPlayer->arts.aux == q.arts.aux && b.shadowPlayer->arts.nodes == q.arts.nodes &&
          b.shadowPlayer->arts.equipped == q.arts.equipped, "天赋、主辅修及真实层数、节点、装备完整保留");
    check(std::any_of(b.enemyCards.deck.begin(), b.enemyCards.deck.end(), [](const Card& c) { return c.spell == "wood"; }),
          "金虹宗门不会过滤影录实际携带的木系神通");
    check(g.npcs.empty() && g.clock.days == 0 && !g.job && !g.pending && !g.dungeons.run,
          "切磋无NPC回查、日历或原世界工作状态");
    const auto originalQ = q;
    q.arts.manuals["bull"] = 1; q.arts.gear[0].bonus = 0;
    checkEq(b.shadowPlayer->arts.manuals.at("bull"), 4, "发布后原角色变更不影响影录");
    checkNear(b.shadowPlayer->arts.gear[0].bonus, .17, "影录装备为深拷贝而非源角色引用");
    q = originalQ;
    Game loaded;
    check(shadowRoundTrip(g, loaded), "完整真人影录与战斗计划能够存读档");
    if (loaded.player.arts.battle) {
        check(loaded.player.arts.battle->shadowPlayer->traits == q.traits,
              "重载保留影录天赋与不可变构筑");
        check(saveToJson(loaded) == saveToJson(g), "影录存读档无损且没有递归战斗快照");
    }
    checkStr(saveToJson(shadowFight(p, q)), saveToJson(shadowFight(p, q)), "同种子同快照确定性复现");

    // Run an artifact through the enemy scheduler and compare to the player spell pipeline.
    Game artifacts = shadowFight(p, q);
    Battle& ab = *artifacts.player.arts.battle;
    ab.hand.clear(); ab.shield = 0; ab.enemyShield = 0;
    ab.enemyCards.planned = {"artifact:" + weapon.id}; ab.enemyCards.mana = 12;
    Battle mirror = battleReferenceState(q);
    mirror.shadowPlayer = std::make_shared<const Player>(p);
    mirror.enemyCards.enabled = true;
    mirror.enemyHp = ab.hp; mirror.enemyMax = ab.max; mirror.enemyShield = 0;
    const Preview expected = battlePreview(q, mirror, "artifact:" + weapon.id);
    const EnemyPreview before = battleEnemyPreview(artifacts);
    checkEq(before.damage, expected.damage, "AI法宝主动伤害与玩家法宝结算相同");
    const int hp = ab.hp;
    check(battleTurn(artifacts, "end").ok, "影录执行法宝主动");
    checkEq(hp - ab.hp, expected.damage, "影录法宝主动预览与实际伤害相符");
    check(ab.enemyCards.cooldowns.count("artifact:" + weapon.id), "影录法宝有独立冷却");
    check(shadowRoundTrip(artifacts, loaded), "法宝主动冷却可以持久化恢复");

    // End-turn traits and defensive equipment apply on both sides.
    Player dragon = shadowFixture("龙血"); dragon.traits = {"dragonBlood"};
    Game healing = shadowFight(p, dragon);
    Battle& hb = *healing.player.arts.battle;
    hb.enemyHp = hb.enemyMax / 2; hb.enemyCards.planned.clear(); hb.hand.clear();
    const int wounded = hb.enemyHp;
    const EnemyPreview healPreview = battleEnemyPreview(healing);
    battleTurn(healing, "end");
    checkEq(hb.enemyHp - wounded, jsRound(hb.enemyMax * .03), "影录龙血每回合回复3%气血");
    checkEq(healPreview.heal, hb.enemyHp - wounded, "龙血回复进入AI预览");
    Player armored = shadowFixture("护身"); armored.arts.nodes = {"4-2"};
    Gear armor; armor.id = "shadow-armor"; armor.slot = "armor"; armor.element = "土";
    armor.name = "厚甲"; armor.shape = "armor"; armor.grade = 3; armor.bonus = .2;
    armored.arts.gear = {armor}; armored.arts.equipped["armor"] = armor.id;
    Game guarded = shadowFight(p, armored), unguarded = shadowFight(p, shadowFixture("普通"));
    auto& gb = *guarded.player.arts.battle; auto& ub = *unguarded.player.arts.battle;
    gb.enemyShield = ub.enemyShield = 0;
    const Preview guardedHit = battlePreview(p, gb, "gold"), plainHit = battlePreview(p, ub, "gold");
    check(guardedHit.damage < plainHit.damage, "敌方真实悟道减伤参与玩家攻击预览与结算");
    checkEq(gb.enemyMax, battleReferenceState(armored).max, "敌方防具气血效果与玩家同公式");

    // 24-action draw loop, including replaying a UID after a shuffle.
    Player draw = shadowFixture("运筹"); draw.arts.nodes = {"5-4"};
    Game loop = shadowFight(p, draw);
    auto& lb = *loop.player.arts.battle; auto& le = lb.enemyCards;
    le.deck.clear(); le.hand.clear(); le.draw.clear(); le.discard.clear(); le.exhausted.clear(); le.planned.clear();
    for (int i = 0; i < 10; ++i) {
        const string uid = "loop-" + std::to_string(i);
        le.deck.push_back({uid, "skim"});
        (i < 5 ? le.hand : le.draw).push_back(uid);
    }
    battleTurn(loop, "end");
    checkEq(le.planned.size(), 24, "真人影录不受NPC宗门8次上限，完整使用24次行动");
    check(shadowRoundTrip(loop, loaded), "24步计划及洗牌后重复UID可无损持久化");
    if (loaded.player.arts.battle) {
        check(loaded.player.arts.battle->enemyCards.planned == le.planned, "重载完整保留超过8步计划");
        battleTurn(loop, "end"); battleTurn(loaded, "end");
        check(saveToJson(loaded) == saveToJson(loop), "24步计划断线续战与连续执行完全一致");
    }

    Game tied = shadowFight(p, p);
    auto& tb = *tied.player.arts.battle;
    tb.turn = 23; tb.enemyCards.planned.clear(); tb.hand.clear();
    const int stones = tied.player.stones, wins = tied.player.arts.progress.wins;
    battleTurn(tied, "end");
    checkStr(battleShadowOutcome(tied), "draw", "24回合存活明确判定平局");
    check(!battleActive(tied) && !tied.player.arts.battleActive(), "终局保留战报但不能继续出牌");
    check(!battleTurn(tied, "end").ok, "结束的切磋拒绝重复结算");
    checkEq(tied.player.stones, stones, "影录切磋不会发放PVE灵石");
    checkEq(tied.player.arts.progress.wins, wins, "影录切磋不会变更PVE胜场");
    check(shadowRoundTrip(tied, loaded) && battleShadowOutcome(loaded) == "draw", "平局持久化后不会复活战斗");
    Game lost = shadowFight(p, p);
    auto& dead = *lost.player.arts.battle;
    dead.hp = -9;
    battleFinish(lost, false);
    check(shadowRoundTrip(lost, loaded) && battleShadowOutcome(loaded) == "loss", "负气血终局能读回且不触发死亡惩罚");
    Game retreat = shadowFight(p, p);
    battleTurn(retreat, "escape");
    checkStr(battleShadowOutcome(retreat), "retreat", "认输明确终结，源世界不受影响");
    check(shadowRoundTrip(retreat, loaded), "认输战报持久化");

    // Flat poison cleansing must not round a two-point cleanse into a full stack.
    Player cleanser = shadowFixture("清浊"); cleanser.arts.nodes = {"10-5"};
    Game cleanse = shadowFight(p, cleanser);
    auto& cb = *cleanse.player.arts.battle;
    cb.poison = 3; cb.poisonTurns = 3; cb.poisonDamage = jsRound((BATTLE_POWER[p.stage] + 12) * .035 * 3);
    cb.enemyHp = cb.enemyMax / 2; cb.hand.clear();
    shadowHand(cb, "wood");
    battleTurn(cleanse, "end");
    checkEq(cb.poisonDamage, 21, "影录治疗清毒精确扣2点毒伤，不向下取整成整层");
    checkEq(cb.poison, 3, "清毒未清空时保留毒层用于毒爆，与玩家相同");
    check(shadowRoundTrip(cleanse, loaded) && loaded.player.arts.battle->poisonDamage == 21,
          "精确毒伤持久化后不恢复未清毒的伤害");
    const int afterHeal = cb.enemyHp;
    cb.enemyCards.planned.clear();
    battleTurn(cleanse, "end");
    checkEq(afterHeal - cb.enemyHp, 21, "下一回合实际扣除21点已清毒伤害");

    // A faster opponent gets the first phase, not an extra 25th round.
    Player fast = p; fast.name = "先手修士";
    Player slow = p; slow.arts.speed = 0;
    Game capped = shadowFight(slow, fast);
    auto& cap = *capped.player.arts.battle;
    check(!cap.playerFirst && cap.enemyCards.turns == 1, "高速影录先行动一次");
    cap.turn = 23; cap.enemyCards.turns = 24; cap.hp = cap.max;
    const int beforeCap = cap.hp;
    checkEq(battleEnemyPreview(capped).damage, 0, "24个对手回合后预览不会再显示第25次攻击");
    battleTurn(capped, "end");
    checkStr(battleShadowOutcome(capped), "draw", "敌方先手时双方各24回合后平局");
    checkEq(cap.hp, beforeCap, "回合封顶不赠送高速影录第25次行动");
    checkEq(cap.enemyCards.turns, 24, "高速影录行动次数上限为24");
    check(shadowRoundTrip(capped, loaded), "高速影录24回合终局可以持久化");
    string normalError;
    check(!loadFromJson(saveToJson(capped), loaded, &normalError), "普通角色导入拒绝天机战斗载荷");
    Game detachedSnapshot; detachedSnapshot.player = q; detachedSnapshot.clock.ageBase = q.age;
    check(shadowRoundTrip(detachedSnapshot, loaded) && saveToJson(detachedSnapshot) == saveToJson(loaded),
          "无战斗的冻结快照也按独立路径原样重载");

    Game mutual = shadowFight(p, p);
    auto& mb = *mutual.player.arts.battle;
    mb.hp = mb.enemyHp = 1; mb.burn = mb.enemyCards.burn = 1;
    mb.burnTurns = mb.enemyCards.burnTurns = 1;
    battleTurn(mutual, "end");
    checkStr(battleShadowOutcome(mutual), "draw", "双方同时被持续伤害击倒时判平局");
    check(shadowRoundTrip(mutual, loaded), "双方零气血终局可恢复");
    auto recursive = json::Value::parse(saveToJson(g));
    recursive.asObject()["player"].asObject()["arts"].asObject()["battle"]
        .asObject()["shadowPlayer"].asObject()["arts"].set("battle", json::Value(json::Object{}));
    check(!loadDetachedFromJson(recursive.dump(), loaded, &normalError), "递归影录战斗在反序列化前被拒绝");

    // Old PvE keeps its 1.45 balancing and normal clear-on-finish behavior.
    Game pve; pve.player = p; pve.clock.ageBase = p.age;
    EnemySpec npc; npc.name = "旧试炼"; npc.stage = p.stage; npc.style = "剑";
    battleStart(pve, npc);
    check(!pve.player.arts.battle->shadowPlayer, "普通PVE没有影录分支");
    battleFinish(pve, true);
    check(!pve.player.arts.battle, "普通PVE结算仍清除在途斗法");
}
}
#ifdef NZ_SHADOW_COMBAT_TEST_MAIN
int main() { nztest::runShadowCombatTests(); return nztest::failures() ? 1 : 0; }
#endif
