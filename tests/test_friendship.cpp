// 自测：道友往来（好感度 / 交情 / NPC 背包 / 送装备）
//
// 这一块单独立测试，是因为它踩过几个只有真跑才会现形的坑：
//   · npcTryEquip 里持有 vector 元素的引用，push_back 触发重新分配后
//     那个引用成了野指针 —— 直接 bad_alloc（已经不是「偶尔出错」级别了）
//   · 打招呼如果不去重，配合「不耗时」就是无限刷好感
//   · 交情如果不夹到 >= 0，花超了会变成负数
#include <string>

#include "core/friendship.h"
#include "core/play.h"
#include "core/save.h"
#include "core/sim.h"
#include "harness.h"

namespace nztest {

using namespace nz;

void runFriendshipTests() {
    CreateOptions o;
    o.seed = 4242;
    o.traits = {"bright"};
    Game g = createGame(o);
    friendship::ensureAllNpcData(g);

    Npc& n = g.npcs[0];
    n.location = g.player.location;

    // ---- 打招呼：只有第一次有收益 ----
    {
        const int days0 = g.clock.days;
        const auto r1 = friendship::greet(g, n.id);
        check(r1.ok && r1.firstTime, "打招呼第一次有收益");
        check(r1.affinityDelta != 0, "第一次见礼确实改变了好感");

        const auto r2 = friendship::greet(g, n.id);
        check(r2.ok && !r2.firstTime, "再打招呼标记为「非首次」");
        checkEq(r2.affinityDelta, 0, "再打招呼不再加好感");
        checkEq(g.clock.days, days0, "打招呼不消耗天数");
    }

    // ---- 好感度可负，且与交情独立 ----
    {
        g.player.relations[n.id] = -50;
        check(relationOf(g, n.id) < 0, "好感度可以为负");
        friendship::addCredit(g, n.id, 30);
        check(friendship::creditOf(g, n.id) > 0, "交情可以为正");
        check(relationOf(g, n.id) < 0 && friendship::creditOf(g, n.id) > 0,
              "「讨厌你但欠你人情」是合法状态");
    }

    // ---- 送礼：加交情，且不耗时 ----
    {
        g.player.arts.herbs["恒心草"] = 5;
        g.player.herbs = 0;
        for (const auto& [name, count] : g.player.arts.herbs) g.player.herbs += count;
        const int days0 = g.clock.days;
        const auto r = friendship::giftItem(g, n.id, "herb", "恒心草");
        check(r.ok, "送礼成功");
        check(r.creditDelta > 0, "送礼加交情");
        checkEq(g.clock.days, days0, "送礼不消耗天数");
        checkEq(g.player.arts.herbs["恒心草"], 4, "送礼扣掉一份药材");
    }

    // ---- 交情只增不减（花掉才减），且不会为负 ----
    {
        const int before = friendship::creditOf(g, n.id);
        friendship::addCredit(g, n.id, -10000);
        checkEq(friendship::creditOf(g, n.id), 0, "交情被夹到 0，不会变负");
        friendship::addCredit(g, n.id, before);
    }

    // ---- NPC 背包：一定翻得到东西 ----
    {
        const auto bag = friendship::npcBag(g, n.id);
        check(!bag.empty(), "NPC 背包不是空的");
        bool priced = true;
        for (const auto& it : bag)
            if (it.creditCost <= 0 || it.value < 0) priced = false;
        check(priced, "背包每件东西都有正的换取代价");
    }

    // ---- 花交情换取 ----
    {
        friendship::addCredit(g, n.id, 2000);
        const auto bag = friendship::npcBag(g, n.id);
        check(!bag.empty(), "有东西可换");
        if (!bag.empty()) {
            const int before = friendship::creditOf(g, n.id);
            const auto r = friendship::takeFromNpc(g, n.id, bag[0].kind, bag[0].id);
            check(r.ok, "花交情换到了东西");
            check(friendship::creditOf(g, n.id) < before, "交情被扣掉");
            checkEq(friendship::creditOf(g, n.id), before - r.cost,
                    "扣掉的正好是标价");
        }
        // 交情不够时应当拒绝
        friendship::addCredit(g, n.id, -friendship::creditOf(g, n.id));
        const auto bag2 = friendship::npcBag(g, n.id);
        if (!bag2.empty()) {
            const auto r = friendship::takeFromNpc(g, n.id, bag2[0].kind, bag2[0].id);
            check(!r.ok, "交情不够时换不了");
        }
    }

    // ---- 送装备：NPC 比自身好就换上 ----
    // 这一项曾经因为 vector 引用失效直接 bad_alloc，必须守住。
    {
        // 先把 NPC 身上清干净，确保从「空槽位」和「有旧装备」两条路都走一遍
        n.gear.clear();
        n.equipped.clear();

        Gear weak;
        weak.id = "weak-sword"; weak.name = "锈剑"; weak.slot = "weapon";
        weak.grade = 1; weak.bonus = 0.01; weak.craftCost = 30;
        g.player.arts.gear.push_back(weak);
        const auto r1 = friendship::giftItem(g, n.id, "gear", "weak-sword");
        check(r1.ok, "送出第一件法器");
        check(n.equipped.count("weapon") > 0 &&
                  n.equipped["weapon"] == "weak-sword",
              "空槽位直接换上");

        // 再送一件更好的 —— 这里会走「替换旧装备」的分支
        Gear strong;
        strong.id = "strong-sword"; strong.name = "青锋剑"; strong.slot = "weapon";
        strong.grade = 4; strong.bonus = 0.09; strong.craftCost = 400;
        g.player.arts.gear.push_back(strong);
        const auto r2 = friendship::giftItem(g, n.id, "gear", "strong-sword");
        check(r2.ok, "送出第二件（更好的）法器");
        check(n.equipped["weapon"] == "strong-sword", "更好的法器被换上");
        check(r2.text.find("青锋剑") != std::string::npos,
              "提示里写明了换上的法器");

        // 送一件更差的：不该被换上
        Gear worse;
        worse.id = "worse-sword"; worse.name = "木棍"; worse.slot = "weapon";
        worse.grade = 1; worse.bonus = 0.0; worse.craftCost = 10;
        g.player.arts.gear.push_back(worse);
        const auto r3 = friendship::giftItem(g, n.id, "gear", "worse-sword");
        check(r3.ok, "送出更差的法器（收下但不换）");
        check(n.equipped["weapon"] == "strong-sword", "更差的法器不会被换上");

        // 玩家背包里这三件都应该没了
        bool allGone = true;
        for (const auto& x : g.player.arts.gear)
            if (x.id == "weak-sword" || x.id == "strong-sword" ||
                x.id == "worse-sword")
                allGone = false;
        check(allGone, "送出的法器都从玩家背包扣掉了");
    }

    // ---- 存档往返 ----
    {
        const std::string txt = saveToJson(g);
        Game rt;
        std::string err;
        check(loadFromJson(txt, rt, &err), "含道友往来的存档能读回");
        checkEq(friendship::creditOf(rt, n.id), friendship::creditOf(g, n.id),
                "交情往返一致");
        auto bit = rt.player.npcBonds.find(n.id);
        check(bit != rt.player.npcBonds.end() && bit->second.greeted,
              "打招呼记录往返一致");
    }
}

} // namespace nztest
