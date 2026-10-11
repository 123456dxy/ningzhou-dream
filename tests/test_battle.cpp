// 卡牌斗法回归：固定普通招式、确定性抽牌、预览兑现、回合结算与剑道。
#include "harness.h"

#include "core/arts.h"
#include "core/battle.h"
#include "core/sim.h"
#include "core/sword.h"

using namespace nz;

namespace nztest {

namespace {

Game makeCardGame(uint32_t seed) {
    Game g;
    g.seed = seed;
    g.rng = seed;
    g.year = 0;

    Player& p = g.player;
    p.name = "测试";
    p.gender = "女";
    p.age = 20;
    p.lifespan = 100;
    p.stage = 0;
    p.apt = 60;
    p.luck = 45;
    p.insight = 20;
    p.sword = 20;
    p.alchemy = 10;
    p.body = 2;
    p.health = 100;
    p.stones = 500;
    p.herbs = 20;
    p.location = "wuling";
    p.traits = {"sword"};
    p.build.budget = 10;

    ensureArts(g);
    // 与 JS 测试脚本一致：携带灵击/御剑诀/水灵盾/回春术
    p.arts.spells = {"strike", "sword", "water", "wood"};
    return g;
}

EnemySpec testEnemy() {
    EnemySpec e;
    e.name = "试招对手";
    e.stage = 0;
    e.style = "剑";
    e.beast = true;
    return e;
}

const Card* cardOf(const Battle& b, const string& uid) {
    for (const auto& c : b.deck)
        if (c.uid == uid) return &c;
    return nullptr;
}

} // namespace

void runCardTests() {
    std::printf("\n-- 卡牌斗法：固定普通招式与额外神通 --\n");
    Game g = makeCardGame(20261004);
    battleStart(g, testEnemy());
    const Battle start = *g.player.arts.battle;
    checkEq(start.hp, 161, "开局气血含体魄加成");
    checkEq(start.max, 161, "气血上限含体魄加成");
    checkEq(start.enemyMax, 208, "对手气血含斗法修正");
    checkEq(start.mana, 3, "炼气开局灵力3");
    checkEq(start.maxMana, 3, "炼气每回合自然回复3");
    checkEq(start.deck.size(), 16, "三个额外神通加十张固定普通招式");
    checkEq(start.hand.size(), 5, "起手抽5张");
    checkEq(start.draw.size(), 11, "起手后其余十一张在抽牌堆");
    map<string, int> counts;
    for (const auto& c : start.deck) counts[c.spell]++;
    checkEq(counts["strike"], 5, "普通攻击固定5张，旧携带项不重复加入");
    checkEq(counts["defend"], 5, "普通防御固定5张");
    checkEq(counts["sword"], 2, "额外御剑诀2张");
    checkEq(counts["water"], 2, "额外水灵盾2张");
    checkEq(counts["wood"], 2, "额外回春术2张");
    Game same = makeCardGame(20261004);
    battleStart(same, testEnemy());
    check(start.hand == same.player.arts.battle->hand &&
              start.draw == same.player.arts.battle->draw &&
              start.intentRaw == same.player.arts.battle->intentRaw,
          "相同种子产生相同起手、牌堆与已展示意图");
    checkStr(start.intentKind, "attack", "开局意图为普通攻击");

    struct Case { const char* id; int cost, dmg, shield, heal, energy, qi; };
    const Case cases[] = {
        {"water", 1, 0, 29, 0, -1, 0}, {"wood", 2, 0, 0, 0, -2, 0},
        {"strike", 1, 12, 0, 0, -1, 0}, {"sword", 2, 22, 0, 0, -2, 1},
        {"defend", 1, 0, 19, 0, -1, 0},
    };
    for (const auto& c : cases) {
        const Preview pv = battlePreview(g.player, start, c.id);
        check(pv.valid && pv.cost == c.cost && pv.damage == c.dmg &&
                  pv.shield == c.shield && pv.heal == c.heal &&
                  pv.energy == c.energy && pv.qi == c.qi,
              string(c.id) + "预览兑现基础伤害、盾、费用与剑气");
    }
    check(g.player.arts.battle->hand == start.hand &&
              g.player.arts.battle->mana == start.mana,
          "所有预览不改变真实手牌与灵力");

    auto arrange = [](Battle& b, const vector<string>& spellIds) {
        b.hand.clear(); b.draw.clear(); b.discard.clear();
        for (const auto& id : spellIds) {
            for (const auto& c : b.deck) {
                if (c.spell == id && std::find(b.hand.begin(), b.hand.end(), c.uid) == b.hand.end()) {
                    b.hand.push_back(c.uid); break;
                }
            }
        }
        for (const auto& c : b.deck)
            if (std::find(b.hand.begin(), b.hand.end(), c.uid) == b.hand.end()) b.draw.push_back(c.uid);
    };
    {
        Game t = makeCardGame(20240611); battleStart(t, testEnemy());
        Battle& b = *t.player.arts.battle; arrange(b, {"strike", "sword"});
        const int hp = b.hp, raw = b.intentRaw;
        const Preview preview = battlePreview(t.player, b, "strike");
        const int enemyHp = b.enemyHp;
        check(battleTurn(t, "strike").ok, "可施放手牌普通攻击");
        checkEq(b.enemyHp, enemyHp - preview.damage, "真实伤害与预览一致");
        checkEq(b.hp, hp, "出牌不触发敌方反击");
        checkEq(b.intentRaw, raw, "出牌不改变已展示意图");
        checkEq(b.turn, 0, "出牌不推进回合");
        check(battleTurn(t, "sword").ok, "可施放剩余御剑诀");
        checkEq(b.mana, 0, "连续两次出牌正确消耗灵力");
        checkEq(b.swordQi, 1, "御剑诀增加剑气");
        checkEq(b.played, 2, "本回合出牌数逐次累计");
        checkEq(b.hand.size(), 0, "施放卡离开手牌");
        checkEq(b.discard.size(), 2, "施放卡进入弃牌堆");
        check(battleTurn(t, "end").ok, "可结束回合");
        checkEq(b.turn, 1, "结束回合才推进回合");
        check(b.hp < hp, "结束回合才受到敌方伤害");
        checkEq(b.shield, 0, "结束回合后普通护盾清零");
        checkEq(b.hand.size(), 5, "新回合重新抽5张");
        checkEq(b.mana, 3, "新回合回复基础灵力");
    }
    {
        Game t = makeCardGame(1); battleStart(t, testEnemy());
        Battle& b = *t.player.arts.battle; arrange(b, {"sword"}); b.mana = 0;
        const TurnResult r = battleTurn(t, "sword");
        check(!r.ok, "灵力不足拒绝出牌");
        checkStr(r.error, "灵力不足，请选择其他手牌或结束回合。", "给出灵力不足提示");
        checkEq(b.hand.size(), 1, "被拒绝的卡仍在手中");
        check(!battleTurn(t, "rain").ok, "不在手中的神通拒绝施放");
    }
    {
        Game t = makeCardGame(1);
        checkStr(battleTurn(t, "strike").error, "当前没有斗法。", "无斗法给出明确提示");
        checkEq(battleEnergy(t.player), 3, "默认自然回灵3");
        t.player.arts.nodes = {"8-1"};
        checkEq(battleEnergy(t.player), 4, "气道一层保留额外回灵");
        checkEq(battleCost(t.player, *battleSpell("strike")), 1, "普通攻击固定费用1");
        checkEq(battleCost(t.player, *battleSpell("defend")), 1, "普通防御固定费用1");
        checkEq(battleCost(t.player, *battleSpell("sword")), 2, "原费用3折半向上取整2");
        checkEq(battleCost(t.player, *battleSpell("rain")), 3, "原费用6折半费用3");
    }
    {
        Game t = makeCardGame(1); battleStart(t, testEnemy());
        Battle& b = *t.player.arts.battle; arrange(b, {"sword", "sword"}); b.mana = 8;
        const Preview first = battlePreview(t.player, b, "sword");
        check(battleTurn(t, "sword").ok, "第一张剑牌可施放");
        check(battlePreview(t.player, b, "sword").damage > first.damage, "积攒剑气强化后续剑招");
    }
    {
        Game t = makeCardGame(1); t.player.stage = 12;
        EnemySpec tank = testEnemy(); tank.stage = 12; tank.style = "体";
        battleStart(t, tank);
        int turns = 0;
        while (t.player.arts.battle && turns++ < 40) {
            t.player.arts.battle->hp = t.player.arts.battle->max;
            battleTurn(t, "end");
        }
        check(!t.player.arts.battle, "久战最多24回合必然结束");
        checkEq(turns, 24, "健康玩家也不能超出24回合上限");
    }
}
void runSwordTests() {
    std::printf("\n-- 剑道七重境界（对照 Demo06 sword.mjs）--\n");

    checkEq(static_cast<long long>(swordRealms().size()), 7, "七重剑道境界");
    checkStr(swordRealmName(0), "初识剑路", "0 剑意为初识剑路");
    checkStr(swordRealmName(19), "初识剑路", "19 剑意仍为初识剑路");
    checkStr(swordRealmName(20), "剑随心动", "20 剑意进入剑随心动");
    checkStr(swordRealmName(60), "剑气凝锋", "60 剑意为剑气凝锋");
    checkStr(swordRealmName(120), "剑意通明", "120 剑意为剑意通明");
    checkStr(swordRealmName(220), "剑心圆融", "220 剑意为剑心圆融");
    checkStr(swordRealmName(360), "剑道化境", "360 剑意为剑道化境");
    checkStr(swordRealmName(500), "万剑归一", "500 剑意为万剑归一");

    checkEq(swordRank(0), 0, "剑意 0 -> 第 0 重");
    checkEq(swordRank(500), 6, "剑意 500 -> 第 6 重");
    checkEq(swordRank(9999), 6, "超出上限仍是第 6 重");

    std::printf("\n-- 剑修判定与旧剑意字段兼容 --\n");
    {
        Game g = makeCardGame(1);
        g.player.sect = "jinhong";
        check(swordCultivator(g.player), "金虹弟子是剑修");

        Game g2 = makeCardGame(1);
        g2.player.sect.clear();
        g2.player.arts.main = "sword";
        check(swordCultivator(g2.player), "主修剑典是剑修");

        Game g3 = makeCardGame(1);
        g3.player.sect.clear();
        g3.player.arts.main = "tuna";
        g3.player.arts.spells = {"sword", "rain"};
        check(swordCultivator(g3.player), "携带两种剑系神通是剑修");

        Game g4 = makeCardGame(1);
        g4.player.sect.clear();
        g4.player.arts.main = "tuna";
        g4.player.arts.spells = {"strike", "gold"};
        check(!swordCultivator(g4.player), "条件都不满足则不是剑修");
    }
    {
        Game g = makeCardGame(1);
        g.player.sect = "jinhong";
        g.player.sword = 120;   // 剑意通明
        Battle b;
        b.enemySword = 60;      // 剑气凝锋
        b.enemySwordCult = true;
        const SwordDuel d = swordDuelOf(g.player, b);
        check(!d.active, "双方旧剑意不同也不再产生永久境差");
        checkEq(d.gap, 0, "兼容境差接口恒为零");
        checkNear(d.attack, 1.0, "旧剑意不再增加攻击倍率", 1e-9);
        checkNear(d.reduction, 0, "旧剑意不再减少来伤", 1e-9);
    }
    {
        Game g = makeCardGame(1);
        g.player.sect = "jinhong";
        g.player.sword = 500;
        Battle b;
        b.enemySword = 0;
        b.enemySwordCult = false;
        const SwordDuel d = swordDuelOf(g.player, b);
        check(!d.active, "对手不是剑修时不计算境差");
        checkNear(d.attack, 1.0, "不生效时伤害倍率为 1", 1e-9);
    }
    {
        Game g = makeCardGame(1);
        g.player.sect = "jinhong";
        g.player.sword = 500;   // 第 6 重
        Battle b;
        b.enemySword = 0;       // 第 0 重
        b.enemySwordCult = true;
        const SwordDuel d = swordDuelOf(g.player, b);
        checkEq(d.gap, 0, "最高旧剑意值也不会恢复旧境差");
        checkNear(d.attack, 1.0, "最高旧剑意不加伤", 1e-9);
        checkNear(d.reduction, 0, "最高旧剑意不减伤", 1e-9);
        const int oldSword = g.player.sword;
        const int experience = g.player.arts.exp["剑"];
        swordGain(g, 12);
        checkEq(g.player.sword, oldSword, "剑道成长保留旧档字段而不再增加永久剑意");
        check(g.player.arts.exp["剑"] > experience, "剑道活动转为真正的悟道经验成长");
    }
}

} // namespace nztest
