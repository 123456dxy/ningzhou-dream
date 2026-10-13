#include "harness.h"

#include <algorithm>
#include <limits>

#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/friendship.h"
#include "core/json.h"
#include "core/npc_learning.h"
#include "core/play.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"

using namespace nz;

namespace nztest {
namespace {
Game socialGame() {
    CreateOptions options; options.name = "社交完整性回归"; options.seed = 734;
    Game g = createGame(options);
    g.npcs.clear(); g.pending.reset(); g.job.reset();
    g.player.location = "wuling"; g.player.stones = 500;
    g.player.herbs = 10; g.player.arts.herbs = {{"恒心草", 10}};
    Npc n; n.id = "social-integrity"; n.name = "同地道友";
    n.location = "wuling"; n.stage = 0; n.age = 30; n.lifespan = realmOf(0).life;
    n.personality = "重情重义"; n.dao = "木";
    n.bag = {{"herb:恒心草", 2}};
    g.npcs.push_back(n);
    return g;
}

void blockedActions(Game& g, const string& label) {
    const string before = saveToJson(g);
    const string id = g.npcs[0].id;
    check(!friendship::greet(g, id).ok && !friendship::giftItem(g, id, "herb", "恒心草").ok &&
          !friendship::takeFromNpc(g, id, "herb", "恒心草").ok && !play::socialDo(g, id, "chat").ok,
          label + "拦截见礼、送礼、换物与社交");
    checkStr(saveToJson(g), before, label + "不改变双方库存、余额、关系、交情与月份记录");
}

void rejectSocialSave(const json::Value& invalid, const string& label) {
    Game current = socialGame(); current.player.name = "不能被损坏档替换的当前角色";
    current.player.stones = 321;
    const string before = saveToJson(current);
    string error;
    check(!loadFromJson(invalid.dump(), current, &error) && !error.empty(), label + "拒绝读取并说明原因");
    checkStr(saveToJson(current), before, label + "不替换当前角色或状态");
}
}

void runSocialIntegrityTests() {
    std::printf("\n-- 道友往来：统一限制、月内限额与真实礼物 --\n");
    {
        Game g = socialGame(); g.npcs[0].location = "yunxi";
        blockedActions(g, "远程调用");
        g = socialGame(); g.npcs[0].alive = false;
        blockedActions(g, "已故道友");
        g = socialGame(); g.npcs[0].health = 0;
        blockedActions(g, "无气血道友");
        g = socialGame(); g.npcs[0].age = g.npcs[0].lifespan;
        blockedActions(g, "寿尽道友");
        g = socialGame(); g.pending = Pending{};
        blockedActions(g, "待处理抉择");
        g = socialGame(); Job job; job.actionType = "plan"; job.plan = "rest";
        job.total = job.remaining = 5; g.job = job;
        blockedActions(g, "工作进行中");
        g = socialGame(); g.player.arts.battle = Battle{};
        blockedActions(g, "斗法进行中");
        g = socialGame(); g.ended = true;
        blockedActions(g, "人生结束");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        const int insight = g.player.insight;
        check(play::socialDo(g, id, "chat").ok, "本月首次闲谈成功");
        checkEq(g.player.insight, insight + 1, "首次闲谈实际获得悟性");
        const auto before = saveToJson(g);
        check(!play::socialDo(g, id, "chat").ok, "同人同月闲谈不能无限刷悟性");
        checkStr(saveToJson(g), before, "重复闲谈拒绝时没有任何收益");
        g.player.relations[id] = 50;
        check(play::socialDo(g, id, "study").ok && play::socialDo(g, id, "deep").ok,
              "讨论修行与彻夜论道共享两次月限额");
        const int stones = g.player.stones;
        check(!play::socialDo(g, id, "study").ok && !play::socialDo(g, id, "deep").ok,
              "两次之后两条入口都拒绝重复讨论");
        checkEq(g.player.stones, stones, "达到月限额后不会先扣讨论灵石");
        check(play::socialDo(g, id, "travel").ok && !play::socialDo(g, id, "travel").ok,
              "结伴游历同一人每月只领取一次收益");
        g.clock.days = 30;
        check(play::socialDo(g, id, "chat").ok && play::socialDo(g, id, "study").ok &&
              play::socialDo(g, id, "travel").ok, "实际跨月后闲谈、讨论、结伴限额可再使用");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        g.player.relations[id] = 50;
        check(play::socialDo(g, id, "deep").ok, "没有道侣时仍可与道友彻夜论道");
        checkEq(g.bonds.bondDays, 0, "与普通道友论道不虚增道侣相伴天数");

        g = socialGame(); g.player.relations[id] = 50;
        g.bonds.partner = "another-partner"; g.bonds.bondDays = 85;
        check(play::socialDo(g, id, "deep").ok, "已有道侣时仍可与其他道友论道");
        checkEq(g.bonds.bondDays, 85, "与其他道友论道不推进现任道侣的相伴记录");
        const auto& stories = data::StoryDef_TABLE();
        const auto sharedMoon = std::find_if(stories.begin(), stories.end(), [](const auto& story) {
            return story.id == "sharedmoon";
        });
        check(sharedMoon != stories.end() && !storyRequiresMet(g, *sharedMoon),
              "与别人论道不能绕过同看一轮月的九十天相伴门槛");

        g = socialGame(); g.player.relations[id] = 50;
        g.bonds.partner = id; g.bonds.bondDays = 85;
        check(play::socialDo(g, id, "deep").ok, "仍可与现任道侣彻夜论道");
        checkEq(g.bonds.bondDays, 90, "与现任道侣论道真实累计五天相伴");
        check(sharedMoon != stories.end() && storyRequiresMet(g, *sharedMoon),
              "真实相伴满九十天才满足同看一轮月门槛");
    }
    {
        Game g = socialGame(); Npc& n = g.npcs[0]; const string id = n.id;
        n.stage = 9; n.lifespan = realmOf(9).life; n.sect = "zhushan";
        const auto preferred = friendship::giftOutcome(g, n, 100, "herb", "恒心草");
        const auto neutral = friendship::giftOutcome(g, n, 100);
        check(preferred.affinity > neutral.affinity && preferred.affinity > 0,
              "低境界给元婴道友送偏好礼物依价值加好感，不被自动视为侮辱");
        const int bag = n.bag["herb:恒心草"];
        const int herbs = g.player.herbs;
        auto first = friendship::giftItem(g, id, "herb", "恒心草");
        check(first.ok, "药材实际送入对方背包");
        checkEq(n.bag["herb:恒心草"], bag + 1, "道友药材库存增加一份");
        checkEq(g.player.herbs, herbs - 1, "送药材同步玩家总量");
        friendship::giftItem(g, id, "herb", "恒心草");
        friendship::giftItem(g, id, "herb", "恒心草");
        const auto fourth = friendship::giftItem(g, id, "herb", "恒心草");
        check(fourth.ok && fourth.affinityDelta < first.affinityDelta,
              "第四次送礼仍可进行，好感收益递减");
        checkEq(fourth.creditDelta, first.creditDelta, "反复送礼交情按真实价值保持");
        friendship::addCredit(g, id, 100);
        const int totalBefore = g.player.herbs;
        check(friendship::takeFromNpc(g, id, "herb", "恒心草").ok, "花交情真正取回一份NPC药材");
        checkEq(g.player.herbs, totalBefore + 1, "取药材同步玩家总量");
        checkEq(g.player.herbs, g.player.arts.herbs["恒心草"], "分类药材与总药材保持一致");
    }
    {
        Game g = socialGame(); Npc& n = g.npcs[0];
        Gear weak; weak.id = "gift-first"; weak.name = "第一柄剑"; weak.slot = "weapon";
        weak.grade = 1; weak.bonus = 0.1; weak.element = "木";
        g.player.arts.gear.push_back(weak);
        check(friendship::giftItem(g, n.id, "gear", weak.id).ok, "赠送首件法器成功");
        checkEq(n.gear.size(), 1, "空槽首次接收法器只存一件，不重复push");
        checkStr(n.equipped["weapon"], weak.id, "首次法器实际装备");
        Gear strong = weak; strong.id = "gift-better"; strong.bonus = 0.2;
        g.player.arts.gear.push_back(strong);
        check(friendship::giftItem(g, n.id, "gear", strong.id).ok, "赠送更好法器成功");
        checkEq(n.gear.size(), 2, "换装后保留新旧各一件");
        checkStr(n.equipped["weapon"], strong.id, "更好的法器真实换上");
        Gear inferior = weak; inferior.id = "gift-inferior"; inferior.bonus = 0.01;
        g.player.arts.gear.push_back(inferior);
        check(friendship::giftItem(g, n.id, "gear", inferior.id).ok, "较差礼物仍接收到库存");
        checkEq(n.gear.size(), 3, "较差法器接收一次且不重复");
        checkStr(n.equipped["weapon"], strong.id, "较差法器不覆盖当前装备");
        check(g.player.arts.gear.empty(), "赠送法器真实从玩家背包移出");
        g.player.arts.potions["5001"] = 2;
        n.health = 50;
        check(friendship::giftItem(g, n.id, "potion", "5001").ok && n.health > 50,
              "受伤道友实际服用赠予的疗伤丹");
        checkEq(g.player.arts.potions["5001"], 1, "赠丹真实扣玩家一枚");
        check(std::find(n.pillsUsed.begin(), n.pillsUsed.end(), "5001") != n.pillsUsed.end(),
              "NPC记录已经服用的丹药");
        n.health = 100;
        check(friendship::giftItem(g, n.id, "potion", "5001").ok && n.bag["potion:5001"] == 1,
              "满血后赠予的疗伤丹留在NPC背包，不浪费药效");
    }
    {
        Game g = socialGame(); Npc& n = g.npcs[0];
        check(friendship::giftItem(g, n.id, "herb", "恒心草").ok, "循环回归先实际赠送一份药材");
        const int credit = friendship::creditOf(g, n.id);
        check(!friendship::takeFromNpc(g, n.id, "herb", "恒心草").ok,
              "刚送礼赚取的交情不能无成本取回同物无限刷好感");
        checkEq(friendship::creditOf(g, n.id), credit, "取回交情不足时不先扣人情");
        const auto quote = friendship::npcBag(g, n.id);
        auto herb = std::find_if(quote.begin(), quote.end(), [](const auto& item) {
            return item.kind == "herb" && item.id == "恒心草";
        });
        check(herb != quote.end() && herb->creditCost > credit, "背包报价显示取用真实人情成本");
        friendship::addCredit(g, n.id, 10);
        const int before = friendship::creditOf(g, n.id);
        const auto traded = friendship::takeFromNpc(g, n.id, "herb", "恒心草");
        check(traded.ok && herb != quote.end() && traded.cost == herb->creditCost,
              "补足交情后按显示报价真正取回药材");
        checkEq(friendship::creditOf(g, n.id), before - traded.cost, "同物返还循环实际消耗额外交情");
        g.player.arts.herbs["恒心草"] = 40; g.player.herbs = 40;
        for (int i = 0; i < 30; ++i) friendship::giftItem(g, n.id, "herb", "恒心草");
        const auto repeated = friendship::giftItem(g, n.id, "herb", "恒心草");
        check(repeated.ok && repeated.affinityDelta == 0 && repeated.creditDelta > 0,
              "反复小额赠礼的好感最终为0，仍允许送礼并保留实际交情");
    }
    {
        for (const string& kind : {string("herb"), string("potion"), string("material")}) {
            Game g = socialGame(); const string npcId = g.npcs[0].id;
            const string itemId = kind == "herb" ? "恒心草" : kind == "potion" ? "5001" : "iron";
            if (kind == "potion") g.player.arts.potions[itemId] = 2;
            if (kind == "material") g.player.arts.materials[itemId] = 2;
            g.npcs[0].bag[kind + ":" + itemId] = 1000000000;
            const string before = saveToJson(g);
            Game restored; string error;
            check(loadFromJson(before, restored, &error), kind + "赠礼边界使用可读取的合法库存");
            check(!friendship::giftItem(g, npcId, kind, itemId).ok,
                  kind + "单件赠礼拒绝让NPC库存超过存档上限");
            check(saveToJson(g) == before, kind + "NPC库存已满时不扣物品、不发好感或交情");
            check(loadFromJson(saveToJson(g), restored, &error), kind + "拒绝赠礼后存档仍可读取");

            g = socialGame();
            if (kind == "potion") g.player.arts.potions[itemId] = 2;
            if (kind == "material") g.player.arts.materials[itemId] = 2;
            g.npcs[0].bag[kind + ":" + itemId] = 999999999;
            check(friendship::giftItem(g, npcId, kind, itemId).ok,
                  kind + "NPC剩一份容量时仍可正常赠礼");
            checkEq(g.npcs[0].bag[kind + ":" + itemId], 1000000000,
                    kind + "正常赠礼可到达库存上限");
            check(loadFromJson(saveToJson(g), restored, &error), kind + "上限赠礼结果仍可存档读取");
        }
    }
    {
        for (const string& kind : {string("herb"), string("potion"), string("material")}) {
            Game g = socialGame(); const string npcId = g.npcs[0].id;
            const string itemId = kind == "herb" ? "恒心草" : kind == "potion" ? "5001" : "iron";
            auto& inventory = kind == "herb" ? g.player.arts.herbs :
                kind == "potion" ? g.player.arts.potions : g.player.arts.materials;
            inventory[itemId] = 1000000000;
            if (kind == "herb") g.player.herbs = 1000000000;
            g.npcs[0].bag[kind + ":" + itemId] = 1;
            friendship::addCredit(g, npcId, 10000);
            const auto bag = friendship::npcBag(g, npcId);
            const auto quote = std::find_if(bag.begin(), bag.end(), [&](const auto& item) {
                return item.kind == kind && item.id == itemId;
            });
            check(quote != bag.end() && !quote->affordable && !quote->reason.empty(),
                  kind + "玩家库存已满时NPC报价显示不可换物原因");
            const string before = saveToJson(g);
            Game restored; string error;
            check(loadFromJson(before, restored, &error), kind + "换物边界使用可读取的合法库存");
            check(!friendship::takeFromNpc(g, npcId, kind, itemId).ok,
                  kind + "换物拒绝让玩家库存超过存档上限");
            check(saveToJson(g) == before, kind + "玩家库存已满时不扣交情或NPC物品");
            check(loadFromJson(saveToJson(g), restored, &error), kind + "拒绝换物后存档仍可读取");

            Game boundary = socialGame();
            auto& boundaryInventory = kind == "herb" ? boundary.player.arts.herbs :
                kind == "potion" ? boundary.player.arts.potions : boundary.player.arts.materials;
            boundaryInventory[itemId] = 999999999;
            if (kind == "herb") boundary.player.herbs = 999999999;
            boundary.npcs[0].bag[kind + ":" + itemId] = 1;
            friendship::addCredit(boundary, npcId, 10000);
            check(friendship::takeFromNpc(boundary, npcId, kind, itemId).ok,
                  kind + "玩家剩一份容量时仍可正常换物");
            checkEq(boundaryInventory[itemId], 1000000000, kind + "正常换物可到达库存上限");
            check(loadFromJson(saveToJson(boundary), restored, &error), kind + "上限换物结果仍可存档读取");
        }
    }
    {
        Game g = socialGame(); const string npcId = g.npcs[0].id;
        g.player.arts.herbs = {{"恒心草", 999999999}, {"银月花", 1}};
        g.player.herbs = 1000000000;
        friendship::addCredit(g, npcId, 10000);
        const auto bag = friendship::npcBag(g, npcId);
        const auto quote = std::find_if(bag.begin(), bag.end(), [](const auto& item) {
            return item.kind == "herb" && item.id == "恒心草";
        });
        check(quote != bag.end() && !quote->affordable && !quote->reason.empty(),
              "药材分类数量未满但总量已满时NPC报价同样不可换物");
        const string before = saveToJson(g);
        check(!friendship::takeFromNpc(g, npcId, "herb", "恒心草").ok,
              "分类药材还有容量但药材总量已满时仍拒绝换物");
        check(saveToJson(g) == before, "药材总量上限同样保护NPC库存和玩家交情");
    }
    {
        struct PurchaseBoundary { int owned, count; bool allowed; };
        const PurchaseBoundary boundaries[] = {
            {1000000000, 1, false}, {999999999, 2, false}, {999999999, 1, true}
        };
        for (const string& kind : {string("herb"), string("potion"), string("material")}) {
            const string itemId = kind == "herb" ? "恒心草" : kind == "potion" ? "5001" : "iron";
            for (const auto& boundary : boundaries) {
                Game g = socialGame();
                auto& inventory = kind == "herb" ? g.player.arts.herbs :
                    kind == "potion" ? g.player.arts.potions : g.player.arts.materials;
                inventory[itemId] = boundary.owned;
                if (kind == "herb") g.player.herbs = boundary.owned;
                const string before = saveToJson(g);
                const int stones = g.player.stones;
                const auto error = play::marketBuy(g, kind, itemId, boundary.count);
                const string label = kind + "坊市购买" + std::to_string(boundary.count) + "份";
                check(error.empty() == boundary.allowed, label + "按实际背包剩余容量执行");
                if (boundary.allowed) {
                    checkEq(inventory[itemId], 1000000000, label + "允许恰好达到存档上限");
                    check(g.player.stones < stones, label + "仍支付真实灵石价款");
                } else {
                    check(saveToJson(g) == before, label + "超过容量时不扣灵石、不增加物品");
                }
                Game restored; string loadError;
                check(loadFromJson(saveToJson(g), restored, &loadError), label + "执行后存档仍可读取");
            }
        }
    }
    {
        Game g = socialGame();
        g.player.arts.herbs = {{"恒心草", 999999999}, {"银月花", 1}};
        g.player.herbs = 1000000000;
        const string before = saveToJson(g);
        check(!play::marketBuy(g, "herb", "恒心草", 1).empty(),
              "坊市买药材同样受分类药材合计的存档上限约束");
        check(saveToJson(g) == before, "总药材已满时购买不扣灵石或制造不可读存档");
    }
    {
        Game g = socialGame(); g.player.herbs = 1000000000;
        Game restored; string error;
        const string before = saveToJson(g);
        check(loadFromJson(before, restored, &error), "保留未分类药材总量的旧库存记录可读取");
        check(!play::marketBuy(g, "herb", "恒心草", 1).empty(),
              "分类记录不足的旧库存仍不能让药材总数购买后超过上限");
        check(saveToJson(g) == before, "旧库存总数已满时拒绝购买不产生任何扣款");
    }
    {
        Game g = socialGame(); const string npcId = g.npcs[0].id;
        Gear gear; gear.id = "quote-equipped"; gear.name = "道友佩剑";
        gear.slot = "weapon"; gear.grade = 1; gear.bonus = 0.1;
        g.npcs[0].gear.push_back(gear); g.npcs[0].equipped["weapon"] = gear.id;
        friendship::addCredit(g, npcId, 10000);
        const auto bag = friendship::npcBag(g, npcId);
        const auto quote = std::find_if(bag.begin(), bag.end(), [&](const auto& item) {
            return item.kind == "gear" && item.id == gear.id;
        });
        check(quote != bag.end() && !quote->affordable && !quote->reason.empty(),
              "NPC正装备的法器报价显示不可取用并说明原因");
        const string before = saveToJson(g);
        check(!friendship::takeFromNpc(g, npcId, "gear", gear.id).ok,
              "报价与执行一致拒绝取走NPC正在使用的法器");
        check(saveToJson(g) == before, "拒取装备中的法器不扣交情、不转移装备");
        g.npcs[0].equipped.clear();
        const auto spare = friendship::npcBag(g, npcId);
        const auto spareQuote = std::find_if(spare.begin(), spare.end(), [&](const auto& item) {
            return item.kind == "gear" && item.id == gear.id;
        });
        check(spareQuote != spare.end() && spareQuote->affordable && spareQuote->reason.empty(),
              "NPC卸下法器后报价恢复可取用");
        check(friendship::takeFromNpc(g, npcId, "gear", gear.id).ok,
              "报价可用的闲置法器仍可支付交情真实取走");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        Job legacy; legacy.actionType = "social"; legacy.kind = "talk";
        legacy.npcId = id; legacy.total = legacy.remaining = 3;
        legacy.cost = 5; legacy.paid = true;
        // 旧档费用已付，完成时不能要求另一份 study 学费。
        g.player.stones = 0;
        const int insight = g.player.insight;
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight + 1, "旧在途论道只结算一次真实闲谈悟性");
        checkEq(g.player.npcBonds[id].talkMonth, 0, "旧论道和现代闲谈共享月内记录");
        checkEq(g.player.stones, 0, "旧已付费论道完成不二次收请教学费");
        const int relation = relationOf(g, id);
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight + 1, "重复旧social结算不能绕过闲谈月限额");
        checkEq(relationOf(g, id), relation, "重复旧social结算不再发固定好感奖励");
        check(!play::socialDo(g, id, "chat").ok, "旧入口消耗的闲谈额度同时阻止现代入口");
        g.player.stones = 20;
        check(play::socialDo(g, id, "study").ok && play::socialDo(g, id, "study").ok,
              "真实请教仍按自己的两次月限额执行");
        check(!play::socialDo(g, id, "study").ok, "旧闲谈不重置真实请教的月内计数");
        g.clock.days = 30;
        check(play::socialDo(g, id, "chat").ok, "旧论道限额到下个月真实恢复");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        g.player.arts.herbs = {{"恒心草", 2}, {"银月花", 2}};
        g.player.herbs = 4; g.npcs[0].bag.clear();
        Job legacy; legacy.actionType = "social"; legacy.kind = "gift";
        legacy.npcId = id; legacy.total = legacy.remaining = 2; legacy.gift = 3;
        const int stones = g.player.stones;
        monthComplete(g, legacy);
        checkEq(g.player.herbs, 1, "旧赠药任务扣除真实约定份数并同步玩家总量");
        checkEq(g.player.arts.herbs["恒心草"] + g.player.arts.herbs["银月花"], 1,
                "旧赠药真实扣分类药材而非仅扣旧总数");
        checkEq(g.npcs[0].bag["herb:恒心草"] + g.npcs[0].bag["herb:银月花"], 3,
                "旧赠药的每一份药材进入NPC实际背包");
        checkEq(g.player.npcBonds[id].giftsThisMonth, 1, "旧多份赠药按一次礼物记月内次数");
        check(friendship::creditOf(g, id) > 0, "旧真实赠药按实际价值产生交情");
        checkEq(g.player.stones, stones, "旧赠药完成不再次扣已支付费用");
        const int relation = relationOf(g, id), credit = friendship::creditOf(g, id);
        const auto bag = g.npcs[0].bag;
        monthComplete(g, legacy);
        checkEq(g.player.herbs, 1, "旧赠药库存不足不会扣成负数或自动补药");
        check(g.npcs[0].bag == bag && relationOf(g, id) == relation &&
              friendship::creditOf(g, id) == credit && g.player.npcBonds[id].giftsThisMonth == 1,
              "不足药材的旧赠药不先发关系、信用或月计数");
        legacy.gift = 0;
        monthComplete(g, legacy);
        checkEq(relationOf(g, id), relation, "旧赠药零份数不能白拿好感");
        legacy.gift = -1;
        monthComplete(g, legacy);
        checkEq(g.player.herbs, 1, "旧赠药负份数不能增加药材库存");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        Job legacy; legacy.actionType = "social"; legacy.kind = "talk";
        legacy.npcId = id; legacy.total = legacy.remaining = 1;
        const int insight = g.player.insight;
        g.npcs[0].location = "yunxi";
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight, "旧social不能向远方道友领取收益");
        g.npcs[0].location = g.player.location; g.npcs[0].health = 0;
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight, "旧social不能向无气血道友领取收益");
        g.npcs[0].health = 100; g.pending = Pending{};
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight, "旧social不能绕过待处理抉择领取收益");
        g.pending.reset(); legacy.paid = false;
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight, "旧social未支付的损坏安排不能领取成果");
        legacy.paid = true; legacy.kind = "unknown";
        monthComplete(g, legacy);
        checkEq(g.player.insight, insight, "未知旧social种类不能按论道偷偷结算");
        legacy.kind = "talk"; g.job = legacy;
        Game restored; string error;
        check(loadFromJson(saveToJson(g), restored, &error), "旧在途social任务仍可存档恢复");
        if (restored.job) {
            check(calendarRun(restored, 1).ok && !restored.job, "旧在途social经真实日历正常完成");
            checkEq(restored.player.insight, insight + 1, "读档旧social只结算一次月内闲谈成果");
        }
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        g.player.npcBonds[id].giftMonth = g.clock.days / 30;
        g.player.npcBonds[id].giftsThisMonth = 1000000000;
        check(friendship::giftItem(g, id, "herb", "恒心草").ok,
              "达到存档计数上限仍可送出真实物品");
        checkEq(g.player.npcBonds[id].giftsThisMonth, 1000000000,
                "赠礼月计数在存档上限饱和而不产生不可读取状态");
        check(friendship::giftHerbs(g, id, 2).ok, "旧赠药入口在计数上限仍真实转物");
        checkEq(g.player.npcBonds[id].giftsThisMonth, 1000000000,
                "旧批量赠药与即时赠礼使用相同计数上限");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        friendship::addCredit(g, id, 30);
        const auto before = saveToJson(g);
        check(!friendship::spendCredit(g, id, -1), "交情消费拒绝负数成本");
        checkStr(saveToJson(g), before, "负成本不能增加交情或降低已花交情");
        friendship::addCredit(g, id, std::numeric_limits<int>::max());
        checkEq(friendship::creditOf(g, id), 1000000000, "交情累积不会整数溢出或超过存档数值范围");
    }
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        const int before = g.player.arts.exp[g.player.arts.focus];
        const int health = g.player.health;
        check(play::socialDo(g, id, "spar").ok && g.player.arts.battleActive(),
              "切磋进入真实NPC卡牌斗法");
        check(g.player.arts.battle && g.player.arts.battle->npcId == id && !g.player.arts.battle->lethal,
              "切磋携带对方身份且非致命");
        checkEq(g.player.arts.exp[g.player.arts.focus], before, "开始切磋不提前给予随机胜负经验");
        check(g.player.health == health || !g.player.arts.battle->playerFirst,
              "切磋气血仅由真实敌方先手改变");
        battleFinish(g, true);
        check(!play::socialDo(g, id, "spar").ok, "同人本月切磋不能反复领奖");
        Game restored; string error;
        check(loadFromJson(saveToJson(g), restored, &error), "社交限额、NPC礼物装备与交情可以存档恢复");
        if (!restored.npcs.empty()) checkEq(restored.player.npcBonds[id].sparMonth, g.player.npcBonds[id].sparMonth,
                                           "读档保留切磋月限额");
    }
    std::printf("\n-- 交往与授业存档：单字段损坏拒绝 --\n");
    {
        Game g = socialGame(); const string id = g.npcs[0].id;
        check(friendship::greet(g, id).ok && play::socialDo(g, id, "study").ok,
              "通过真实见礼与请教产生有效交往记录");
        const auto valid = json::Value::parse(saveToJson(g));
        Game restored; string error;
        check(loadFromJson(valid.dump(), restored, &error), "有效交往记录存档可正常读取");
        auto badCredit = valid;
        badCredit.asObject()["player"].asObject()["npcBonds"].asObject()[id].set("credit", json::Value(-1));
        rejectSocialSave(badCredit, "负交情记录");
        auto badDiscussions = valid;
        badDiscussions.asObject()["player"].asObject()["npcBonds"].asObject()[id].set("discussCount", json::Value(3));
        rejectSocialSave(badDiscussions, "超过两次上限的讨论记录");
    }
    {
        Game g = socialGame(); Npc& teacher = g.npcs[0]; const string id = teacher.id;
        g.player.stage = g.player.highestStage = 3;
        g.player.lifespan = realmOf(3).life + g.player.lifeBonus;
        teacher.stage = 3; teacher.lifespan = realmOf(3).life;
        teacher.sect = "jinhong"; teacher.dao = "金";
        g.player.relations[id] = 100; friendship::addCredit(g, id, 1000);
        const auto lessons = npcLessons(g, id);
        const auto lesson = std::find_if(lessons.begin(), lessons.end(), [](const auto& item) {
            return item.enabled && !item.learned && item.kind == "spell";
        });
        check(lesson != lessons.end(), "授业损坏回归使用真实可学神通");
        if (lesson != lessons.end()) {
            check(npcLearnBegin(g, id, lesson->kind, lesson->id).empty() && g.job,
                  "授业损坏回归从真实付费开工开始");
            if (g.job) {
                const auto valid = json::Value::parse(saveToJson(g));
                Game restored; string error;
                check(loadFromJson(valid.dump(), restored, &error), "真实已付费授业在途存档可读");
                auto fakePlan = valid;
                fakePlan.asObject()["job"].set("plan", json::Value(string("missing-spell")));
                rejectSocialSave(fakePlan, "伪造授业内容");
                auto fakeCost = valid;
                fakeCost.asObject()["job"].set("cost", json::Value(g.job->cost + 1));
                rejectSocialSave(fakeCost, "篡改授业费用");
            }
        }
    }
}
} // namespace nztest
