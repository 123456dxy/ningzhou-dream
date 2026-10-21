// 宗门与俸禄自测：对照 JS 版 socRank / socStipend / socAccrue。
//
// 期望值来自 tools/society-check.mjs（JS 版实际运行结果）。
#include <algorithm>

#include "harness.h"

#include "core/arts.h"
#include "core/model.h"
#include "core/sim.h"
#include "core/society.h"
#include "data/reference_tables.h"

using namespace nz;

namespace nztest {

namespace {

Game makeSocietyGame(const string& sect, int stage, int days) {
    Game g;
    g.year = days / 360;
    g.clock.days = days;
    g.clock.ageBase = 16;
    g.player.stage = stage;
    g.player.highestStage = stage;
    g.player.sect = sect;
    // 纯模块夹具显式准备宗门府库；报价不得通过map下标偷偷补世界。
    if (!sect.empty()) g.world.sects.try_emplace(sect);
    g.player.location = sect.empty() ? "wuling" : sect;
    g.player.traits = {"sword"};
    g.player.build.budget = 10;
    ensureArts(g);
    ensureSociety(g);
    return g;
}

} // namespace

void runSocietyTests() {
    std::printf("\n-- 宗门身份与俸禄（对照 JS 版 society.mjs）--\n");

    {
        // 身份随大境界变化
        struct Case { int stage; const char* rank; int stipend; };
        const Case cases[7] = {
            {0, "外门弟子", 8},   {2, "外门弟子", 8},
            {3, "内门弟子", 16},  {5, "内门弟子", 16},
            {6, "亲传执事", 28},  {9, "长老", 45},
            {12, "太上长老", 65},
        };
        for (const auto& c : cases) {
            Person p;
            p.stage = c.stage;
            checkStr(socRank(p), c.rank,
                     "境界 " + std::to_string(c.stage) + " 身份为" + c.rank);
            checkEq(socStipend(p), c.stipend,
                    "境界 " + std::to_string(c.stage) + " 月俸 " +
                        std::to_string(c.stipend));
        }
    }

    {
        // 第 0 月拜入，记账当月即为 8
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        checkEq(g.bonds.payroll.balance, 8, "记账当月俸禄 8");
        checkEq(g.bonds.payroll.credited, 1, "已记账 1 个月");
    }
    {
        // 推进到第 5 月：炼气初期 8/月 × 6 = 48
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 48, "第 5 月累计俸禄 48");
        checkEq(g.bonds.payroll.credited, 6, "已记账 6 个月");
        checkEq(g.bonds.payroll.lastMonth, 5, "记账月份推进到 5");
    }
    {
        // 散修不记俸禄
        Game g = makeSocietyGame("", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 0, "散修没有俸禄");
    }
    {
        // 恶名 40 以上暂停发放，但月份仍然推进
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.bonds.notoriety = 50;
        g.clock.days = 150;
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 8, "恶名 50 时俸禄停在 8");
        checkEq(g.bonds.payroll.lastMonth, 5, "月份仍推进到 5（不补发）");
    }
    {
        // 筑基初期 16/月 × 4 = 64
        Game g = makeSocietyGame("jinhong", 3, 0);
        socInitPayroll(g);
        g.clock.days = 90;
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 64, "筑基初期第 3 月累计 64");
    }
    {
        // 恶名 39 仍可领取；40 开始暂停（对照 tools/notoriety-check.mjs）
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.bonds.notoriety = 39;
        g.clock.days = 60;
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 24, "恶名 39 仍可领取（3 月 24）");

        Game g2 = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g2);
        g2.bonds.notoriety = 40;
        g2.clock.days = 60;
        socAccrue(g2);
        checkEq(g2.bonds.payroll.balance, 8, "恶名 40 起暂停发放（停在 8）");
    }
    {
        // 换宗门会重建账簿
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        const int before = g.bonds.payroll.balance;
        g.player.sect = "zhushan";
        socAccrue(g);   // 检测到宗门变化 -> 重建
        check(before == 48, "换宗前累计 48");
        checkEq(g.bonds.payroll.balance, 8, "换宗后账簿重建为当月 8");
    }

    std::printf("\n-- 人际关系初始化 --\n");
    {
        Game g = makeSocietyGame("jinhong", 0, 0);
        check(g.bonds.master.empty(), "初始没有师尊");
        check(g.bonds.disciples.empty(), "初始没有弟子");
        check(g.bonds.partner.empty(), "初始没有道侣");
        checkEq(g.bonds.notoriety, 0, "初始恶名为 0");
        checkEq(g.bonds.stipend, -1, "初始未领过俸禄");
        check(!g.bonds.origins.empty(), "天赋起源已记录");
    }

    std::printf("\n-- 领取俸禄（Demo06 累计俸禄）--\n");
    {
        // 回山门可以一次领清
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;      // 累计到第 5 月
        socAccrue(g);
        checkEq(g.bonds.payroll.balance, 48, "累计待领 48");
        const int before = g.player.stones;
        const StipendResult r = socCollectStipend(g);
        check(r.ok, "可以领取俸禄");
        checkEq(r.paid, 48, "一次领清 48");
        checkEq(r.owed, 0, "领清后不欠");
        checkEq(g.player.stones, before + 48, "灵石增加 48");
        checkEq(g.bonds.payroll.balance, 0, "账簿余额清零");
    }
    {
        // 同月不能重复领
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        socCollectStipend(g);
        const StipendResult r2 = socCollectStipend(g);
        check(!r2.ok, "同月不能重复领取");
        checkStr(r2.error, "本月已经领过俸禄。", "给出重复领取提示");
    }
    {
        // 异地同样可以领取，到账仍从真实宗门府库扣除
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        g.player.location = "wuling";
        const StipendResult r = socCollectStipend(g);
        check(r.ok, "不在山门也能领俸禄");
        checkEq(r.paid, 48, "异地领取累计俸禄48");
    }
    {
        // 府库不足时部分支付，欠额保留
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        g.world.sects["jinhong"].treasury = 20;
        const StipendResult r = socCollectStipend(g);
        check(r.ok, "府库不足时仍可部分领取");
        checkEq(r.paid, 20, "实付 20");
        checkEq(r.owed, 28, "欠额保留 28");
        checkEq(g.bonds.payroll.balance, 28, "账簿仍记 28");
    }
    {
        // 恶名 40 以上暂停领取，旧欠不消失
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        g.bonds.notoriety = 45;
        const StipendResult r = socCollectStipend(g);
        check(!r.ok, "恶名过高暂停领取");
        checkEq(g.bonds.payroll.balance, 48, "旧欠 48 不消失");
    }
    {
        // 散修没有俸禄
        Game g = makeSocietyGame("", 0, 0);
        socInitPayroll(g);
        const StipendResult r = socCollectStipend(g);
        check(!r.ok, "散修没有俸禄");
        checkStr(r.error, "散修没有俸禄。", "给出散修提示");
    }
    {
        // 外出不会清空余额
        Game g = makeSocietyGame("jinhong", 0, 0);
        socInitPayroll(g);
        g.clock.days = 150;
        socAccrue(g);
        g.player.location = "sea";
        g.clock.days = 300;
        socAccrue(g);
        check(g.bonds.payroll.balance > 48, "外出期间俸禄继续累计");
    }

    std::printf("\n-- 原创支线（8 段，含 5 段隐藏）--\n");
    {
        const auto& stories = data::StoryDef_TABLE();
        checkEq(static_cast<long long>(stories.size()), 8, "支线共 8 段");

        int hidden = 0;
        for (const auto& s : stories)
            if (s.hidden) ++hidden;
        checkEq(hidden, 5, "其中隐藏支线 5 段");

        const data::StoryDef* letter = nullptr;
        for (const auto& s : stories)
            if (s.id == "letter") letter = &s;
        check(letter != nullptr, "能查到未拆的家书");
        if (letter) {
            checkStr(letter->name, "未拆的家书", "支线名称正确");
            checkStr(letter->location, "wuling", "地点为武陵城");
            checkEq(letter->days, 3, "耗时 3 天");
            checkEq(letter->conditionId, 1, "条件编号为 1（成年）");
        }
    }
    {
        // 未拆的家书：成年即可触发
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* letter = nullptr;
        for (const auto& s : data::StoryDef_TABLE())
            if (s.id == "letter") letter = &s;
        g.player.age = 17;
        check(!storyRequiresMet(g, *letter), "17 岁不满足条件");
        g.player.age = 18;
        check(storyRequiresMet(g, *letter), "18 岁满足条件");

        g.player.location = "yifeng";
        checkStr(storyError(g, letter), "请先前往机缘地点。",
                 "不在武陵城时给出地点提示");
        g.player.location = "wuling";
        checkStr(storyError(g, letter), "", "条件齐备时可以触发");
    }
    {
        // 石脉洗灵：筑基以上且土道经验 >= 30
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* s = nullptr;
        for (const auto& x : data::StoryDef_TABLE())
            if (x.id == "rootpool") s = &x;
        g.player.stage = 2;
        g.player.arts.exp["土"] = 100;
        check(!storyRequiresMet(g, *s), "炼气期不满足石脉洗灵");
        g.player.stage = 3;
        g.player.arts.exp["土"] = 29;
        check(!storyRequiresMet(g, *s), "土道不足 30 不满足");
        g.player.arts.exp["土"] = 30;
        check(storyRequiresMet(g, *s), "筑基+土道30 满足石脉洗灵");
    }
    {
        // 已完成的支线不能重复
        Game g = makeSocietyGame("", 0, 0);
        g.player.age = 20;
        const data::StoryDef* letter = nullptr;
        for (const auto& s : data::StoryDef_TABLE())
            if (s.id == "letter") letter = &s;
        storyComplete(g, *letter, 0);
        checkEq(static_cast<long long>(g.bonds.stories.count("letter")), 1,
                "支线已记录");
        checkStr(storyError(g, letter), "这段因缘已落笔。", "重复触发被拒绝");
    }
    {
        // 回信报平安给心境+4、福缘+2
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* letter = nullptr;
        for (const auto& s : data::StoryDef_TABLE())
            if (s.id == "letter") letter = &s;
        const int heartBefore = g.player.arts.heart;
        const int luckBefore = g.player.luck;
        storyComplete(g, *letter, 0);
        checkEq(g.player.arts.heart, heartBefore + 4, "回信报平安给心境 +4");
        checkEq(g.player.luck, luckBefore + 2, "回信报平安给福缘 +2");
    }
    {
        // 收入旧卷给悟性+2
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* letter = nullptr;
        for (const auto& s : data::StoryDef_TABLE())
            if (s.id == "letter") letter = &s;
        const int insightBefore = g.player.insight;
        storyComplete(g, *letter, 1);
        checkEq(g.player.insight, insightBefore + 2, "收入旧卷给悟性 +2");
    }
    {
        // 以身引泉给资质+3，扣除气血上限12%；此夹具上限148，四舍五入扣18。
        Game g = makeSocietyGame("", 0, 0);
        g.player.apt = 60;
        g.player.health = 80;
        const data::StoryDef* s = nullptr;
        for (const auto& x : data::StoryDef_TABLE())
            if (x.id == "rootpool") s = &x;
        storyComplete(g, *s, 0);
        checkEq(g.player.apt, 63, "以身引泉给资质 +3");
        checkEq(g.player.health, 62, "以身引泉扣上限12%即18点气血");
    }
    {
        // 引灯火入道给悟道点+2、心境+5
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* s = nullptr;
        for (const auto& x : data::StoryDef_TABLE())
            if (x.id == "seaLantern") s = &x;
        const int pointsBefore = g.player.arts.points;
        const int heartBefore = g.player.arts.heart;
        storyComplete(g, *s, 1);
        checkEq(g.player.arts.points, pointsBefore + 2, "引灯火入道给悟道点 +2");
        checkEq(g.player.arts.heart, heartBefore + 5, "引灯火入道给心境 +5");
    }
    {
        // 送剑穗归乡给重剑诀抄本与声望
        Game g = makeSocietyGame("", 0, 0);
        const data::StoryDef* s = nullptr;
        for (const auto& x : data::StoryDef_TABLE())
            if (x.id == "oldblade") s = &x;
        storyComplete(g, *s, 0);
        const bool has = std::find(g.player.arts.bookList.begin(),
                                   g.player.arts.bookList.end(),
                                   "heavy") != g.player.arts.bookList.end();
        check(has, "送剑穗归乡给重剑诀抄本");
        checkEq(g.player.renown, 3, "送剑穗归乡给声望 +3");
    }

    std::printf("\n-- 野外悬赏（3 类）--\n");
    {
        const auto& tasks = data::WildTaskDef_TABLE();
        checkEq(static_cast<long long>(tasks.size()), 3, "野外悬赏 3 类");

        const data::WildTaskDef* bandit = findWildTask("bandit");
        check(bandit != nullptr, "能查到清剿山匪");
        if (bandit) {
            checkStr(bandit->name, "清剿山匪", "悬赏名称正确");
            checkEq(bandit->days, 6, "耗时 6 天");
            checkEq(bandit->fee, 28, "赏金 28");
            checkStr(bandit->style, "体", "对手风格为体");
        }
        const data::WildTaskDef* beast = findWildTask("beast");
        if (beast) {
            checkEq(beast->fee, 32, "驱除妖兽赏金 32");
            checkStr(beast->style, "木", "妖兽风格为木");
        }
        const data::WildTaskDef* supply = findWildTask("supply");
        if (supply) {
            checkEq(supply->fee, 38, "截回货箱赏金 38");
            checkStr(supply->style, "火", "对手风格为火");
        }
        check(findWildTask("nope") == nullptr, "未知悬赏返回空");
    }


    std::printf("\n-- 关系往来的结算 --\n");
    {
        // 拜师：记录师尊并加交情
        Game g = makeSocietyGame("jinhong", 0, 0);
        Npc master;
        master.id = "npc-7";
        master.name = "试师尊";
        master.sect = "jinhong";
        master.stage = 3;
        master.alive = true;
        master.location = "jinhong";
        g.npcs.push_back(master);

        societyBondComplete(g, "master", &master);
        checkStr(g.bonds.master, "npc-7", "拜师后记录师尊 id");
        checkEq(relationOf(g, "npc-7"), 10, "拜师给师尊交情 +10");
    }
    {
        // 授业：加大道经验与心境，第二次给胎息经抄本
        Game g = makeSocietyGame("jinhong", 0, 0);
        g.bonds.master = "npc-7";
        g.bonds.lessons = 0;
        const int focusBefore = g.player.arts.exp.at(g.player.arts.focus);
        const int heartBefore = g.player.arts.heart;

        societyBondComplete(g, "lesson", nullptr);
        checkEq(g.bonds.lessons, 1, "第一次授业记 1 次");
        checkEq(g.player.arts.exp.at(g.player.arts.focus), focusBefore + 10,
                "授业给当前大道 +10 经验");
        checkEq(g.player.arts.heart, heartBefore + 1, "授业给心境 +1");

        societyBondComplete(g, "lesson", nullptr);
        checkEq(g.bonds.lessons, 2, "第二次授业记 2 次");
        // 抄本 id 用数据表里的真名：胎息经 = fetal。
        // 原先写的 "breath" 在表里不存在，等于什么都没发。
        const bool hasBreath =
            std::find(g.player.arts.bookList.begin(),
                      g.player.arts.bookList.end(), "fetal") !=
            g.player.arts.bookList.end();
        check(hasBreath, "第二次授业传授胎息经抄本");
    }
    {
        // 收徒：记录弟子
        Game g = makeSocietyGame("jinhong", 3, 0);
        Npc pupil;
        pupil.id = "npc-9";
        pupil.name = "试弟子";
        pupil.alive = true;
        g.npcs.push_back(pupil);

        check(bondCanTakeDisciple(g), "空门下可以收徒");
        societyBondComplete(g, "disciple", &pupil);
        checkEq(static_cast<long long>(g.bonds.disciples.size()), 1,
                "收徒后弟子数 1");
    }
    {
        // 门下满两人后不能再收
        Game g = makeSocietyGame("jinhong", 3, 0);
        g.bonds.disciples = {"npc-1", "npc-2"};
        check(!bondCanTakeDisciple(g), "门下满两人后不能再收");
    }
    {
        // 传道授业：提升弟子的修为与悟性，但不加资质
        Game g = makeSocietyGame("jinhong", 3, 0);
        Npc pupil;
        pupil.id = "npc-9";
        pupil.name = "试弟子";
        pupil.alive = true;
        pupil.stage = 0;
        pupil.xp = 0;
        pupil.location = g.player.location;
        pupil.insight = 10;
        pupil.apt = 50;
        g.npcs.push_back(pupil);
        g.bonds.disciples = {"npc-9"};

        societyBondComplete(g, "teach", nullptr);
        for (const auto& n : g.npcs) {
            if (n.id != "npc-9") continue;
            checkEq(n.xp, 400, "扩大修为单位后授业给弟子修为 +400");
            checkEq(n.insight, 13, "授业给弟子悟性 +3");
            checkEq(n.apt, 50, "授业不增加弟子资质");
        }
    }
    {
        // 结道侣：记录道侣并加交情
        Game g = makeSocietyGame("", 0, 0);
        Npc love;
        love.id = "npc-3";
        love.name = "试道侣";
        love.alive = true;
        g.npcs.push_back(love);

        societyBondComplete(g, "partner", &love);
        checkStr(g.bonds.partner, "npc-3", "结道侣后记录 id");
        checkEq(relationOf(g, "npc-3"), 20, "结道侣给交情 +20");
    }
    {
        // 相伴共修：加经验、心境、气血，并累计相伴天数
        Game g = makeSocietyGame("", 0, 0);
        g.bonds.partner = "npc-3";
        g.player.health = 80;
        const int focusBefore = g.player.arts.exp.at(g.player.arts.focus);

        societyBondComplete(g, "together", nullptr);
        checkEq(g.bonds.bondDays, 10, "相伴一次累计 10 天");
        checkEq(g.player.arts.exp.at(g.player.arts.focus), focusBefore + 6,
                "相伴给大道经验 +6");
        checkEq(g.player.health, 84, "相伴给气血 +4");
    }
    {
        // 解除道侣：清空并扣交情
        Game g = makeSocietyGame("", 0, 0);
        g.bonds.partner = "npc-3";
        g.bonds.bondDays = 50;
        g.player.relations["npc-3"] = 60;

        societyBondComplete(g, "part", nullptr);
        check(g.bonds.partner.empty(), "解除后道侣为空");
        checkEq(g.bonds.bondDays, 0, "解除后相伴天数清零");
        checkEq(g.player.relations.at("npc-3"), 40, "解除道侣扣交情 20");
    }
    {
        // 辞别师门：清空师尊与授业次数
        Game g = makeSocietyGame("jinhong", 0, 0);
        g.bonds.master = "npc-7";
        g.bonds.lessons = 4;
        g.player.relations["npc-7"] = 50;

        societyBondComplete(g, "dismiss", nullptr);
        check(g.bonds.master.empty(), "辞别后师尊为空");
        checkEq(g.bonds.lessons, 0, "辞别后授业次数归零");
    }
    {
        // 野外悬赏：开始时进入斗法并把赏金挂上
        Game g = makeSocietyGame("", 0, 0);
        Job j;
        j.actionType = "wild";
        j.kind = "bandit";
        j.name = "清剿山匪";
        j.fee = 28;
        j.sect = "体";
        societyWildStart(g, j);
        check(g.player.arts.battle.has_value(), "悬赏完成后进入斗法");
        if (g.player.arts.battle.has_value()) {
            checkStr(g.player.arts.battle->style, "体", "对手风格取自悬赏");
            checkEq(g.player.arts.battle->advFee, 28, "赏金挂在战斗上");
        }
    }
    {
        // 悬赏胜利：领赏金与声望
        Game g = makeSocietyGame("", 0, 0);
        Battle b;
        b.won = true;
        b.kind = "wild";
        b.wildTask = "bandit";
        b.enemy = "清剿山匪";
        b.advFee = 28;
        const int stonesBefore = g.player.stones;
        const int renownBefore = g.player.renown;
        societyWildEnd(g, b);
        checkEq(g.player.stones, stonesBefore + 28, "悬赏胜利领 28 灵石");
        checkEq(g.player.renown, renownBefore + 2, "悬赏胜利给声望 +2");
        checkEq(g.bonds.wildWins, 1, "记录野外胜场 1");
    }
    {
        // 悬赏失败不发奖
        Game g = makeSocietyGame("", 0, 0);
        Battle b;
        b.won = false;
        b.advFee = 28;
        const int stonesBefore = g.player.stones;
        societyWildEnd(g, b);
        checkEq(g.player.stones, stonesBefore, "悬赏失败不发赏金");
    }
}

} // namespace nztest
