#include "harness.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/hand_rules.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"

using namespace nz;

namespace nztest {
namespace {

Game npcFight(const string& sect = "xinghe", const string& dao = "水", int stage = 6) {
    Game g = createGame(CreateOptions{});
    g.pending.reset(); g.npcs.clear();
    g.player.stage = g.player.highestStage = stage;
    g.player.lifespan = realmOf(stage).life + g.player.lifeBonus;
    g.player.body = 0;
    g.player.arts.main.clear(); g.player.arts.aux.clear();
    g.player.arts.manuals.clear(); g.player.arts.nodes.clear();
    g.player.arts.equipped.clear(); g.player.arts.speed = 200;
    g.player.arts.spells = {"sword", "wood", "skim"};
    ensureArts(g);
    g.player.health = mfPlayerHealthMaximum(g.player);
    Npc npc;
    npc.id = "card-opponent"; npc.name = "门派试炼";
    npc.sect = sect; npc.dao = dao; npc.stage = stage;
    npc.lifespan = realmOf(stage).life;
    g.npcs.push_back(npc);
    EnemySpec enemy;
    enemy.id = npc.id; enemy.name = npc.name; enemy.stage = stage;
    enemy.sect = sect; enemy.style = dao;
    battleStart(g, enemy);
    g.player.arts.battle->max *= 5;
    g.player.arts.battle->hp = g.player.arts.battle->max;
    return g;
}

int countSpell(const vector<Card>& deck, const string& id) {
    return static_cast<int>(std::count_if(deck.begin(), deck.end(), [&](const Card& c) { return c.spell == id; }));
}

void enemyHand(Battle& b, const vector<string>& spells) {
    EnemyCombat& e = b.enemyCards;
    e.hand.clear(); e.draw.clear(); e.discard.clear(); e.planned.clear(); e.exhausted.clear();
    for (const auto& id : spells) {
        const auto found = std::find_if(e.deck.begin(), e.deck.end(), [&](const Card& c) {
            return c.spell == id && std::find(e.hand.begin(), e.hand.end(), c.uid) == e.hand.end();
        });
        if (found != e.deck.end()) e.hand.push_back(found->uid);
    }
    for (const auto& card : e.deck)
        if (std::find(e.hand.begin(), e.hand.end(), card.uid) == e.hand.end()) e.draw.push_back(card.uid);
    e.planned = e.hand;
    e.mana = 12;
}

bool partitioned(const EnemyCombat& e) {
    map<string, int> all;
    for (const auto* pile : {&e.hand, &e.draw, &e.discard, &e.exhausted})
        for (const auto& uid : *pile) ++all[uid];
    if (all.size() != e.deck.size() || e.hand.size() > BattleCards::MaximumHandSize) return false;
    for (const auto& card : e.deck) if (all[card.uid] != 1) return false;
    return true;
}

} // namespace

void runNpcCardTests() {
    std::printf("\n-- 双方真实牌组、神通和遁速 --\n");
    {
        Game g = npcFight("xinghe", "水", 0);
        const Battle& b = *g.player.arts.battle;
        check(b.enemyCards.enabled && !b.enemyBeast, "人类对手建立真实独立牌组");
        checkEq(countSpell(b.deck, "strike"), 5, "玩家固定五张普通攻击");
        checkEq(countSpell(b.deck, "defend"), 5, "玩家固定五张普通防御");
        checkEq(countSpell(b.enemyCards.deck, "strike"), 5, "NPC同样固定五张普通攻击");
        checkEq(countSpell(b.enemyCards.deck, "defend"), 5, "NPC同样固定五张普通防御");
        checkEq(b.enemyCards.hand.size(), 5, "NPC起手真实抽五张");
        checkStr(b.intentKind, "cards", "NPC意图由真实手牌连招形成");
        check(!b.enemyCards.planned.empty() && partitioned(b.enemyCards), "计划来自合法手牌且四牌堆守恒");
        checkStr(b.enemyCards.main, "wave", "炼气星河弟子实际使用水道本轮推荐主修");
        bool legal = true;
        for (const auto& card : b.enemyCards.deck) {
            const auto* sp = battleSpell(card.spell);
            legal = legal && sp && sp->stage <= b.enemyStage;
        }
        check(legal, "NPC不会携带高于自身境界的神通");
        Game wanderer = npcFight("", "水", 0);
        const EnemyCombat& weak = wanderer.player.arts.battle->enemyCards;
        checkStr(weak.main, "tuna", "初期散修主修吐纳");
        check(weak.aux.empty() && weak.nodes.empty() && weak.deck.size() <= 14,
              "初期散修缺少辅修悟道且至多两种额外神通");
        check(b.enemyCards.deck.size() > weak.deck.size(), "宗门修士的构筑比同境散修完整");
    }
    {
        Game g = npcFight(); Battle& b = *g.player.arts.battle;
        enemyHand(b, {"waterSeal", "wavehit"});
        checkEq(b.enemyCards.planned.size(), 2, "水系连招测试使用两张真实手牌");
        const uint32_t rng = g.rng;
        const string before = saveToJson(g);
        const EnemyPreview preview = battleEnemyPreview(g);
        check(preview.spells.size() > 2 && preview.spells.size() <= static_cast<std::size_t>(mfBattleEnemyActionLimit(g, b)),
              "水势前缀真实补牌后继续合法新牌，完整意图不超过原行动上限");
        check(preview.spells.size() >= 2 && preview.spells[0] == "waterSeal" && preview.spells[1] == "wavehit",
              "动态续招仍先执行指定的水印与浪击前缀");
        check(preview.damage > 0 && preview.description.find(battleSpell("wavehit")->name) != string::npos,
              "NPC预览展示实际水势神通文案与伤害");
        check(saveToJson(g) == before && g.rng == rng, "NPC预览不改变随机数、资源或牌堆");
        const int hp = b.hp;
        check(battleTurn(g, "end").ok, "NPC连招在结束回合实际执行");
        checkEq(hp - b.hp, preview.damage, "NPC水系真实伤害等于权威预览");
        check(b.enemyCards.water <= 1 && partitioned(b.enemyCards), "浪击实际消耗水势且牌堆守恒");
        checkEq(b.enemyCards.hand.size(), 5, "NPC行动后弃牌并重抽下一轮五张");
        check(b.enemyCards.mana >= b.enemyCards.maxMana && b.enemyCards.mana <= 12,
              "NPC真实费用结算后按自身功法恢复灵力");
    }
    {
        Game g = npcFight("zhushan", "木"); Battle& b = *g.player.arts.battle;
        enemyHand(b, {"corrode"});
        battleTurn(g, "end");
        check(b.playerPoison > 0 && b.playerPoisonTurns == 1 && b.enemyCards.poison >= 3,
              "NPC腐蚀神通真的对玩家施毒");
        const int hp = b.hp;
        enemyHand(b, {}); battleTurn(g, "end");
        check(b.hp < hp && b.playerPoisonTurns == 1, "NPC毒伤随后真实结算并保留本场活跃标记");
        b.enemyHp = b.enemyMax / 2;
        enemyHand(b, {"poisonSiphon"});
        const EnemyPreview burst = battleEnemyPreview(g);
        check(burst.heal > 0, "NPC借毒治疗预览使用已积攒毒层");
        const int burstHp = b.hp, EnemyHp = b.enemyHp;
        battleTurn(g, "end");
        checkEq(burstHp - b.hp, burst.damage, "NPC借毒动作仅含正常跳毒，不附带直伤");
        checkEq(b.enemyHp - EnemyHp, burst.heal, "NPC借毒真实回血和预览一致");
        check(b.enemyCards.poison > 0, "NPC借毒实际保留毒层");
        checkEq(b.playerPoisonTurns, 1, "借毒后玩家仍保留本场毒持续伤害");
    }
    {
        Game g = npcFight("lihuo", "火"); Battle& b = *g.player.arts.battle;
        g.player.traits.clear();
        b.enemyCards.main = "tuna"; b.enemyCards.manuals = {{"tuna", 1}};
        b.enemyCards.aux.clear(); b.enemyCards.nodes.clear();
        enemyHand(b, {"fire", "fire"}); const int openingHp = b.hp;
        const string before = saveToJson(g); const auto rng = g.rng; const auto opening = battleEnemyPreview(g);
        check(saveToJson(g) == before && g.rng == rng, "叠燃NPC预览不修改状态或随机数");
        battleTurn(g, "end");
        checkEq(openingHp - b.hp, opening.damage, "无火功法NPC双火诀直接伤害等于预览");
        check(b.enemyCards.burn > 0 && b.enemyCards.burnTurns == 3, "离火NPC真实施加灼烧");
        checkEq(b.enemyCards.burn, 2 * jsRound(mfBattleDamageBasis(b.enemyStage) * 0.08),
                "NPC同样无需火功法即可天然叠加两次灼烧");
        enemyHand(b, {"fireburst"});
        const auto preview = battleEnemyPreview(g);
        check(preview.damage > 0, "火系NPC真实预览引燃爆发");
        const int burstHp = b.hp; const int burn = b.enemyCards.burn;
        Game bare = g; bare.player.arts.battle->enemyCards.burn = 0;
        bare.player.arts.battle->enemyCards.burnTurns = 0;
        checkEq(preview.damage - battleEnemyPreview(bare).damage, burn * 3,
                "NPC先跳一次灼烧再引爆剩余两回合，总预算只算三次");
        battleTurn(g, "end");
        checkEq(burstHp - b.hp, preview.damage, "NPC跳伤后引爆实际伤害与投影预览一致");
        checkEq(b.enemyCards.burn, 0, "NPC引焰爆消耗灼烧");
        checkEq(b.enemyCards.burnTurns, 0, "NPC引焰爆清除燃烧持续时间");
    }
    {
        Game g = npcFight("zhushan", "木"); Battle& b = *g.player.arts.battle;
        g.player.traits.clear();
        b.enemyCards.main = "tuna"; b.enemyCards.manuals = {{"tuna", 1}};
        b.enemyCards.aux.clear(); b.enemyCards.nodes.clear();
        enemyHand(b, {"corrode", "corrode"}); battleTurn(g, "end");
        enemyHand(b, {"corrode"}); battleTurn(g, "end");
        checkEq(b.enemyCards.poison, 9, "NPC无木功法也能跨回合施毒超过六层");
        checkEq(b.playerPoison, jsRound(mfBattleDamageBasisFromPower(b.enemyPower) * 0.045 * 9), "NPC九层毒同步为真实玩家毒伤");
        Game loaded; string error;
        const bool saved = loadFromJson(saveToJson(g), loaded, &error);
        check(saved, "九层NPC毒的真实战斗可以保存并读回");
        if (saved) {
            checkEq(loaded.player.arts.battle->enemyCards.poison, 9, "存档读回保留高于六层的NPC毒");
            checkEq(loaded.player.arts.battle->playerPoison, b.playerPoison, "存档读回保留实际玩家毒伤");
        }
        const int PoisonDamage = b.playerPoison;
        for (int Turn = 0; Turn < 5; ++Turn) {
            enemyHand(b, {}); const int hp = b.hp; const auto preview = battleEnemyPreview(g);
            check(battleTurn(g, "end").ok, "NPC持续毒伤通过真实结束回合结算");
            checkEq(hp - b.hp, preview.damage, "NPC持续毒伤的完整回合损失与预览一致");
            checkEq(hp - b.hp, PoisonDamage, "没有后续施毒或出牌时NPC九层毒仍真实跳伤");
            checkEq(b.playerPoisonTurns, 1, "NPC毒跨过旧三回合期限仍保留本场活跃标记");
        }
        checkEq(b.playerPoison, PoisonDamage, "NPC无木功法时本场毒伤持续且不额外增长");
        checkEq(b.enemyCards.poison, 9, "NPC无木功法时本场持续保留九层毒");
    }
    {
        Game g = npcFight("zhushan", "木"); Battle& b = *g.player.arts.battle;
        b.enemyHp = b.enemyMax / 2;
        enemyHand(b, {"wood"});
        const int hp = b.hp, enemyHp = b.enemyHp;
        const auto preview = battleEnemyPreview(g);
        check(preview.heal > 0 && preview.damage == 0, "NPC回春术预览为治疗而非攻击");
        battleTurn(g, "end");
        checkEq(b.hp, hp, "NPC治疗不额外偷附一次平击");
        checkEq(b.enemyHp - enemyHp, preview.heal, "NPC真实治疗与预览一致");
        checkEq(b.enemyCards.cooldowns["wood"], 2, "NPC真实施放治疗后进入两回合冷却");
        enemyHand(b, {"wood"}); const int health = b.enemyHp;
        check(battleEnemyPreview(g).spells.empty(), "冷却中的NPC神通不会进入实际意图");
        battleTurn(g, "end");
        check(b.enemyHp - health <= jsRound(b.enemyMax * 0.05), "强塞计划也不能绕过NPC冷却额外治疗");
        checkEq(b.enemyCards.cooldowns["wood"], 1, "NPC冷却按实际回合递减一次");
    }
    {
        Game g = npcFight("xinghe", "水"); Battle& b = *g.player.arts.battle;
        enemyHand(b, {"water"});
        const int hp = b.hp;
        const auto preview = battleEnemyPreview(g);
        check(preview.shield > 0 && preview.damage == 0, "水灵盾NPC意图明确显示护盾");
        battleTurn(g, "end");
        checkEq(b.hp, hp, "NPC护盾卡不会附加虚假攻击");
        checkEq(b.enemyShield, preview.shield, "NPC获得的盾与权威预览相同");
    }
    {
        Game g = npcFight("", "体", 0);
        battleTurn(g, "escape");
        g.player.arts.speed = 0;
        EnemySpec enemy; enemy.name = "迅捷妖兽"; enemy.beast = true; enemy.speed = 100;
        battleStart(g, enemy);
        const Battle& slow = *g.player.arts.battle;
        check(!slow.playerFirst && slow.hp < slow.max, "敌方遁速更快时实际抢先攻击");
        const int wounded = slow.hp;
        battleTurn(g, "escape"); g.player.health = mfPlayerHealthMaximum(g.player); g.player.arts.speed = 200;
        battleStart(g, enemy);
        check(g.player.arts.battle->playerFirst && g.player.arts.battle->hp == g.player.arts.battle->max,
              "玩家遁速更快时先手且开局未受攻击");
        check(wounded > 0, "同境界先手打击不会虚构开局死亡");
        Battle b = *g.player.arts.battle; b.lethal = true;
        const double fast = battleEscapeChance(g.player, b);
        Player slower = g.player; slower.arts.speed = 0;
        check(fast > battleEscapeChance(slower, b), "相同敌人下实际遁速提升撤离成功率");
        check(fast >= 0.1 && fast <= 0.95, "生死斗撤离概率保留上下限");
        b.lethal = false;
        checkNear(battleEscapeChance(slower, b), 1, "切磋撤离保持必定成功");
    }
    {
        Game g = npcFight("", "剑", 0);
        Battle b = *g.player.arts.battle;
        Player low = g.player, high = g.player;
        low.sword = 0; high.sword = 500;
        b.enemySword = 500; b.enemySwordCult = true;
        checkEq(battlePreview(low, b, "sword").damage, battlePreview(high, b, "sword").damage,
                "永久剑意不再构成独立伤害属性");
        checkNear(battleReduction(low, b), battleReduction(high, b), "永久剑意境差不再提供额外减伤");
        const int before = battlePreview(low, b, "sword").damage;
        b.swordQi = 4;
        check(battlePreview(low, b, "sword").damage > before, "本场由剑牌积累的剑气仍然强化连招");
    }
    {
        Game slow = npcFight("", "体", 0);
        battleTurn(slow, "escape"); slow.player.arts.speed = 0;
        EnemySpec enemy; enemy.name = "追击试炼"; enemy.beast = true;
        enemy.lethal = true; enemy.speed = 10000;
        battleStart(slow, enemy);
        Game fast = slow; fast.player.arts.speed = 200;
        fast.player.arts.battle->enemySpeed = 20;
        uint32_t selected = 1;
        for (; selected < 10000; ++selected) {
            Game probe; probe.rng = selected;
            const double roll = randomOf(probe);
            if (roll > 0.2 && roll < 0.8) break;
        }
        slow.rng = fast.rng = selected;
        const int day = slow.clock.days;
        battleTurn(slow, "escape"); battleTurn(fast, "escape");
        check(slow.player.arts.battle && slow.player.arts.battle->turn == 1,
              "低遁速撤离失败实际消耗一回合并遭追击");
        check(!fast.player.arts.battle && !fast.ended, "同一次随机判定下高遁速成功撤离");
        checkEq(slow.clock.days, day, "追击不会偷偷推进暂停的世界日历");
    }
    {
        Game g = npcFight("", "水", 0); Battle& b = *g.player.arts.battle;
        enemyHand(b, {"waterSeal"}); b.enemyCards.mana = 0;
        const int hp = b.hp;
        check(battleEnemyPreview(g).spells.empty(), "NPC灵力不足不会预演付不起的神通");
        battleTurn(g, "end");
        checkEq(b.hp, hp, "NPC实际执行也不能绕过灵力费用");
        checkEq(b.enemyCards.water, 0, "付不起的水牌不会生成水势");
    }
    {
        for (const auto& sect : {string("jinhong"), string("xinghe"), string("zhushan"),
                                 string("lihuo"), string("huachen")}) {
            Game g = createGame(CreateOptions{});
            g.pending.reset(); g.npcs.clear();
            Npc npc; npc.id = "early-foe"; npc.name = "炼气弟子"; npc.sect = sect;
            g.npcs.push_back(npc);
            EnemySpec enemy; enemy.id = npc.id; enemy.name = npc.name; enemy.sect = sect;
            battleStart(g, enemy);
            check(!g.ended && g.player.arts.battle && g.player.arts.battle->hp > 0,
                  "正常新档对同境" + sect + "弟子不会因先手直接死亡");
        }
    }
    {
        Game g = npcFight();
        Battle& b = *g.player.arts.battle;
        enemyHand(b, {"waterSeal", "wavehit"});
        Game restored;
        string error;
        check(loadFromJson(saveToJson(g), restored, &error), "NPC真实牌组在途存档可读回：" + error);
        if (restored.player.arts.battle) {
            check(restored.player.arts.battle->enemyCards.planned == b.enemyCards.planned,
                  "读档保留NPC已展示的连招UID");
            check(saveToJson(g) == saveToJson(restored), "NPC牌堆、资源、先手与构筑完整往返保存");
            battleTurn(g, "end"); battleTurn(restored, "end");
            check(saveToJson(g) == saveToJson(restored), "读档后的NPC出牌与随机抽牌结果完全一致");
        }
    }
    {
        Game g = npcFight("", "体", 0);
        battleTurn(g, "escape");
        Job travel; travel.actionType = "travel"; travel.destination = "yunxi";
        travel.total = travel.remaining = 9; g.job = travel;
        EnemySpec enemy; enemy.name = "拦路散修"; enemy.kind = "ambush"; enemy.lethal = true;
        enemy.speed = 20; battleStart(g, enemy);
        g.player.arts.battle->lethal = false;
        battleTurn(g, "escape");
        check(g.job && g.job->remaining == 9 && g.player.location != "yunxi",
              "途中遇敌撤离后保留未完成行程且不会提前抵达");
    }
    {
        Game g = npcFight();
        g.player.arts.battle->lethal = true;
        g.player.arts.battle->hp = 1;
        battleFinish(g, true);
        check(!g.ended && g.player.health == 1, "生死斗残血获胜的气血换算不会四舍五入成零");
    }
    {
        Game g = npcFight("zhushan", "木");
        battleTurn(g, "escape");
        g.npcs.front().health = 25;
        EnemySpec enemy; enemy.id = g.npcs.front().id; enemy.name = g.npcs.front().name;
        enemy.stage = g.npcs.front().stage;
        battleStart(g, enemy);
        const Battle& b = *g.player.arts.battle;
        checkEq(b.enemyHp, jsRound(b.enemyMax * 0.25), "真实NPC伤势影响当前气血而非永久压低气血上限");
    }
    {
        Game g = npcFight("", "体", 0);
        battleTurn(g, "escape");
        Job bounty; bounty.actionType = "wild"; bounty.name = "清剿山匪";
        bounty.kind = "bandit"; bounty.sect = "剑";
        societyWildStart(g, bounty);
        check(g.player.arts.battle && !g.player.arts.battle->enemyBeast && g.player.arts.battle->enemyCards.enabled,
              "清剿山匪使用散修真实牌组和人物身份");
        battleTurn(g, "escape"); bounty.kind = "beast"; bounty.name = "驱赶妖兽";
        societyWildStart(g, bounty);
        check(g.player.arts.battle && g.player.arts.battle->enemyBeast && !g.player.arts.battle->enemyCards.enabled,
              "驱赶妖兽才使用妖兽身份及妖兽行动");
    }
    {
        Game base = createGame(CreateOptions{});
        base.pending.reset(); base.npcs.clear();
        base.player.stage = base.player.highestStage = 3;
        base.player.lifespan = realmOf(3).life + base.player.lifeBonus;
        base.player.arts.nodes = {"6-5"};
        base.player.arts.combatPrep["shield"] = 0.30;
        Npc wounded;
        wounded.id = "opening-counter-target"; wounded.name = "负伤劫修";
        wounded.dao = "金"; wounded.health = 1;
        base.npcs.push_back(wounded);
        for (const auto& reward : {string("wild"), string("adventure"), string("hunt")}) {
            bool counterWin = false;
            for (uint32_t seed = 1; seed <= 64 && !counterWin; ++seed) {
                Game g = base; g.rng = seed;
                const int stones = g.player.stones;
                EnemySpec enemy;
                enemy.name = wounded.name; enemy.id = wounded.id;
                enemy.speed = 10000; enemy.kind = reward == "hunt" ? "hunt" : "wild";
                if (reward == "wild") { enemy.wildTask = "bandit"; enemy.wildFee = 83; }
                else if (reward == "adventure") {
                    AdvContext context; context.kind = "bandit"; context.fee = 83;
                    enemy.adventure = context; enemy.adventureChoice = "fight";
                } else enemy.huntFee = 83;
                battleStart(g, enemy);
                if (g.player.arts.battle || g.player.arts.progress.wins == 0) continue;
                counterWin = true;
                check(!g.ended && g.player.health > 0, reward + "首轮护盾反震获胜正常收口");
                checkEq(g.player.stones, stones + 83, reward + "首轮反杀也领取已登记的真实赏金");
                if (reward == "wild") checkEq(g.bonds.wildWins, 1, "首轮反殺计入悬赏胜场");
                else if (reward == "adventure") checkEq(g.adventure.wins, 1, "首轮反杀计入游历胜场");
                else checkEq(g.economy.huntCounts, 1, "首轮反杀计入城市猎妖交付");
                battleFinish(g, true);
                checkEq(g.player.stones, stones + 83, reward + "开战即结束后再次收口不会重复发奖");
            }
            check(counterWin, reward + "回归实际执行敌方先手与反震致死，未绕过出牌结算");
        }
    }
}

} // namespace nztest
