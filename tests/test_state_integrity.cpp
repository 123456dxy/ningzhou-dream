// 生命周期与存档回归：真正的日数、突破消耗前参数和读档完整性。
#include "harness.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <chrono>

#include "core/battle.h"
#include "core/calendar.h"
#include "core/cultivate.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {
namespace {

Game smallGame() {
    CreateOptions opts;
    opts.seed = 734;
    Game g = createGame(opts);
    g.pending.reset();
    g.npcs.clear();
    return g;
}

Job planJob(const Game& g, const string& plan, int days) {
    Job j;
    j.actionType = "plan"; j.plan = plan; j.total = days;
    j.remaining = days; j.started = g.clock.days;
    return j;
}

} // namespace

void runStateIntegrityTests() {
    std::printf("\n-- 生命周期与存档完整性回归 --\n");
    {
        Game split = smallGame();
        split.player.stones = 1000; split.player.stoneSupply = 1;
        Game whole = split;
        const int before = split.player.stones;
        for (int i = 0; i < 30; ++i) monthComplete(split, planJob(split, "cultivate", 1));
        monthComplete(whole, planJob(whole, "cultivate", 30));
        checkEq(split.player.stones, whole.player.stones, "拆分每日闭关不会少付配给账单");
        check(split.player.stones < before, "连续一天闭关仍支付配给费用");
        Game empty = smallGame(); empty.player.stones = 0; empty.player.stoneSupply = 1;
        Game none = empty; none.player.stoneSupply = 0;
        monthComplete(empty, planJob(empty, "cultivate", 30));
        monthComplete(none, planJob(none, "cultivate", 30));
        checkEq(empty.player.xp, none.player.xp, "没有灵石时不会免费获得配给加成");
        Game worker = smallGame();
        worker.player.apt = APT_MAX;
        worker.player.insight = INSIGHT_MAX;
        for (int i = 0; i < 60; ++i) monthComplete(worker, planJob(worker, "work", 1));
        check(worker.player.xp > 0, "短时营生的修为零头也会累计");
    }
    {
        for (const string& action : {"work", "gather", "rest", "explore", "travel", "social"}) {
            Game none = smallGame();
            none.player.apt = APT_MAX; none.player.insight = INSIGHT_MAX;
            none.player.stones = 1000;
            none.player.stoneSupply = 0; none.player.qiSupply = 0;
            none.player.cave = action == "travel" ? "sea" : none.player.location;
            Npc n; n.id = "supply-peer"; n.name = "同道";
            n.location = none.player.location; n.alive = true;
            none.npcs.push_back(n);
            Game supplied = none;
            supplied.player.stoneSupply = 3; supplied.player.qiSupply = 3;
            Job j = planJob(none, action, 30);
            if (action == "travel") {
                j.actionType = "travel"; j.destination = "sea";
            } else if (action == "social") {
                j.actionType = "social"; j.kind = "talk"; j.npcId = n.id;
            }
            monthComplete(none, j);
            monthComplete(supplied, j);
            checkEq(supplied.player.xp, none.player.xp, action + "的零散修为不受配给影响");
            checkNear(supplied.clock.carry["xp"], none.clock.carry["xp"],
                      action + "的修为零头不受配给影响");
            checkEq(supplied.player.stones, none.player.stones,
                    action + "不消耗闭关配给费用");
            checkNear(supplied.clock.carry["supplyCost"], 0,
                      action + "不会积累未使用的配给账单");
        }
    }
    {
        Game g = smallGame();
        g.job = planJob(g, "rest", 60);
        const auto r = calendarRun(g, 1);
        check(r.ok, "长工作可只推进一天");
        checkEq(r.days, 1, "返回实际推进一天");
        checkEq(g.clock.days, 1, "一天按钮不会推进整个月");
        checkEq(g.job->remaining, 59, "长工作正确保留剩余天数");
        Pending p; p.type = "valley"; p.options = {{"leave", "离开", "", false}};
        g.pending = p;
        check(!calendarRun(g).ok, "抉择未处理时不能推进");
        checkEq(g.clock.days, 1, "待处理抉择保持时钟");
        g.pending.reset();
        g.player.arts.battle = Battle{};
        check(!calendarRun(g).ok, "斗法期间不能推进");
        checkEq(g.job->remaining, 59, "斗法期间保留工作");
        g.player.arts.battle.reset();
        g.ended = true;
        check(!calendarRun(g).ok, "结束的人生不能推进");
    }
    {
        Game g = smallGame();
        g.player.age = 99; g.player.lifespan = 100;
        g.clock.ageBase = 99; g.clock.days = 350;
        g.job = planJob(g, "rest", 30);
        const auto r = calendarRun(g);
        check(r.ended && g.ending.has_value(), "寿元耗尽立即结算");
        checkEq(r.days, 10, "寿尽只报告实际经过的十天");
        checkEq(g.clock.days, 360, "死亡后不继续流逝");
        check(!g.job.has_value(), "死亡清除未完成工作");
    }
    {
        Game g = smallGame();
        g.player.health = 50;
        g.job = planJob(g, "rest", 10);
        calendarRun(g);
        checkEq(g.player.health, 53, "十天休养按日累计额外7%月恢复，月初自然恢复尚未结算");
        g.job = planJob(g, "gather", 15);
        const int herbs = g.player.herbs;
        calendarRun(g);
        check(g.player.herbs > herbs, "不足一个月采药仍有收益");
        const int stones = g.player.stones;
        for (int i = 0; i < 10; ++i) {
            g.job = planJob(g, "work", 1);
            calendarRun(g);
        }
        check(g.player.stones >= stones + 8, "连续短时营生的小数收入不会丢失");
    }
    {
        Game g = smallGame();
        g.tide.active = true; g.tide.monthsLeft = 12;
        g.job = planJob(g, "rest", 60);
        calendarRun(g, 5);
        checkEq(g.tide.monthsLeft, 2, "五天行动后固定兽潮仍有两月以内");
        calendarRun(g, 25);
        checkEq(g.tide.monthsLeft, 1, "三十天后固定兽潮剩一月");
        calendarRun(g, 30);
        checkEq(g.tide.monthsLeft, 0, "六十天后固定兽潮结束");
    }
    {
        Game g = smallGame();
        g.clock.days = 47;
        g.clock.carry["xp"] = 0.4;
        ensureCalendar(g);
        checkEq(g.clock.days, 47, "重复初始化保留月内进度");
        checkNear(g.clock.carry["xp"], 0.4, "重复初始化保留小数收益");
    }
    {
        Game g = smallGame();
        g.player.stage = 2; g.player.xp = realmOf(2).need;
        Job j;
        j.actionType = "breakthrough"; j.total = 90;
        j.breakthroughPrepared = true; j.breakthroughMajor = true;
        j.breakthroughChance = 1; j.breakthroughFailureLoss = 0.15;
        // 辅助丹药与灵核已在行动开始时消耗，但本次概率必须保留。
        g.player.pills = 0; g.player.cores = 0; g.player.arts.breakBuff.clear();
        monthComplete(g, j);
        checkEq(g.player.stage, 3, "突破采用消耗资源前锁定的成功率");
        const int stage = g.player.stage;
        g.ended = true;
        monthComplete(g, j);
        checkEq(g.player.stage, stage, "已结束人生不再结算工作奖励");
    }
    {
        Game g = createGame(CreateOptions{});
        g.pending.reset();
        Npc& n = g.npcs[0];
        n.alive = false;
        Job j; j.actionType = "bond"; j.kind = "master"; j.npcId = n.id;
        j.total = 4;
        monthComplete(g, j);
        check(g.bonds.master.empty(), "在途拜师对象离世后不会建立师徒关系");
        n.alive = true; n.location = "sea";
        monthComplete(g, j);
        check(g.bonds.master.empty(), "在途拜师对象离开后不会建立师徒关系");
    }
    {
        Game original = createGame(CreateOptions{});
        original.pending.reset(); // 在途工作夹具从空闲状态开始。
        original.flags = {{"choiceYear", 3}, {"invitation", 1}};
        original.clock.carry["xp"] = 0.1234567890123456;
        original.player.arts.combatPrep = {{"attack", 0.1}, {"reduction", 0.12}};
        original.player.arts.combatBuff = 0.1;
        original.bonds.achievementTexts = {"药香入道"};
        original.bonds.origins = {{"sword", "师门赠剑"}};
        original.npcs[0].pillsUsed = {"5001"};
        Job j;
        j.actionType = "breakthrough"; j.total = 90; j.remaining = 62;
        j.breakthroughPrepared = true; j.breakthroughMajor = true;
        j.breakthroughChance = 0.834567891234; j.breakthroughFailureLoss = 0.15;
        original.job = j;
        Game restored;
        string err;
        check(loadFromJson(saveToJson(original), restored, &err), "完整在途工作能读回：" + err);
        checkEq(restored.flags["invitation"], 1, "读档保留已领取机缘标记");
        checkNear(restored.clock.carry["xp"], original.clock.carry["xp"], "读档保留精确小数收益", 1e-15);
        checkNear(restored.player.arts.combatPrep["reduction"], 0.12, "读档保留下一场丹药准备");
        checkNear(restored.player.arts.combatBuff, 0.1, "读档保留百分比斗法加成");
        checkEq(restored.bonds.achievementTexts.size(), 1, "读档保留成就文本");
        checkEq(restored.bonds.origins.size(), 1, "读档保留开局起源");
        checkEq(restored.npcs[0].pillsUsed.size(), 1, "读档保留同道服药记录");
        check(restored.job.has_value() && restored.job->breakthroughPrepared, "读档保留突破准备标志");
        if (restored.job) checkNear(restored.job->breakthroughChance, j.breakthroughChance, "读档保留锁定概率", 1e-15);
        checkStr(saveToJson(restored), saveToJson(original), "完整状态再次序列化稳定");
    }
    {
        Game original = smallGame();
        EnemySpec e; e.name = "测试修士"; e.stage = 0;
        battleStart(original, e);
        Battle& b = *original.player.arts.battle;
        b.intentSpell = "rain"; b.enemyDao = "水"; b.enemySpells = {"rain", "water"};
        b.enemyCd = {{"rain", 2}}; b.enemyWeaken = 1;
        b.buff = 0.1; b.reduction = 0.12;
        b.playerPoison = 7; b.playerPoisonTurns = 1;
        b.huntFee = 145; b.wildTask = "妖祸";
        b.carryMana = 2; b.manaBonus = 1; b.manaPenalty = 1;
        b.drawNext = 2; b.pendingDraw = 1; b.carryCap = 4; b.hpCostPct = 10;
        Game restored; string err;
        check(loadFromJson(saveToJson(original), restored, &err), "在途斗法完整读回：" + err);
        check(restored.player.arts.battle.has_value(), "读档不会跳过斗法");
        if (restored.player.arts.battle) {
            const Battle& rb = *restored.player.arts.battle;
            checkStr(rb.intentSpell, "rain", "敌方当前神通保真");
            checkEq(rb.enemySpells.size(), 2, "敌方神通列表保真");
            checkEq(rb.enemyCd.at("rain"), 2, "敌方冷却保真");
            checkEq(rb.playerPoison, 7, "玩家中毒状态保真");
            checkEq(rb.huntFee, 145, "战斗赏金保真");
            checkEq(rb.carryMana, 2, "蓄灵保真");
            checkEq(rb.hpCostPct, 10, "聚气代价保真");
            checkNear(rb.buff, 0.1, "伤害百分比保真");
            checkStr(saveToJson(restored), saveToJson(original), "斗法全部字段往返稳定");
        }
    }
    {
        CreateOptions opts; opts.traits = {"longlife"}; opts.traitsExplicit = true;
        Game original = createGame(opts);
        original.pending.reset();
        Game restored; string err;
        check(loadFromJson(saveToJson(original), restored, &err), "长生天赋存档可读取");
        checkEq(restored.player.lifeBonus, 15, "长生天赋额外寿元不会因派生字段被漏存");
        auto old = json::Value::parse(saveToJson(original));
        auto p = old["player"];
        p.asObject().erase("build"); p.set("lifeBonus", json::Value(0)); old.set("player", p);
        check(loadFromJson(old.dump(), restored, &err), "旧版漏寿元字段的存档可以迁移");
        checkEq(restored.player.lifeBonus, 15, "旧版额外寿元从寿命上限恢复");
        restored.player.stage = 2; restored.player.xp = realmOf(2).need;
        Job j; j.actionType = "breakthrough"; j.total = 90;
        j.breakthroughPrepared = true; j.breakthroughMajor = true; j.breakthroughChance = 1;
        monthComplete(restored, j);
        checkEq(restored.player.lifespan, 215, "读档后突破继续保留长生天赋额外寿元");
    }
    {
        Game original = smallGame();
        finish(original, "测试结局");
        original.ending->legacyRewardPoints = 4;
        original.ending->legacyEligible = true;
        original.ending->legacyReasons = {"入道有成"};
        Game restored; string err;
        check(loadFromJson(saveToJson(original), restored, &err), "结局存档可读取");
        check(restored.ending.has_value(), "读档保留结局摘要");
        if (restored.ending) {
            checkStr(restored.ending->cause, "测试结局", "结局原因保真");
            checkEq(restored.ending->legacyRewardPoints, 4, "传承奖励保真");
        }
    }
    {
        Game original = smallGame();
        auto corrupt = json::Value::parse(saveToJson(original));
        auto p = corrupt["player"];
        p.set("location", json::Value("不存在的地点")); corrupt.set("player", p);
        Game out = original; string err;
        check(!loadFromJson(corrupt.dump(), out, &err), "损坏的地点被拒绝");
        checkStr(saveToJson(out), saveToJson(original), "失败读档不改动当前人生");
        p.set("location", json::Value("wuling")); p.set("stage", json::Value(99)); corrupt.set("player", p);
        check(!loadFromJson(corrupt.dump(), out, &err), "越界境界被拒绝");
        p.set("stage", json::Value(0)); p.set("stones", json::Value(-1)); corrupt.set("player", p);
        check(!loadFromJson(corrupt.dump(), out, &err), "负数资源被拒绝");
        corrupt = json::Value::parse(saveToJson(original));
        auto clock = corrupt["clock"]; clock.set("days", json::Value(360)); corrupt.set("clock", clock);
        check(!loadFromJson(corrupt.dump(), out, &err), "时钟年份不一致被拒绝");
        corrupt = json::Value::parse(saveToJson(original));
        json::Value job(json::Object{}); job.set("actionType", json::Value("travel"));
        job.set("total", json::Value(5)); job.set("remaining", json::Value(-2)); corrupt.set("job", job);
        check(!loadFromJson(corrupt.dump(), out, &err), "损坏的在途工作被拒绝");
        check(!loadFromJson("{\"format\":\"ningzhou-dream-cpp\"}", out, &err), "只含格式标记的残缺存档被拒绝");
        corrupt = json::Value::parse(saveToJson(original));
        job.set("actionType", json::Value("brew")); job.set("remaining", json::Value(5));
        job.set("gift", json::Value(1000000000)); job.set("batch", json::Value(1));
        corrupt.set("job", job);
        check(!loadFromJson(corrupt.dump(), out, &err), "导入巨大炼丹炉数被拒绝，不能卡死结算");
        job.set("gift", json::Value(3)); corrupt.set("job", job);
        check(!loadFromJson(corrupt.dump(), out, &err), "新档炼丹gift与batch不一致被拒绝");
        auto legacyPlayer = corrupt["player"]; legacyPlayer.asObject().erase("build");
        corrupt.set("player", legacyPlayer);
        check(loadFromJson(corrupt.dump(), out, &err), "旧档默认batch字段可按实际三炉迁移");
        if (out.job) checkEq(out.job->batch, 3, "旧档迁移保留实际三炉");
        job.set("gift", json::Value(0)); corrupt.set("job", job);
        check(loadFromJson(corrupt.dump(), out, &err), "旧档缺少gift时按单炉迁移");
        if (out.job) checkEq(out.job->gift, 1, "旧档缺少gift安全恢复为单炉");
    }
    {
        string err;
        for (const char* bad : {"+1", "01", "1.", "1e", "1-2", "1e999", "\"\\uGGGG\"", "\"\\uD800\"", "\"\\uDC00\"", "\"a\nb\""}) {
            try {
                json::Value::parse(bad, &err);
                check(!err.empty(), string("严格拒绝非法 JSON：") + bad);
            } catch (...) { check(false, string("非法 JSON 不应抛异常：") + bad); }
        }
        auto emoji = json::Value::parse("\"\\uD83D\\uDE00\"", &err);
        check(err.empty(), "Unicode 代理对可以解析");
        checkStr(emoji.asString(), "😀", "Unicode 代理对转换为正确 UTF-8");
        string deep(150, '['); deep += '0'; deep += string(150, ']');
        json::Value::parse(deep, &err);
        check(!err.empty(), "过深 JSON 被安全拒绝");
        json::Value::parse("{}", &err);
        check(err.empty(), "成功解析清除旧错误");
        checkEq(json::Value(1e30).asInt(27), 27, "越界数字转整数安全返回默认值");
        checkStr(json::Value(std::numeric_limits<double>::infinity()).dump(), "null", "非有限数字不会输出非法 JSON");
        const double tiny = 1.0 / 360.0;
        checkNear(json::Value::parse(json::Value(tiny).dump()).asNumber(), tiny, "序列化小数不截断六位", 1e-17);
    }
    {
        const auto Unique = std::chrono::steady_clock::now().time_since_epoch().count();
        // 工作区临时目录可在受限测试进程中写入，且每次运行只占用自己的子目录。
        const auto base = std::filesystem::current_path() / "tmp" /
            ("ningzhou-state-integrity-test-" + std::to_string(Unique));
        const auto target = base / std::filesystem::u8path("中文存档.json");
        const auto utf8 = target.u8string();
        const string path(reinterpret_cast<const char*>(utf8.data()), utf8.size());
        Game first = smallGame(); string err;
        const bool FirstSaved = saveToFile(first, path, &err);
        check(FirstSaved, "中文路径存档写入成功：" + err);
        Game second = first; second.player.stones += 10;
        const bool Replaced = saveToFile(second, path, &err);
        check(Replaced, "原存档原子替换成功：" + err);
        Game out;
        const bool Loaded = loadFromFile(path, out, &err);
        check(Loaded, "中文路径存档读取成功：" + err);
        checkEq(out.player.stones, second.player.stones, "替换后读到新存档");
        std::error_code ec;
        std::filesystem::remove(target, ec);
        std::filesystem::remove(base, ec);
    }
}

} // namespace nztest
