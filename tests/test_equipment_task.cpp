// 两个已修 bug 的回归自测。
//
// 1) 真实新建角色按当前初装自动携带四个神通；
//    携带/卸下、槽位上限和生成牌组必须保持一致，主动卸空的旧档不补牌。
// 2) 主城接取战斗任务无效果
//    —— monthComplete 没有把营生类工作转交 ecoComplete，
//       猎妖委托不开打、差事不发工钱、委托不交工。
#include "harness.h"

#include <algorithm>
#include <string>

#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/save.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {

namespace {

bool carried(const Arts& a, const std::string& id) {
    return std::find(a.spells.begin(), a.spells.end(), id) != a.spells.end();
}

// 把在途工作推到结束（或进入斗法）
void runToCompletion(Game& g, int guardMax = 60) {
    int guard = 0;
    while (g.job.has_value() && !g.player.arts.battle.has_value() &&
           !g.ended && guard++ < guardMax) {
        calendarRun(g);
    }
}

} // namespace

void runEquipmentAndTaskTests() {
    std::printf("\n===== 开机神通与营生任务结算（回归）=====\n");

    // ------------------------------------------------------------
    // 1) 开局自动装备神通
    // ------------------------------------------------------------
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        checkEq(static_cast<long long>(g.player.arts.spells.size()), 4,
                "开局自动装备 4 个神通");
        check(!carried(g.player.arts, "strike"), "固定普通攻击不占额外神通名额");
        check(carried(g.player.arts, "gold"), "开局带金芒刺直伤");
        check(carried(g.player.arts, "fire"), "开局带焰火诀灼烧");
        check(carried(g.player.arts, "water"), "开局带水灵盾");
        check(carried(g.player.arts, "gather"), "开局带聚气回灵");

        // 四个携带神通各进牌组两张，外加普攻/防御各五张。
        EnemySpec e;
        e.name = "回归靶子";
        e.stage = 0;
        battleStart(g, e);
        const Battle& b = *g.player.arts.battle;
        checkEq(static_cast<long long>(b.deck.size()), 18,
                "开局牌组 18 张（4 神通×2 + 普攻5 + 防御5）");
        // 额外神通不重复固定基础卡。
        for (const auto& id : g.player.arts.spells) {
            int n = 0;
            for (const auto& c : b.deck)
                if (c.spell == id) ++n;
            const int want = 2;
            checkEq(n, want, "携带的神通「" + id + "」进牌组 " +
                                 std::to_string(want) + " 张");
        }
    }

    // 主动卸空额外神通应保留，固定基础牌仍然可用。
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        g.player.arts.spells.clear();
        const std::string text = saveToJson(g);

        Game back;
        std::string err;
        check(loadFromJson(text, back, &err), "空神通存档可以读入");
        check(back.player.arts.spells.empty(), "读档保留玩家卸空的额外神通");
        EnemySpec enemy; enemy.name = "基础牌靶子";
        battleStart(back, enemy);
        checkEq(back.player.arts.battle->deck.size(), 10, "额外神通为空仍固定携带十张普通招式");
    }

    // 「携带 / 卸下神通」这个动作此前完全不存在，现在应当可用
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        g.player.stage = 0;   // 炼气期拥有四个额外神通格子

        // 卸下一个
        const ActionError off = artInstant(g, "spell", "gold");
        check(off.ok, "可以卸下已携带的神通");
        check(!carried(g.player.arts, "gold"), "gold 已被卸下");
        checkEq(static_cast<long long>(g.player.arts.spells.size()), 3,
                "卸下后剩 3 个神通");

        // 再装回来
        const ActionError on = artInstant(g, "spell", "gold");
        check(on.ok, "可以把神通装回");
        check(carried(g.player.arts, "gold"), "gold 已重新携带");

        // 上限四个：已经有 4 个，再加第 5 个应被拒绝
        const ActionError over = artInstant(g, "spell", "wood");
        check(!over.ok, "超过四个神通会被拒绝");
        check(!over.error.empty(), "超过当前境界名额时给出提示");

        // 不存在的神通
        const ActionError bad = artInstant(g, "spell", "no-such-spell");
        check(!bad.ok, "不存在的神通不能携带");

        // 换了神通后，牌组应当跟着变
        check(artInstant(g, "spell", "gold").ok, "换装前卸下金芒刺");
        check(artInstant(g, "spell", "wood").ok, "换上已经领悟的回春术");
        EnemySpec e;
        e.name = "回归靶子";
        e.stage = 0;
        battleStart(g, e);
        int gold = 0, wood = 0;
        for (const auto& c : g.player.arts.battle->deck) {
            if (c.spell == "gold") ++gold;
            if (c.spell == "wood") ++wood;
        }
        checkEq(wood, 2, "换上的回春术进牌组 2 张");
        checkEq(gold, 0, "卸下的金芒刺不再进牌组");
    }

    // ------------------------------------------------------------
    // 2) 主城接取战斗任务（猎妖委托）必须真的打起来
    // ------------------------------------------------------------
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        g.player.location = "wuling";     // 武陵城（主城）
        g.player.stones = 500;
        g.player.health = 100;

        checkStr(ecoActionError(g, "hunt", ""), "", "主城可以接猎妖委托");

        const int huntBefore = g.economy.huntCounts;
        ecoBegin(g, "hunt", "");
        check(g.job.has_value(), "接单后进入在途工作");

        runToCompletion(g);

        check(g.player.arts.battle.has_value(),
              "猎妖委托到期后真的进入斗法（此前完全没有效果）");
        if (g.player.arts.battle.has_value()) {
            checkStr(g.player.arts.battle->kind, "hunt",
                     "猎妖战斗的类型是 hunt");
            checkStr(g.player.arts.battle->enemy, "任务妖兽",
                     "猎妖对手是任务妖兽");
        }
        checkEq(g.economy.huntCounts, huntBefore,
                "未分胜负前不计猎妖胜场");

        // 打完并获胜，应拿到赏金与胜场
        if (g.player.arts.battle.has_value()) {
            Battle& b = *g.player.arts.battle;
            b.enemyHp = 1;                 // 一击可破
            g.player.arts.battle->mana = 8;
            battleTurn(g, "strike");
            if (g.player.arts.battle.has_value()) {
                b.enemyHp = 0;
                battleFinish(g, true, false);
            }
            checkEq(g.economy.huntCounts, huntBefore + 1,
                    "猎妖获胜后胜场 +1");
        }
    }

    // ------------------------------------------------------------
    // 3) 普通营生任务（山门差事）到期要发工钱
    // ------------------------------------------------------------
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        g.player.location = "wuling";
        g.player.stones = 500;

        const int tasksBefore = g.economy.cityTasks;
        ecoBegin(g, "task", "errand");
        const int fee = g.job.has_value() ? static_cast<int>(g.job->fee) : 0;
        const int stonesBefore = g.player.stones;
        check(g.job.has_value(), "可以接山门差事");

        runToCompletion(g);

        checkEq(g.economy.cityTasks, tasksBefore + 1, "差事计数 +1");
        checkEq(g.player.stones, stonesBefore + fee,
                "差事到期发放工钱（此前分文没有）");
    }

    // ------------------------------------------------------------
    // 4) 参悟 / 寻矿 等营生行动也要真的结算
    // ------------------------------------------------------------
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        g.player.location = "wuling";
        g.player.stones = 500;

        // 寻矿：应产出矿材
        int matsBefore = 0;
        for (const auto& [k, v] : g.player.arts.materials) matsBefore += v;
        ecoBegin(g, "mine", "");
        runToCompletion(g);
        int matsAfter = 0;
        for (const auto& [k, v] : g.player.arts.materials) matsAfter += v;
        check(matsAfter > matsBefore, "寻矿到期真的产出矿材");

        // 参悟：应真的推进大道经验
        const int daoBefore = g.player.arts.exp.count("剑")
                                  ? g.player.arts.exp.at("剑")
                                  : 0;
        ecoBegin(g, "meditate", "剑");
        runToCompletion(g);
        const int daoAfter = g.player.arts.exp.count("剑")
                                 ? g.player.arts.exp.at("剑")
                                 : 0;
        check(daoAfter > daoBefore, "参悟到期真的增加大道经验");
    }

    std::printf("-- 开机神通与营生任务结算结束 --\n");
}

} // namespace nztest
