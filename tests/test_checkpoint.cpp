#include "harness.h"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/api.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/checkpoint.h"
#include "core/dungeon.h"
#include "core/json.h"
#include "core/legacy.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
using namespace nz;
namespace {
Game checkpointGame(int days = 0) {
    Game g = createGame(CreateOptions{});
    g.clock.days = days;
    g.year = days / 360;
    g.player.age = g.clock.ageBase + g.year;
    g.player.lifespan = 3000;
    g.player.health = 100;
    g.pending.reset(); g.job.reset(); g.player.arts.battle.reset();
    ensureDungeons(g);
    return g;
}
void setCheckpointDay(Game& g, int days) {
    g.clock.days = days; g.year = days / 360; g.player.age = g.clock.ageBase + g.year;
}
Job restJob(const Game& g, int days) {
    Job job; job.actionType = "plan"; job.plan = "rest"; job.name = "恢复入口回归";
    job.started = g.clock.days; job.total = job.remaining = days; return job;
}
void checkRestoredWorld(const Game& g, const Game& expected, const string& label) {
    Game current = g, baseline = expected;
    current.checkpoint = AutoCheckpoint{}; baseline.checkpoint = AutoCheckpoint{};
    // 恢复记录是唯一允许新增的世界差异，其他进度必须全部还原。
    current.logs = baseline.logs; current.eventSeq = baseline.eventSeq;
    check(saveToJson(current) == saveToJson(baseline), label + "完整还原NPC、世界、时间、资源与战利品");
    checkEq(g.rng, expected.rng, label + "随机序列准确还原");
    checkEq(g.clock.days, expected.clock.days, label + "日历准确还原");
    check(!g.ended && !g.ending, label + "未生成终局或传承资格");
    legacy::Archive archive = legacy::create(); archive.earned = 9; archive.runs = 2;
    const string originalArchive = legacy::toJson(archive);
    check(!legacy::settle(g, archive).eligible && legacy::toJson(archive) == originalArchive,
          label + "不能通过传承结算刷奖励");
}
http::Response checkpointRequest(const string& route, const json::Value& body = json::Object{}) {
    return api::handle({"POST", "/api/" + route, body.dump(), ""});
}

string checkpointFileText(const string& path) {
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    std::ostringstream text; text << file.rdbuf(); return text.str();
}

struct CheckpointSlotBackup {
    string path = savePathFor(4), original;
    bool existed = false, valid = true;
    CheckpointSlotBackup() {
        std::error_code error;
        existed = std::filesystem::exists(std::filesystem::u8path(path), error);
        valid = !error;
        if (existed) {
            std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
            std::ostringstream text; text << file.rdbuf();
            valid = valid && file.is_open() && !file.bad(); original = text.str();
        }
    }
    ~CheckpointSlotBackup() {
        if (!valid) return;
        if (existed) {
            std::ofstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::trunc);
            file.write(original.data(), static_cast<std::streamsize>(original.size())); file.close();
            check(file.good() && checkpointFileText(path) == original, "自动档磁盘回归保留原槽位内容");
        } else {
            std::error_code error;
            std::filesystem::remove(std::filesystem::u8path(path), error);
            check(!error && !std::filesystem::exists(std::filesystem::u8path(path)),
                  "自动档磁盘回归移除测试新建的槽位，不残留角色");
        }
    }
};
}

void runCheckpointTests() {
    std::printf("\n===== 最近存档与十年自动恢复回归 =====\n");
    // 实际API响应会写自动槽位，整个测试期间都保留运行前的原始字节。
    CheckpointSlotBackup slotBackup;
    check(slotBackup.valid, "自动槽位可安全备份后进行磁盘回归");
    if (!slotBackup.valid) return;
    {
        Game g = checkpointGame();
        check(!g.checkpoint.enabled && g.checkpoint.snapshot.empty(), "纯建号不自动建立恢复点");
        const auto rng = g.rng;
        checkpointEnable(g);
        check(g.checkpoint.enabled && !g.checkpoint.snapshot.empty(), "会话入口启用基准恢复点");
        checkEq(g.rng, rng, "建立基准点不消耗游戏随机数");
        checkEq(g.checkpoint.days, 0, "基准恢复点保留开局天数");
        checkEq(g.checkpoint.slot, 4, "自动恢复点使用独立只读档位4");
        checkStr(g.checkpoint.source, "auto", "基准点标记为自动来源");
        const string base = g.checkpoint.snapshot;
        checkpointEnable(g);
        checkStr(g.checkpoint.snapshot, base, "重复启用不会覆盖当前恢复点");
        const auto saved = json::Value::parse(base);
        check(!saved["checkpoint"]["enabled"].asBool() && saved["checkpoint"]["snapshot"].asString().empty(),
              "快照内的恢复系统已重置，不会递归嵌套旧快照");
        setCheckpointDay(g, 3599);
        check(!checkpointTick(g), "第十年前一天不提前自动存档");
        setCheckpointDay(g, 3600);
        check(checkpointTick(g), "满十年当日更新自动存档");
        checkEq(g.checkpoint.lastAutoYear, 10, "十年周期按实际经过年份计数");
        checkEq(g.checkpoint.days, 3600, "十年存档的进度为第3600天");
        checkEq(g.rng, rng, "自动存档不推进随机数");
        check(!checkpointTick(g), "同一十年段不重复自动保存");
        setCheckpointDay(g, 7200);
        check(checkpointTick(g) && g.checkpoint.lastAutoYear == 20, "满二十年再次自动保存");
    }
    {
        Game g = checkpointGame(9000);
        checkpointEnable(g);
        checkEq(g.checkpoint.lastAutoYear, 20, "较晚旧档首次启用按当前十年段初始化");
        check(!checkpointTick(g), "二十五年旧档启用不会立刻回溯补存第二十年");
        setCheckpointDay(g, 10800);
        Pending p; p.type = "sectInvitation"; g.pending = p;
        check(!checkpointTick(g) && g.checkpoint.lastAutoYear == 20, "抉择期间延期保存");
        g.pending.reset();
        EnemySpec enemy; enemy.name = "安全时刻测试"; enemy.stage = g.player.stage;
        battleStart(g, enemy);
        check(!checkpointTick(g) && g.checkpoint.lastAutoYear == 20, "斗法期间延期保存");
        g.player.arts.battle.reset();
        Job j; j.actionType = "plan"; j.plan = "rest"; j.total = 1; j.remaining = 0; j.started = g.clock.days; g.job = j;
        check(!checkpointTick(g), "任务完成尚未结算时不存不可载入的零天任务");
        g.job.reset();
        check(checkpointTick(g) && g.checkpoint.lastAutoYear == 30, "延期后下一安全时刻补存第三十年");
        const int last = g.checkpoint.lastAutoYear;
        setCheckpointDay(g, 14400); g.player.health = 0;
        check(!checkpointTick(g) && g.checkpoint.lastAutoYear == last, "死亡当日不能覆盖安全恢复点");
        g.player.health = 100; g.ended = true;
        check(!checkpointTick(g), "已结束的人生不能自动覆盖恢复点");
    }
    {
        Game g = checkpointGame();
        checkpointEnable(g); setCheckpointDay(g, 4000); checkpointTick(g);
        const int lastAuto = g.checkpoint.lastAutoYear;
        setCheckpointDay(g, 4500); g.player.stones = 321; g.player.cave = "wuling"; g.player.caveTier = 1;
        const auto savedRng = g.rng;
        checkpointManual(g, 2);
        checkStr(g.checkpoint.source, "manual", "手动存档成为最近恢复来源");
        checkEq(g.checkpoint.slot, 2, "最近手动档位记录准确");
        checkEq(g.checkpoint.lastAutoYear, lastAuto, "手动保存不改变自动十年周期");
        const string snapshot = g.checkpoint.snapshot;
        checkpointManual(g, 4); checkpointManual(g, 0);
        checkStr(g.checkpoint.snapshot, snapshot, "手动无法覆盖只读自动档位或非法档位");
        setCheckpointDay(g, 4600); g.player.stones = 0; g.player.health = 0; g.ended = true;
        g.player.caveTier = 2; randomOf(g);
        check(checkpointRestore(g, "野外败北"), "败北可以还原最近的手动存档");
        checkEq(g.clock.days, 4500, "恢复到真正保存时的游戏天数");
        checkEq(g.player.stones, 321, "恢复资源没有追加免费奖励");
        checkEq(g.player.caveTier, 1, "死亡后恢复存档时的洞府档位");
        check(g.player.health == 100 && !g.ended, "恢复后角色存活且无错误终局状态");
        checkEq(g.rng, savedRng, "还原存档随机序列，恢复操作不另抽随机数");
        checkEq(g.checkpoint.restores, 1, "恢复次数增加一次");
        checkStr(g.checkpoint.lastReason, "野外败北", "记录最近恢复原因");
        checkEq(g.checkpoint.lastAutoYear, lastAuto, "恢复保留原自动存档周期");
        checkStr(g.checkpoint.snapshot, snapshot, "恢复后仍保有同一最近存档");
        g.player.health = 0; finish(g, "伤势不治");
        check(checkpointRestore(g, "再次意外死亡") && g.checkpoint.restores == 2,
              "再次意外死亡主动选择回档仍能读取同一安全恢复点");
        Game loaded; string error;
        check(loadFromJson(saveToJson(g), loaded, &error), "带自动恢复元信息的存档可以载入");
        check(loaded.checkpoint.enabled && loaded.checkpoint.snapshot == snapshot && loaded.checkpoint.restores == 2,
              "自动恢复点和恢复次数完整往返保存");
    }
    {
        Game g = checkpointGame();
        const string disabled = saveToJson(g);
        check(!checkpointRestore(g, "未启用") && saveToJson(g) == disabled, "未启用恢复时不制造角色或改变状态");
        checkpointEnable(g); g.checkpoint.snapshot = "broken json";
        const string corrupt = saveToJson(g);
        check(!checkpointRestore(g, "损坏快照") && saveToJson(g) == corrupt, "损坏恢复点失败时保留现有角色以便兜底");
        Game dead = checkpointGame(); dead.player.health = 0; dead.ended = true;
        dead.checkpoint = AutoCheckpoint{};
        g.checkpoint.snapshot = saveToJson(dead);
        check(!checkpointRestore(g, "无效终局恢复点"), "不能恢复到已死快照造成无限死亡循环");
        Game fresh = checkpointGame(); fresh.ended = true; fresh.player.health = 0;
        checkpointEnable(fresh);
        check(!fresh.checkpoint.enabled, "结束的旧档不会成为新的安全恢复基准");
        Game foreign = checkpointGame(); foreign.lifeId = g.lifeId + "-foreign";
        g.checkpoint.days = foreign.clock.days;
        g.checkpoint.snapshot = saveToJson(foreign);
        check(!checkpointRestore(g, "异世快照"), "最近存档不能来自另一人生");
        Game future = checkpointGame(30); future.lifeId = g.lifeId;
        g.checkpoint.days = future.clock.days; g.checkpoint.snapshot = saveToJson(future);
        check(!checkpointRestore(g, "未来快照"), "不能凭恢复跳到未来的游戏日程");
    }
    // 战败先进入人生结算，主动选择回档才还原进度，期间不能发奖励。
    for (const string kind : {"wild", "hunt", "tide", "ambush", "kill", "rob"}) {
        Game g = checkpointGame(120); g.player.stones = 333;
        g.player.arts.materials["gold"] = 2;
        const Game baseline = g;
        checkpointEnable(g);
        g.player.stones += 700; g.player.arts.materials["gold"] += 20;
        if (!g.npcs.empty()) { g.npcs.front().health = 0; g.npcs.front().alive = false; }
        randomOf(g);
        EnemySpec enemy; enemy.name = "战败恢复检查"; enemy.kind = kind;
        enemy.stage = g.player.stage; enemy.beast = true; enemy.huntFee = 500;
        battleStart(g, enemy);
        check(g.player.arts.battle.has_value(), kind + "斗法已建立");
        if (g.player.arts.battle) {
            g.player.arts.battle->hp = 0;
            battleFinish(g, false);
        }
        check(g.ended && g.ending && g.player.health == 0 && !g.player.arts.battle && !g.dungeons.run,
              kind + "战败通过实际battleFinish进入结算等待选择");
        checkEq(g.checkpoint.restores, 0, kind + "战败不会自动回档");
        checkEq(g.player.stones, 1033, kind + "战败结算前不会追加战斗奖励或额外扣费");
        checkEq(g.player.arts.materials["gold"], 22, kind + "战败不会发放掉落材料");
        check(checkpointRestore(g, kind + "意外死亡"), kind + "主动选择恢复最近存档");
        checkEq(g.checkpoint.restores, 1, kind + "主动选择后只恢复一次");
        checkRestoredWorld(g, baseline, kind + "战败");
    }
    {
        Game g = checkpointGame(150); const Game baseline = g; checkpointEnable(g);
        EnemySpec enemy; enemy.name = "致命切磋"; enemy.kind = "spar"; enemy.lethal = true;
        enemy.stage = g.player.stage; enemy.beast = true;
        battleStart(g, enemy); if (g.player.arts.battle) g.player.arts.battle->hp = 0;
        battleFinish(g, false);
        check(g.ended && g.ending && !g.player.arts.battle, "致命斗法先进入结算等待用户选择");
        checkEq(g.checkpoint.restores, 0, "致命斗法不会自动恢复");
        check(checkpointRestore(g, "致命斗法意外死亡"), "致命斗法可主动恢复最近存档");
        checkRestoredWorld(g, baseline, "致命斗法");
    }
    {
        Game g = checkpointGame(150); const Game baseline = g; checkpointEnable(g);
        EnemySpec enemy; enemy.name = "秘境首领"; enemy.kind = "spar"; enemy.stage = g.player.stage; enemy.beast = true;
        battleStart(g, enemy);
        if (g.player.arts.battle) { g.player.arts.battle->hp = 0; g.player.arts.battle->dungeonSite = "integration-boss"; }
        battleFinish(g, false);
        check(g.ended && g.ending && !g.player.arts.battle && !g.dungeons.run,
              "秘境首领战败进入人生结算，清理探索以便用户选择");
        checkEq(g.checkpoint.restores, 0, "秘境首领战败不自动恢复或重复发奖");
        check(checkpointRestore(g, "秘境首领意外死亡"), "秘境战败用户主动恢复后才回滚");
        checkRestoredWorld(g, baseline, "秘境首领战败");
    }
    for (const string outcome : {"spar", "win", "escape"}) {
        Game g = checkpointGame(); checkpointEnable(g);
        EnemySpec enemy; enemy.name = "不会错误回档的斗法";
        enemy.kind = outcome == "spar" ? "spar" : "wild"; enemy.stage = g.player.stage; enemy.beast = true;
        battleStart(g, enemy);
        if (g.player.arts.battle) battleFinish(g, outcome == "win", outcome == "escape");
        checkEq(g.checkpoint.restores, 0, outcome + "斗法结果不会错误回档");
        check(!g.player.arts.battle && !g.ended, outcome + "斗法正常结束");
    }
    {
        Game g = checkpointGame(210); const Game baseline = g; checkpointEnable(g);
        g.player.health = 0; g.player.stones = 1; randomOf(g);
        finish(g, "伤势不治");
        check(g.ended && g.ending && g.ending->cause == "伤势不治", "实际finish意外死亡先进入人生结算");
        checkEq(g.checkpoint.restores, 0, "实际finish死亡入口不自动回档");
        check(checkpointRestore(g, "伤势不治"), "意外死亡主动选择恢复最近存档");
        checkRestoredWorld(g, baseline, "finish伤势不治");
    }
    {
        Game g = checkpointGame(359); g.player.lifespan = g.player.age + 1;
        g.job = restJob(g, 20); checkpointEnable(g);
        const auto result = calendarRun(g, 20);
        check(result.ok && !result.restored && result.ended && result.days == 1,
              "跨年自然寿终由calendarRun返回正常人生结束");
        checkEq(g.checkpoint.restores, 0, "自然寿尽不自动回档");
        check(g.ending && g.ending->cause == "寿终正寝" && !g.job && g.clock.days == 360,
              "自然寿尽保留年界进度，结束旧安排");
        legacy::Archive archive = legacy::create();
        check(legacy::settle(g, archive).eligible && archive.runs == 1, "自然寿尽保留人生传承结算资格");
        check(!legacy::settle(g, archive).eligible && archive.runs == 1, "自然寿尽传承只能结算一次");
    }
    {
        Game g = checkpointGame(3599); g.flags["choiceYear"] = 10;
        checkpointEnable(g); g.job = restJob(g, 1);
        const auto completion = calendarRun(g, 1);
        check(completion.ok && !completion.restored && g.clock.days == 3600 && !g.job,
              "满十年那日的最后一个任务正常结算");
        checkEq(g.checkpoint.lastAutoYear, 10, "任务结算后补存十年快照");
        checkEq(g.checkpoint.days, 3600, "十年快照包含任务完成后的真实进度");
        const auto saved = json::Value::parse(g.checkpoint.snapshot);
        check(saved["job"].isNull(), "十年任务完成快照没有残留零天任务");
        Game baseline; string error;
        check(loadFromJson(g.checkpoint.snapshot, baseline, &error), "十年完成快照可真正载入");
        g.player.health = 0; g.player.stones = 0;
        const auto death = calendarRun(g, 30);
        check(death.ok && death.ended && !death.restored && !g.job, "十年任务完成后意外死亡先正常结算");
        checkEq(g.checkpoint.restores, 0, "十年任务完成后死亡不自动恢复");
        check(checkpointRestore(g, "十年任务后意外死亡"), "十年任务完成后可主动选择回档");
        checkRestoredWorld(g, baseline, "十年任务完成后死亡");
    }
    // 此部分须在 runApiTests 之后运行，以免影响其冷启动断言。
    {
        Game& g = api::game(); const Game old = g;
        g = checkpointGame(359); g.player.lifespan = g.player.age + 1;
        g.job = restJob(g, 20); checkpointEnable(g);
        json::Value request(json::Object{}); request.set("days", 20); request.set("autoStop", true);
        const auto response = checkpointRequest("advance", request);
        checkEq(response.status, 200, "实际推进接口可处理跨年自然寿终结算");
        checkEq(g.checkpoint.restores, 0, "实际推进接口寿尽不自动回档");
        check(g.ended && g.ending && g.ending->cause == "寿终正寝" && !g.job,
              "推进接口寿尽后停止，进入结算而不继续旧工作");
        const auto payload = json::Value::parse(response.body);
        checkEq(payload["report"]["daysPassed"].asInt(-1), 1, "自然寿尽报告年界实际经过的一天");
        checkStr(payload["report"]["stopReason"].asString(), "ended", "推进接口明确报告人生结束");
        const string beforeRestore = saveToJson(g);
        check(checkpointRequest("checkpoint/restore").status >= 400, "自然寿尽不提供主动回档接口");
        check(saveToJson(g) == beforeRestore, "拒绝寿尽回档保留原完整结算状态");
        legacy::Archive archive = legacy::create();
        check(legacy::settle(g, archive).eligible && archive.runs == 1, "接口自然寿尽保留传承结算资格");
        g = old;
    }
    {
        Game& g = api::game(); const Game old = g;
        g = checkpointGame(270); g.job = restJob(g, 30);
        const Game baseline = g; checkpointEnable(g);
        g.player.stones += 999; g.player.arts.materials["dragon"] += 3;
        if (!g.npcs.empty()) g.npcs.front().bag["material:gold"] += 10;
        EnemySpec enemy; enemy.name = "实际出牌死亡恢复"; enemy.kind = "wild";
        enemy.stage = g.player.stage; enemy.beast = true;
        battleStart(g, enemy);
        if (g.player.arts.battle) {
            // 毒在敌方行动前结算，保证用真实 end 行动触发战败而不依赖随机攻击。
            auto& battle = *g.player.arts.battle;
            battle.hp = 1; battle.playerPoison = 2; battle.playerPoisonTurns = 1;
            battle.poison = battle.burn = battle.poisonTurns = battle.burnTurns = 0;
        }
        json::Value turn(json::Object{}); turn.set("id", "end");
        checkEq(checkpointRequest("battle/turn", turn).status, 200, "真实结束回合接口处理野外毒伤死亡");
        check(g.ended && g.ending && !g.player.arts.battle && !g.job,
              "真实出牌意外死亡先进入人生结算界面");
        checkEq(g.checkpoint.restores, 0, "真实出牌战败不自动恢复");
        checkEq(checkpointRequest("checkpoint/restore").status, 200, "用户通过真实恢复接口选择回到最近存档");
        checkEq(g.checkpoint.restores, 1, "主动选择后真实恢复接口只回档一次");
        check(g.job && g.job->remaining == 30 && !g.player.arts.battle,
              "出牌回档后不继续结算最近存档的旧安排");
        checkRestoredWorld(g, baseline, "出牌接口死亡");
        const string alive = saveToJson(g);
        check(checkpointRequest("checkpoint/restore").status >= 400, "存活时不能调用意外死亡恢复接口");
        check(saveToJson(g) == alive, "存活时误点恢复不改变当前进度");
        g = old;
    }
    for (const int decade : {10, 20}) {
        Game& g = api::game(); const Game old = g;
        g = checkpointGame(decade * 360 - 1); g.flags["choiceYear"] = decade;
        checkpointEnable(g); g.job = restJob(g, 1);
        json::Value advance(json::Object{}); advance.set("days", 1); advance.set("autoStop", true);
        const auto response = checkpointRequest("advance", advance);
        const string label = std::to_string(decade) + "年自动档";
        checkEq(response.status, 200, label + "通过真实推进接口完成年界任务");
        checkEq(g.checkpoint.lastAutoYear, decade, label + "更新十年周期");
        checkEq(g.checkpoint.days, decade * 360, label + "保存实际游戏日期");
        check(g.checkpoint.source == "auto" && g.checkpoint.slot == 4 && !g.job,
              label + "任务完成后写入独立第四槽位");
        Game disk; string error;
        check(loadFromFile(savePathFor(4), disk, &error), label + "磁盘文件可实际读取");
        check(saveToJson(disk) == saveToJson(g), label + "磁盘内容与完整自动保存进度一致");
        const string beforeSave = saveToJson(g), fileBefore = checkpointFileText(savePathFor(4));
        json::Value automaticSlot(json::Object{}); automaticSlot.set("slot", 4);
        check(checkpointRequest("save", automaticSlot).status >= 400, label + "拒绝手动覆盖第四槽位");
        check(checkpointFileText(savePathFor(4)) == fileBefore, label + "拒绝覆盖后原文件字节不变");
        check(saveToJson(g) == beforeSave, label + "拒绝覆盖后当前进度不变");
        g.player.stones = 1; g.player.health = 27; g.clock.days++;
        if (!g.npcs.empty()) { g.npcs.front().health = 15; g.npcs.front().bag["material:gold"] += 8; }
        g.job = restJob(g, 9);
        checkEq(checkpointRequest("load", automaticSlot).status, 200, label + "可通过真实load4接口恢复");
        check(saveToJson(g) == beforeSave, label + "load4准确恢复日期、NPC、资源与已完成工作");
        g = old;
    }
}
} // namespace nztest
