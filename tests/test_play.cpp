// 坊市与 NPC 往来的自测
#include "core/play.h"
#include "core/roster.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/battle.h"
#include <algorithm>

#include "harness.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/roster.h"

using namespace nz;

namespace nztest {

void runPlayTests() {
    std::printf("\n===== 坊市与往来 =====\n");

    // ---- 好感度档位 ----
    checkStr(play::relationLabel(85), "生死之交", "85 是生死之交");
    checkStr(play::relationLabel(65), "知己", "65 是知己");
    checkStr(play::relationLabel(45), "好友", "45 是好友");
    checkStr(play::relationLabel(0), "素不相识", "0 是素不相识");
    checkStr(play::relationLabel(-30), "结怨", "-30 是结怨");

    // ---- 城池才有坊市 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";       // 武陵城
        check(ecoIsCity(g.player.location), "武陵城是城池");
        const auto list = play::marketList(g);
        check(list.size() > 10, "城池坊市有货");
        bool hasHerb = false, hasPotion = false;
        for (const auto& m : list) {
            if (m.kind == "herb") hasHerb = true;
            if (m.kind == "potion") hasPotion = true;
        }
        check(hasHerb, "坊市卖药材");
        check(hasPotion, "坊市卖丹药");

        // 野外没有坊市
        g.player.location = "fanghu";
        check(play::marketList(g).empty(), "野外没有坊市");
    }

    // ---- 买药材要扣灵石、加数量 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.stones = 1000;
        const int before = g.player.stones;
        const auto list = play::marketList(g);
        std::string herbId;
        int price = 0;
        for (const auto& m : list)
            if (m.kind == "herb") { herbId = m.id; price = m.price; break; }
        check(!herbId.empty(), "找得到一味药材");

        const std::string err = play::marketBuy(g, "herb", herbId, 3);
        checkStr(err, "", "买药材成功");
        checkEq(g.player.stones, before - price * 3, "扣了 3 份的灵石");
    }

    // ---- 灵石不够要说清差多少 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.stones = 0;
        const auto list = play::marketList(g);
        std::string herbId;
        for (const auto& m : list)
            if (m.kind == "herb") { herbId = m.id; break; }
        const std::string err = play::marketBuy(g, "herb", herbId, 1);
        check(err.find("灵石不足") != std::string::npos, "灵石不足有提示");
        check(err.find("需要") != std::string::npos, "提示里说了需要多少");
    }

    // ---- 野外买东西要给出去城池的建议 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "fanghu";
        const std::string err = play::marketBuy(g, "herb", "6001", 1);
        check(err.find("城池") != std::string::npos, "野外提示去城池");
    }

    // ---- NPC 往来 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        // 造一个同地点的人
        for (auto& n : g.npcs) {
            if (n.alive && n.location == "wuling") {
                const std::string id = n.id;
                const int before = relationOf(g, id);
                const auto r = play::socialDo(g, id, "chat");
                check(r.ok, "闲谈可以成功");
                check(r.relationDelta > 0, "闲谈加好感");
                check(relationOf(g, id) > before, "好感度确实涨了");
                break;
            }
        }
    }

    // ---- 送礼要扣药材 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.herbs = 3;
        g.player.arts.herbs = {{"恒心草", 3}};
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            const auto r = play::socialDo(g, n.id, "gift");
            check(r.ok, "送礼成功");
            checkEq(g.player.herbs, 2, "送礼扣 1 份药材");
            break;
        }
    }

    // ---- 没药材时送礼要给出具体原因 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.herbs = 0;
        g.player.arts.herbs.clear();
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            const auto acts = play::socialActions(g, n.id);
            for (const auto& a : acts) {
                if (a.id != "gift") continue;
                check(!a.enabled, "没药材时送礼不可用");
                check(a.reason.find("药材") != std::string::npos,
                      "原因里说了缺药材");
            }
            break;
        }
    }

    // ---- 好感不够时道侣未解锁，拜师入口保留并写清缺少条件 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            g.player.sect = n.sect = "jinhong";
            g.player.renown = 10;
            n.stage = g.player.stage + 1;
            g.player.relations[n.id] = 0;
            const auto acts = play::socialActions(g, n.id);
            bool hasPartner = false, hasMaster = false, masterEnabled = false;
            std::string masterReason;
            for (const auto& a : acts) {
                if (a.id == "partner") hasPartner = true;
                if (a.id == "master") {
                    hasMaster = true;
                    masterEnabled = a.enabled;
                    masterReason = a.reason;
                }
            }
            check(!hasPartner, "好感 0 时看不到「结为道侣」");
            check(hasMaster && !masterEnabled && masterReason.find("好感不足40") != std::string::npos,
                  "好感 0 时拜师入口可见但禁用，并说明好感门槛");
            break;
        }
    }

    // ---- 好感够了，「彻夜论道」会出现 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            g.player.relations[n.id] = 50;
            const auto acts = play::socialActions(g, n.id);
            bool hasDeep = false;
            for (const auto& a : acts)
                if (a.id == "deep") hasDeep = true;
            check(hasDeep, "好感 50 时解锁「彻夜论道」");
            break;
        }
    }

    // ---- 切磋使用真实NPC神通，经验在结束后结算 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.health = 100;
        g.player.arts.focus = "剑";
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            n.stage = g.player.stage; n.dao = "剑"; n.sect.clear();
            n.gear.clear(); n.equipped.clear(); n.spells = {"sword"};
            const int swordExpBefore = g.player.arts.exp.count("剑")
                                     ? g.player.arts.exp.at("剑") : 0;
            const auto r = play::socialDo(g, n.id, "spar");
            check(r.ok, "切磋可以成功");
            check(g.player.arts.battleActive() && g.player.arts.battle->npcId == n.id &&
                  !g.player.arts.battle->lethal, "切磋建立对应道友的非致命真实斗法");
            checkEq(g.player.arts.exp["剑"], swordExpBefore, "开始切磋不提前发经验");
            if (g.player.arts.battle) {
                const int before = g.player.arts.battle->hp;
                battleTurn(g, "end");
                check(g.player.arts.battle && g.player.arts.battle->hp < before,
                      "结束真实回合后敌方神通造成气血损伤");
                battleFinish(g, true);
            }
            check(g.player.arts.exp["剑"] > swordExpBefore, "切磋胜利后实际获得剑道经验");
            break;
        }
    }

    // ---- 气血太低不能切磋 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.health = 10;
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            const auto acts = play::socialActions(g, n.id);
            for (const auto& a : acts) {
                if (a.id != "spar") continue;
                check(!a.enabled, "气血低时不能切磋");
                check(a.reason.find("气血") != std::string::npos,
                      "原因里说了气血不足");
            }
            break;
        }
    }

    // ---- 结为道侣 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.age = 20;
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            if (std::abs(g.player.stage - n.stage) > 3) continue;
            n.age = 20;
            g.player.relations[n.id] = 80;
            const auto acts = play::socialActions(g, n.id);
            bool enabled = false;
            for (const auto& a : acts)
                if (a.id == "partner" && a.enabled) enabled = true;
            check(enabled, "好感 80 且条件满足时可以结道侣");
            const auto r = play::socialDo(g, n.id, "partner");
            check(r.ok, "结道侣成功");
            checkStr(g.bonds.partner, n.id, "道侣已记录");
            break;
        }
    }

    // ---- 已经结为道侣后不能再结 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.bonds.partner = "someone";
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            g.player.relations[n.id] = 90;
            const auto acts = play::socialActions(g, n.id);
            for (const auto& a : acts)
                if (a.id == "partner")
                    check(!a.enabled, "已有道侣时不能再结");
            break;
        }
    }

    // ---- 附近的人按好感排序 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        int i = 0;
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            g.player.relations[n.id] = (i == 0) ? 5 : 80;
            if (++i >= 2) break;
        }
        const auto people = play::nearbyPeople(g);
        check(people.size() >= 2, "找得到至少两个人");
        if (people.size() >= 2) {
            check(relationOf(g, people[0]->id) >= relationOf(g, people[1]->id),
                  "按好感度降序排列");
        }
    }

    // ---- 炼丹：缺料要说清缺什么 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.stones = 1000;
        g.player.arts.herbs.clear();
        g.player.herbs = 0;
        const auto opts = play::craftOptions(g, "brew");
        check(!opts.empty(), "有可选的丹方");
        int blocked = 0;
        for (const auto& o : opts) {
            if (o.enabled) continue;
            ++blocked;
            check(o.reason.find("缺药材") != std::string::npos ||
                  o.reason.find("灵石") != std::string::npos ||
                  o.reason.find("境界") != std::string::npos ||
                  o.reason.find("丹炉") != std::string::npos,
                  "不可炼时给出了具体原因");
        }
        check(blocked > 0, "缺料时确实有被挡住的丹方");
    }

    // ---- 炼丹成功要有产出 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.stones = 5000;
        g.player.alchemy = 500;          // 拉满丹术，尽量必成
        // 把第一张丹方需要的药材补足
        const auto opts = play::craftOptions(g, "brew");
        check(!opts.empty(), "有丹方可选");
        if (!opts.empty()) {
            // 直接给足所有 1-3 阶药材
            for (const auto& h : data::HerbDef_TABLE()) {
                if (h.grade <= 3) g.player.arts.herbs[h.name] = 20;
            }
            const auto r = play::craftDo(g, "brew", opts[0].id);
            check(r.ok, "炼丹可以执行");
            check(!r.text.empty(), "炼丹有结果文本");
        }
    }

    // ---- 炼丹会扣灵石与丹炉耐久 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.stones = 5000;
        for (const auto& h : data::HerbDef_TABLE())
            if (h.grade <= 3) g.player.arts.herbs[h.name] = 20;
        const auto opts = play::craftOptions(g, "brew");
        int durBefore = g.player.arts.durability;
        int stonesBefore = g.player.stones;
        const auto r = play::craftDo(g, "brew", opts[0].id);
        if (r.ok) {
            check(g.player.stones < stonesBefore, "炼丹扣灵石");
            check(g.player.arts.durability < durBefore, "炼丹磨丹炉耐久");
        }
    }

    std::printf("\n-- 通讯录与建号 --\n");

    // ---- 建号：点数校验 ----
    {
        const auto c = play::checkBuild({"sword"}, {}, 10);
        check(c.ok, "剑心通明(4点)在 10 点预算内合法");
        checkEq(c.spent, 4, "天赋消耗 4");
        checkEq(c.remain, 6, "剩余 6");
    }
    {
        // 磨砺退还点数
        const auto c = play::checkBuild({"mortal"}, {}, 10);
        check(c.ok, "凡骨求道(-3点)合法");
        checkEq(c.spent, -3, "磨砺消耗是 -3");
        checkEq(c.remain, 13, "磨砺让剩余涨到 13");
    }
    {
        // 超支要拦住
        const auto c = play::checkBuild({"root", "genius"}, {}, 10);
        check(!c.ok, "7+7 超支被拦下");
        check(c.error.find("超支") != std::string::npos, "提示里说了超支");
    }
    {
        // 数量不再设上限：四项只要点数够就该通过
        // （旧规则是「最多三项」，已按需求去掉，只留点数限制）
        const auto c = play::checkBuild({"sword", "mortal", "poor", "weak"}, {}, 10);
        check(c.ok, "四项天赋不再被数量拦下");
        checkEq(c.traitCount, 4, "记下选了 4 项");
        // sword(4) + mortal(-3) + poor(-2) + weak(-2) = -3，尚余 13
        checkEq(c.remain, 13, "磨砺类退还点数后尚余 13");
    }
    {
        // 数量放开了，但点数仍然管住
        const auto c = play::checkBuild(
            {"sword", "root", "luck", "genius", "herb"}, {}, 10);
        check(!c.ok, "数量放开后，超支仍被拦下");
        check(c.error.find("超支") != std::string::npos, "提示里说了超支");
    }
    {
        // 重复天赋要拦住
        const auto c = play::checkBuild({"sword", "sword"}, {}, 10);
        check(!c.ok, "重复选同一项被拦下");
    }
    {
        // 单项属性超上限要拦住
        std::map<std::string, int> alloc;
        alloc["apt"] = 99;
        const auto c = play::checkBuild({}, alloc, 40);
        check(!c.ok, "属性超过单项上限被拦下");
        check(c.error.find("最多") != std::string::npos, "提示里说了上限");
    }
    {
        // 属性点也算进消耗
        std::map<std::string, int> alloc;
        alloc["apt"] = 5;
        const auto c = play::checkBuild({"sword"}, alloc, 10);
        checkEq(c.allocSpent, 5, "属性消耗 5");
        checkEq(c.remain, 1, "4+5 之后剩 1");   // 10 - 4 - 5 = 1
    }

    // ---- 天赋表 ----
    {
        const auto t = play::allTalents();
        check(t.size() >= 34, "天赋全表不少于 34 项");
        // 磨砺类排在最后
        bool sawNeg = false;
        bool ok = true;
        for (const auto& x : t) {
            if (x.cost < 0) sawNeg = true;
            else if (sawNeg) ok = false;    // 负数之后不该再有正数
        }
        check(sawNeg, "表里有磨砺类天赋");
        check(ok, "磨砺类排在正成本之后");
    }

    // ---- 五项可分配属性 ----
    {
        const auto opts = play::allocOptions({});
        checkEq(static_cast<long long>(opts.size()), 5,
                "五项可分配属性");
    }

    // ---- 通讯录 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        // 默认不含陌生人：新档一个都不认识
        const auto none = play::roster(g, false, "relation");
        checkEq(static_cast<long long>(none.size()), 0,
                "新档通讯录里没有认识的人");
        // 含陌生人：应该有一堆
        const auto all = play::roster(g, true, "relation");
        check(all.size() > 100, "含陌生人时名单很长");
    }
    {
        // 聊过之后应该进通讯录
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            play::socialDo(g, n.id, "chat");
            break;
        }
        const auto known = play::roster(g, false, "relation");
        checkEq(static_cast<long long>(known.size()), 1,
                "聊过之后有 1 人进通讯录");
    }
    {
        // 排序：按交情降序
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        int i = 0;
        for (auto& n : g.npcs) {
            if (!n.alive || n.location != "wuling") continue;
            g.player.relations[n.id] = (i == 0) ? 10 : 70;
            if (++i >= 3) break;
        }
        const auto list = play::roster(g, false, "relation");
        bool sorted = true;
        for (size_t k = 1; k < list.size(); ++k)
            if (list[k - 1].relation < list[k].relation) sorted = false;
        check(sorted, "按交情排序是降序");
    }
    {
        // 传讯加好感
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        std::string id;
        for (auto& n : g.npcs) {
            if (n.alive && n.location == "wuling") { id = n.id; break; }
        }
        const int before = relationOf(g, id);
        const auto r = play::sendMessage(g, id);
        check(r.ok, "传讯成功");
        check(relationOf(g, id) > before, "传讯涨了好感");
    }
    {
        // 好感很高时传讯不再涨（避免刷）
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        std::string id;
        for (auto& n : g.npcs) {
            if (n.alive && n.location == "wuling") { id = n.id; break; }
        }
        g.player.relations[id] = 90;
        const auto r = play::sendMessage(g, id);
        check(!r.ok, "交情已深时传讯不再涨");
        check(r.error.find("当面") != std::string::npos,
              "提示建议当面相见");
    }
    {
        // 邀请：交情不足要拦住
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        std::string id;
        for (auto& n : g.npcs) {
            if (n.alive && n.location != "wuling") { id = n.id; break; }
        }
        const auto r = play::inviteToMe(g, id);
        check(!r.ok, "交情不足时邀请被拒");
        check(r.error.find("10") != std::string::npos, "提示里说了需要多少交情");
    }
    {
        // 邀请成功：对方会换地点
        Game g = createGame(CreateOptions{});
        g.player.location = "wuling";
        g.player.stones = 500;
        std::string id;
        for (auto& n : g.npcs) {
            if (n.alive && n.location != "wuling") { id = n.id; break; }
        }
        g.player.relations[id] = 30;
        const auto r = play::inviteToMe(g, id);
        check(r.ok, "交情够时邀请成功");
        for (const auto& n : g.npcs)
            if (n.id == id)
                checkStr(n.location, "wuling", "对方已来到玩家所在地");
    }

    // ---- 丹毒 ----
    {
        Game g = createGame(CreateOptions{});
        g.player.arts.poison = 0;
        const auto p = play::poisonInfo(g);
        checkEq(p.current, 0, "无丹毒");
        check(!p.affecting, "0 丹毒不影响修炼");
        checkNear(p.cultPenalty, 1.0, "无丹毒时修炼效率是 1.0");
    }
    {
        // 不超过 20 不影响
        Game g = createGame(CreateOptions{});
        g.player.arts.poison = 20;
        const auto p = play::poisonInfo(g);
        check(!p.affecting, "正好 20 还不影响");
        checkNear(p.cultPenalty, 1.0, "20 时效率仍为 1.0");
    }
    {
        // 超过 20 开始拖慢
        Game g = createGame(CreateOptions{});
        g.player.arts.poison = 60;
        const auto p = play::poisonInfo(g);
        check(p.affecting, "60 丹毒影响修炼");
        check(p.cultPenalty < 1.0, "60 时修炼效率下降");
        checkNear(p.cultPenalty, 0.75, "60 时效率 0.75（与 JS 一致）");
        check(p.daysToClear > 0, "给出了化解所需天数");
    }
    {
        // 上限：再高也压到 0.5
        Game g = createGame(CreateOptions{});
        g.player.arts.poison = 10000;
        const auto p = play::poisonInfo(g);
        checkNear(p.cultPenalty, 0.5, "丹毒极高时效率压到 0.5 下限");
    }
}

} // namespace nztest
