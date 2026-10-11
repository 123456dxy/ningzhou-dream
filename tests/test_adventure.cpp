// 游历与开局物品自测：对照 Demo 06 的 adventure.mjs。
//
// 期望值来自 tools/birth-check.mjs（JS 版实际运行结果）。
#include "harness.h"

#include "core/adventure.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {

namespace {

Game makeAdvGame(const vector<string>& traits, uint32_t seed = 1) {
    Game g;
    g.seed = seed;
    g.rng = seed;
    g.year = 0;
    g.clock.days = 0;
    g.clock.ageBase = 16;

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
    p.traits = traits;
    p.build.budget = 20;

    ensureArts(g);
    ensureEconomy(g);
    ensureAdventure(g);
    return g;
}

} // namespace

void runAdventureTests() {
    std::printf("\n-- 五项开局天赋（对照 Demo06 adventureBirth）--\n");

    checkEq(static_cast<long long>(advKinds().size()), 6, "游历遇事 6 类");

    {
        // 铸器传人：器道经验 30 + 锻剑铁 +4 + 百炼真解抄本
        Game g = makeAdvGame({"forgeheir"});
        const int ironBefore = g.player.arts.materials["iron"];
        adventureBirth(g);
        checkEq(g.player.arts.exp["器"], 30, "铸器传人给器道经验 30");
        checkEq(g.player.arts.materials["iron"], ironBefore + 4,
                "铸器传人给锻剑铁 4 份");
        const bool hasForge =
            std::find(g.player.arts.bookList.begin(),
                      g.player.arts.bookList.end(), "forge") !=
            g.player.arts.bookList.end();
        check(hasForge, "铸器传人给百炼真解抄本");
    }
    {
        // 丹鼎传家：三品丹炉
        Game g = makeAdvGame({"furnaceheir"});
        adventureBirth(g);
        checkStr(g.player.arts.furnace, "furnace-4", "丹鼎传人给 furnace-4");
    }
    {
        // 丹宗遗脉：四品丹炉
        Game g = makeAdvGame({"furnancescion"});
        adventureBirth(g);
        checkStr(g.player.arts.furnace, "furnace-5", "丹宗遗脉给 furnace-5");
    }
    {
        // 藏剑遗赠：一品灵剑且已装备
        Game g = makeAdvGame({"heirblade"});
        adventureBirth(g);
        checkEq(static_cast<long long>(g.player.arts.gear.size()), 1,
                "藏剑遗赠给 1 件法器");
        if (!g.player.arts.gear.empty()) {
            const Gear& gear = g.player.arts.gear.back();
            checkStr(gear.name, "祖传灵剑", "法器名为祖传灵剑");
            checkStr(gear.slot, "weapon", "法器为武器");
            checkEq(gear.grade, 1, "法器为一品");
            checkNear(gear.bonus, 0.04, "法器伤害 +4%", 1e-9);
            checkStr(gear.element, "剑", "法器属剑");
            checkEq(gear.craftCost, 35, "法器铸造成本 35");
            checkEq(static_cast<long long>(gear.dimensions.size()), 5,
                    "法器有五维");
            if (gear.dimensions.size() == 5)
                checkEq(gear.dimensions[0], 300, "五维首项 300");
        }
        checkStr(g.player.arts.equipped["weapon"], "gear-0-0",
                 "灵剑已装备在武器位");
    }
    {
        // 护身旧甲：一品法衣且已装备
        Game g = makeAdvGame({"artifactward"});
        adventureBirth(g);
        checkEq(static_cast<long long>(g.player.arts.gear.size()), 1,
                "护身旧甲给 1 件法器");
        if (!g.player.arts.gear.empty()) {
            const Gear& gear = g.player.arts.gear.back();
            checkStr(gear.name, "护身法衣", "法器名为护身法衣");
            checkStr(gear.slot, "armor", "法器为护甲");
            checkNear(gear.bonus, 0.06, "法器气血上限 +6%", 1e-9);
            checkStr(gear.element, "土", "法器属土");
        }
        checkStr(g.player.arts.equipped["armor"], "gear-0-0",
                 "法衣已装备在护甲位");
    }
    {
        // 普通天赋不发任何物品
        Game g = makeAdvGame({"sword"});
        adventureBirth(g);
        checkEq(static_cast<long long>(g.player.arts.gear.size()), 0,
                "普通天赋不发法器");
        checkStr(g.player.arts.furnace, "furnace-1", "普通天赋用基础丹炉");
        checkEq(g.player.arts.exp["器"], 0, "普通天赋不加器道经验");
    }

    std::printf("\n-- 游历累计与遇事 --\n");
    {
        // 不足 20 天不会遇事
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureExplore(g, 10);
        checkEq(g.adventure.since, 10, "累计游历 10 天");
        checkEq(g.adventure.total, 10, "总游历 10 天");
        check(!g.pending.has_value(), "不足 20 天不触发遇事");
    }
    {
        // 连续累计满 60 天必遇一段
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureExplore(g, 30);
        const bool first = g.pending.has_value();
        if (!first) {
            adventureExplore(g, 30);   // 累计 60 天
        }
        check(g.pending.has_value(), "连续累计满 60 天必定遇事");
        checkEq(g.adventure.events, 1, "只触发一次遇事");
    }
    {
        // 遇事会重置 since
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureExplore(g, 60);
        if (g.pending.has_value()) {
            checkEq(g.adventure.since, 0, "遇事后累计天数清零");
        }
    }
    {
        // 已有待处理抉择时不再触发新遇事
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureExplore(g, 60);
        const int events = g.adventure.events;
        adventureExplore(g, 60);
        checkEq(g.adventure.events, events, "有抉择时不再触发新遇事");
    }
    {
        // 遇事的类型与选项
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureExplore(g, 60);
        if (g.pending.has_value()) {
            checkStr(g.pending->type, "adventure", "抉择类型为 adventure");
            checkEq(static_cast<long long>(g.pending->options.size()), 3,
                    "遇事给 3 个选项");
            check(g.pending->adventure.has_value(), "带游历上下文");
            if (g.pending->adventure.has_value()) {
                const AdvContext& d = *g.pending->adventure;
                checkEq(d.seq, 1, "首个遇事序号为 1");
                checkEq(d.fee, 18, "炼气期赏金 18");
                checkEq(d.grade, 1, "炼气期法器品阶 1");
                check(d.stage == 0 || d.stage == 1, "对手为同境或高一小境");
            }
        }
    }

    std::printf("\n-- 后山探访 --\n");
    {
        Game g = makeAdvGame({"sword"});
        g.player.location = "wuling";
        checkStr(adventureActionError(g, "backhill"), "",
                 "武陵城可以探访后山");

        g.player.health = 20;
        checkStr(adventureActionError(g, "backhill"), "请先休养至35气血。",
                 "气血不足不可探访");
        g.player.health = 100;

        g.player.stones = 5;
        checkStr(adventureActionError(g, "backhill"), "探访需8灵石整备。",
                 "灵石不足不可探访");
        g.player.stones = 500;

        g.player.location = "fanghu";   // 野外
        checkStr(adventureActionError(g, "backhill"),
                 "请在城池或本宗山门探访后山。", "野外不可探访后山");
    }
    {
        // 本月已探访过则不可重复
        Game g = makeAdvGame({"sword"});
        g.player.location = "wuling";
        adventureBegin(g, "backhill");
        checkStr(adventureActionError(g, "backhill"), "本月已探访过后山。",
                 "同月不能重复探访后山");
        checkEq(g.player.stones, 492, "探访扣 8 灵石");
        check(g.job.has_value(), "探访会建在途工作");
        if (g.job.has_value()) {
            checkEq(g.job->total, 7, "探访需 7 天");
            checkStr(g.job->name, "探访后山", "工作名为探访后山");
        }
    }
    {
        // 探访完成直接进入后山遗藏遇事
        Game g = makeAdvGame({"sword"});
        g.player.location = "wuling";
        adventureBegin(g, "backhill");
        adventureComplete(g);
        check(g.pending.has_value(), "探访完成触发后山遗藏");
        if (g.pending.has_value() && g.pending->adventure.has_value())
            checkStr(g.pending->adventure->kind, "backhill",
                     "触发的是后山遗藏");
    }

    std::printf("\n-- 非战斗选项结算 --\n");
    {
        // 残碑：修补碑文花 12 灵石换抄本
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "heritage");
        const int before = g.player.stones;
        const size_t booksBefore = g.economy.books.size();
        adventureChoose(g, "book");
        checkEq(g.player.stones, before - 12, "修补碑文扣 12 灵石");
        check(g.economy.books.size() >= booksBefore, "抄本数量不减少");
        check(!g.pending.has_value(), "选择后抉择清空");
    }
    {
        // 残碑：只参悟道韵给悟道经验 + 剑意 +1
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "heritage");
        const int focusBefore = g.player.arts.exp[g.player.arts.focus];
        const int swordBefore = g.player.sword;
        const int swordDaoBefore = g.player.arts.exp["剑"];
        adventureChoose(g, "study");
        checkEq(g.player.arts.exp[g.player.arts.focus], focusBefore + 8,
                "参悟道韵给当前大道 +8 经验");
        checkEq(g.player.sword, swordBefore, "参悟不再增加独立剑意");
        check(g.player.arts.exp["剑"] >= swordDaoBefore + 1, "参悟额外获得剑道经验");
    }
    {
        // 行商：帮忙修车阵赚 16 灵石
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "caravan");
        const int before = g.player.stones;
        adventureChoose(g, "help");
        checkEq(g.player.stones, before - 8 + 16, "修车阵净赚 8 灵石");
        checkEq(g.player.arts.exp["器"], 4, "修车阵给器道经验 +4");
    }
    {
        // 行商：交换矿材
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "caravan");
        const int before = g.player.stones;
        adventureChoose(g, "trade");
        checkEq(g.player.stones, before - 12, "交换矿材扣 12 灵石");
        checkEq(g.player.arts.materials["iron"], 4 + 2, "获得同阶矿材 2 份");
    }
    {
        // 劫修：交路资
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "bandit");
        const int before = g.player.stones;
        adventureChoose(g, "talk");
        checkEq(g.player.stones, before - 12, "交路资扣 12 灵石");
    }
    {
        // 妖兽：引开妖兽得灵药
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "beast");
        const int before = g.player.herbs;
        adventureChoose(g, "talk");
        checkEq(g.player.herbs, before + 1, "引开妖兽给未分类灵药 +1");
    }
    {
        // 行侠：掩护撤离得声望，掉 3 血
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "justice");
        const int renownBefore = g.player.renown;
        const int healthBefore = g.player.health;
        adventureChoose(g, "talk");
        checkEq(g.player.renown, renownBefore + 1, "掩护撤离给声望 +1");
        checkEq(g.player.health, healthBefore - 3, "掩护撤离气血 −3");
        check(g.player.health >= 1, "掩护撤离不致死");
    }
    {
        // 后山：离开给阵道经验
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "backhill");
        adventureChoose(g, "leave");
        checkEq(g.player.arts.exp["阵"], 4, "后山离开给阵道经验 +4");
    }

    std::printf("\n-- 游历斗法奖励 --\n");
    {
        // 妖兽胜：赏金 + 矿材 2 + 灵药 2
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "beast");
        adventureChoose(g, "fight");
        check(g.player.arts.battle.has_value(), "迎战会开始斗法");
        if (g.player.arts.battle.has_value()) {
            Battle b = *g.player.arts.battle;
            b.won = true;
            b.advKind = "beast";
            b.advFee = 18;
            b.advSeq = 1;
            const int stonesBefore = g.player.stones;
            const int herbsBefore = g.player.herbs;
            adventureBattleEnd(g, b);
            checkEq(g.player.stones, stonesBefore + 18, "妖兽胜利给赏金 18");
            checkEq(g.player.herbs, herbsBefore + 2, "妖兽胜利给灵药 2");
            checkEq(g.player.arts.materials["iron"], 4 + 2,
                    "妖兽胜利给矿材 2");
        }
    }
    {
        // 落败且未撤离：丢失有限行装
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "beast");
        adventureChoose(g, "fight");
        if (g.player.arts.battle.has_value()) {
            Battle b = *g.player.arts.battle;
            b.won = false;
            b.retreat = false;
            b.advKind = "beast";
            b.advFee = 18;
            const int before = g.player.stones;
            adventureBattleEnd(g, b);
            checkEq(g.player.stones, before - 8, "败退丢失 8 灵石行装");
        }
    }
    {
        // 撤离不丢财物
        Game g = makeAdvGame({"sword"}, 20261004);
        adventureEvent(g, "beast");
        adventureChoose(g, "fight");
        if (g.player.arts.battle.has_value()) {
            Battle b = *g.player.arts.battle;
            b.won = false;
            b.retreat = true;
            b.advKind = "beast";
            b.advFee = 18;
            const int before = g.player.stones;
            adventureBattleEnd(g, b);
            checkEq(g.player.stones, before, "撤离不丢失财物");
        }
    }
}

} // namespace nztest
