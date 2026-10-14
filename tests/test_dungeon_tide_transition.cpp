#include "harness.h"
#include "core/api.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/dungeon.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/tide.h"

namespace nztest {
using namespace nz;
namespace {

// 用途：创建跨月/跨年秘境测试快照；流程：定向回归；使用：传绝对天数；返回：无玩家IO或检查点的有效游戏。
Game mfDungeonTransitionFixture(int WorldDays) {
    CreateOptions Options;
    Options.seed = 20261014;
    Options.name = "秘境时间验收";
    Options.traitsExplicit = true;
    Game Fixture = createGame(Options);
    Fixture.npcs.clear();
    Fixture.pending.reset();
    Fixture.job.reset();
    Fixture.player.arts.battle.reset();
    Fixture.checkpoint = AutoCheckpoint{};
    Fixture.clock.days = WorldDays;
    Fixture.clock.ageBase = 16;
    Fixture.year = WorldDays / 360;
    Fixture.player.age = 16 + Fixture.year;
    Fixture.player.lifespan = 1000;
    Fixture.player.stage = Fixture.player.highestStage = 5;
    Fixture.player.sect = "jinhong";
    Fixture.player.stones = 10000;
    Fixture.plan = "rest";
    Fixture.tide = BeastTide{};
    Fixture.dungeons = DungeonWorld{};
    ensureDungeons(Fixture);
    return Fixture;
}

// 用途：进入真实生成的秘境并处理第一层；流程：构造探索耗时；使用：按需结束节点战斗；返回：建立run且不推进世界时间。
bool mfEnterFirstDungeonRoom(Game& Fixture, bool FinishRoomBattle = true) {
    if (Fixture.dungeons.sites.empty()) return false;
    const DungeonSite Entrance = Fixture.dungeons.sites.front();
    Fixture.player.location = Entrance.location;
    if (!dungeonEnter(Fixture, Entrance.id).empty()) return false;
    string SelectedNode;
    for (const auto& Node : Fixture.dungeons.run->nodes) {
        if (dungeonNodeAvailable(*Fixture.dungeons.run, Node.id)) {
            SelectedNode = Node.id;
            break;
        }
    }
    if (SelectedNode.empty() || !dungeonChoose(Fixture, SelectedNode).empty()) return false;
    if (FinishRoomBattle && Fixture.player.arts.battle) {
        Fixture.player.arts.battle->enemyHp = 0;
        battleFinish(Fixture, true);
    }
    return Fixture.dungeons.run.has_value() && (!FinishRoomBattle || !Fixture.player.arts.battle);
}

// 类型：只借用本次测试会话的绑定作用域；构造绑定，析构恢复默认，不拥有游戏或文件资源。
struct DungeonSessionBinding {
    // 用途：为测试请求绑定隔离快照；流程：API回归；使用：传生命周期覆盖本作用域的Session，无返回值。
    explicit DungeonSessionBinding(api::Session& Session) { api::bindSession(&Session); }
    // 用途：恢复默认会话，避免影响后续用例；流程：测试清理，无参数或返回值。
    ~DungeonSessionBinding() { api::bindSession(nullptr); }
};

// 用途：调用真实秘境/日历请求分派；流程：接口回归；使用：传API路径和正文；返回：实际JSON响应，不触碰运行服务。
json::Value mfDungeonTransitionRequest(const string& Path, const json::Value& Body = json::Object{}) {
    http::Request Request;
    Request.method = "POST";
    Request.path = "/api/" + Path;
    Request.body = Body.dump();
    return json::Value::parse(api::handle(Request).body);
}

// 用途：确认边界快照可正常存读档；流程：跨月/年恢复验证；返回：无，验证资源和RNG不被重写。
void mfCheckDungeonTransitionRoundtrip(const Game& Fixture, const string& Label) {
    const string Snapshot = saveToJson(Fixture);
    Game Restored;
    string Error;
    check(loadFromJson(Snapshot, Restored, &Error), Label + "可读回：" + Error);
    checkStr(saveToJson(Restored), Snapshot, Label + "完整快照往返一致");
}
}

// 用途：验证秘境与兽潮的时间/动作边界及恢复出口；流程：定向和常规自测；返回：无，harness记录断言。
void mfRunDungeonTideTransitionTests() {
    std::printf("\n--- 秘境跨月、兽潮中断与继续结算 ---\n");
    {
        api::Session Session;
        Session.g = mfDungeonTransitionFixture(29);
        Session.started = true;
        DungeonSessionBinding Binding(Session);
        check(mfEnterFirstDungeonRoom(Session.g), "月末可进入真实秘境并结束第一层");
        checkEq(Session.g.clock.days, 29, "节点和秘境战斗仅累计探索耗时，尚未跨月");
        check(Session.g.dungeons.run && Session.g.dungeons.run->elapsedDays > 0, "累计真实房间耗时供离境结算");
        const string Snapshot = saveToJson(Session.g);
        json::Value AdvanceBody(json::Object{});
        AdvanceBody.set("days", json::Value(30));
        check(!mfDungeonTransitionRequest("advance", AdvanceBody)["error"].asString().empty(), "秘境期间过月请求明确拒绝");
        checkStr(saveToJson(Session.g), Snapshot, "拒绝过月不改秘境、时间或随机数");
        mfCheckDungeonTransitionRoundtrip(Session.g, "月末秘境");
        const int ElapsedDays = Session.g.dungeons.run->elapsedDays;
        check(mfDungeonTransitionRequest("dungeon/leave")["ok"].asBool(), "真实API离境成功");
        checkEq(Session.g.clock.days, 29 + ElapsedDays, "离境跨月恰好结算累计探索天数");
        check(!Session.g.dungeons.run && !Session.g.job, "普通跨月离境清除run和全部耗时安排");
        mfCheckDungeonTransitionRoundtrip(Session.g, "跨月离境");
    }
    {
        api::Session Session;
        Session.g = mfDungeonTransitionFixture(29);
        Session.started = true;
        Session.g.tide.active = true;
        tide::ensureTide(Session.g);
        DungeonSessionBinding Binding(Session);
        check(mfEnterFirstDungeonRoom(Session.g), "已有兽潮时仍可正常探索秘境");
        checkEq(Session.g.clock.days, 29, "秘境战斗不会误扣兽潮回合日历");
        checkEq(Session.g.tide.lastProcessedDay, 29, "秘境内未推进兽潮积分时钟");
        const string Snapshot = saveToJson(Session.g);
        check(!mfDungeonTransitionRequest("tide/fight")["error"].asString().empty(), "秘境期间兽潮出战请求明确拒绝");
        checkStr(saveToJson(Session.g), Snapshot, "拒绝兽潮出战不覆盖秘境战斗上下文");
        mfCheckDungeonTransitionRoundtrip(Session.g, "兽潮中的秘境");
        const int ElapsedDays = Session.g.dungeons.run->elapsedDays;
        check(mfDungeonTransitionRequest("dungeon/leave")["ok"].asBool(), "兽潮中离境不会卡死");
        checkEq(Session.g.clock.days, 29 + ElapsedDays, "离境真实推进已有兽潮相同天数");
        check(!Session.g.job && Session.g.tide.active, "已有兽潮不制造残留工作或虚假斗法");
    }
    {
        api::Session Session;
        Session.g = mfDungeonTransitionFixture(1079);
        Session.started = true;
        DungeonSessionBinding Binding(Session);
        check(mfEnterFirstDungeonRoom(Session.g), "年末真实秘境可进入并生成耗时");
        const int ElapsedDays = Session.g.dungeons.run->elapsedDays;
        check(ElapsedDays > 1, "房间累计耗时足以跨越年度兽潮开启点");
        Session.g.rng = 2000; // LCG固定种子使下次年度概率判定命中，其他结算仍走真实规则。
        const auto Response = mfDungeonTransitionRequest("dungeon/leave");
        check(Response["ok"].asBool(), "跨年离境创建兽潮后请求正常返回");
        check(Session.g.tide.active && Session.g.clock.days == 1080, "遇新兽潮在真实年界停止，不跳过战期");
        check(!Session.g.dungeons.run && Session.g.job && Session.g.job->actionType == "socialElapsed", "先结束秘境，保留尚未结算的合法耗时工作");
        check(Session.g.job && Session.g.job->remaining == ElapsedDays - 1, "兽潮中断仅扣实际已推进一天");
        checkStr(Response["targetTab"].asString(), "chronicle", "兽潮中断离境后引导至能继续安排的岁月纪事");
        check(!Response["text"].asString().empty(), "明确告知剩余探索耗时与继续推进方式");
        mfCheckDungeonTransitionRoundtrip(Session.g, "新兽潮暂停耗时");
        const string Snapshot = saveToJson(Session.g);
        check(!mfDungeonTransitionRequest("dungeon/leave")["error"].asString().empty(), "重复离境明确拒绝");
        checkStr(saveToJson(Session.g), Snapshot, "重复离境不再次发物资或额外推进时间");
        json::Value AdvanceBody(json::Object{});
        AdvanceBody.set("days", json::Value(ElapsedDays - 1));
        check(mfDungeonTransitionRequest("advance", AdvanceBody)["ok"].asBool(), "年界暂停后可通过真实推进API继续");
        check(!Session.g.job && Session.g.clock.days == 1079 + ElapsedDays, "继续只完成剩余探索耗时");
        check(Session.g.tide.active, "继续结算后仍留有兽潮参战窗口");
        mfCheckDungeonTransitionRoundtrip(Session.g, "年界暂停恢复后");
    }
    {
        api::Session Session;
        Session.g = mfDungeonTransitionFixture(1079);
        Session.started = true;
        DungeonSessionBinding Binding(Session);
        check(mfEnterFirstDungeonRoom(Session.g, false) && Session.g.player.arts.battle,
            "年末真实秘境战斗可保留并从撤离入口退出");
        if (Session.g.player.arts.battle) {
            Session.g.rng = 8; // 第一骰撤离成功，第二骰年度兽潮开启；不绕过实际动作判定。
            json::Value EscapeBody(json::Object{});
            EscapeBody.set("id", json::Value("escape"));
            const auto Response = mfDungeonTransitionRequest("battle/turn", EscapeBody);
            check(Response["ok"].asBool() && !Session.g.player.arts.battle && !Session.g.dungeons.run,
                "真实战斗撤离接回秘境结算，未残留battle/run");
            check(Session.g.tide.active && Session.g.clock.days == 1080 && Session.g.job && Session.g.job->remaining == 2,
                "撤离跨年遇新兽潮也保留两天合法耗时");
            checkStr(Response["targetTab"].asString(), "chronicle", "秘境战斗撤离后同样显示恢复页面");
            check(!Response["text"].asString().empty(), "战斗撤离提供剩余天数说明");
            mfCheckDungeonTransitionRoundtrip(Session.g, "战斗撤离兽潮暂停");
        }
    }
}
} // namespace nztest
