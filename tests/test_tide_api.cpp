#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/checkpoint.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/tide.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nztest {
using namespace nz;
namespace {
// 用途：创建不会访问玩家档案的真实游戏会话；流程：API回归；传境界，返回无事件、工作和NPC干扰的固定60天活动。
Game apiTideFixture(int stage = 9) {
    CreateOptions options; options.name="兽潮接口验收"; options.seed=20261013; options.traitsExplicit=true;
    auto game=createGame(options);
    game.npcs.clear(); game.pending.reset(); game.job.reset(); game.player.arts.battle.reset();
    game.checkpoint=AutoCheckpoint{};
    game.player.stage=game.player.highestStage=stage; game.player.lifespan=5000;
    // 入场合同测试使用所选境界的满血角色，避免炼气开局残留气血在高阶敌人先手时立即结束一世。
    game.player.health=mfPlayerHealthMaximum(game.player);
    game.player.location="wuling"; game.player.stones=100000;
    game.tide=BeastTide{}; game.tide.active=true;
    tide::ensureTide(game);
    return game;
}

// 用途：生成有真实报价和有限库存的已结榜活动；流程：交易API回归；无参数，返回药材与器材各8份的隔离游戏。
Game apiMarketFixture() {
    auto game=apiTideFixture();
    game.tide.playerJoined=true; game.tide.playerScore=40; game.tide.playerEntryStage=9;
    game.clock.days=game.tide.endDay;
    tide::dayTick(game);
    game.tide.marketStock={{"herb","6201",8},{"material","gold",8}};
    return game;
}

// 用途：调用真实C++请求分派；流程：会话接口测试；传路由、JSON与HTTP方法，返回实际状态码和响应正文。
http::Response tideRequest(const string& path, const json::Value& body=json::Object{}, const string& method="POST") {
    http::Request request{};
    request.method=method;
    request.path="/api/"+path;
    request.body=body.dump();
    return api::handle(request);
}

// 用途：组织固定战场请求；流程：入场参数测试；传原始JSON值，返回请求对象，保留非法类型以测试边界。
json::Value battlefieldBody(const json::Value& value) {
    json::Value body(json::Object{}); body.set("battlefield",value); return body;
}

// 用途：组织本期交易请求；流程：报价和交易API回归；传游戏与商品，返回携带赛事来源、数量和正常权威报价的请求。
json::Value purchaseBody(const Game& game, const string& kind="material", const string& id="gold", int count=1) {
    json::Value body(json::Object{});
    body.set("eventId",json::Value(game.tide.eventId)); body.set("kind",json::Value(kind)); body.set("id",json::Value(id));
    body.set("count",json::Value(count));
    for (const auto& item:tide::marketItems(game))
        if (item.kind==kind && item.id==id) body.set("expectedUnitPrice",json::Value(item.unitPrice));
    return body;
}

// 用途：确认非法请求原子拒绝；流程：全部失败场景；传会话、请求与名称，返回无，记录拒绝和完整状态保持两项断言。
void rejectTideRequest(api::Session& session, const string& path, const json::Value& body,
                       const string& label, const string& method="POST") {
    const auto before=saveToJson(session.g);
    const auto response=tideRequest(path,body,method);
    const auto result=json::Value::parse(response.body);
    check(response.status>=400 && !result["error"].asString().empty(),label+"明确拒绝");
    checkStr(saveToJson(session.g),before,label+"不改变余额、库存、背包、时间或随机数");
}

// 用途：自动释放测试会话绑定；流程：API回归退出；无参数、无返回值，后续测试恢复默认会话。
struct SessionBinding {
    explicit SessionBinding(api::Session& session) { api::bindSession(&session); }
    ~SessionBinding() { api::bindSession(nullptr); }
};
}

// 用途：验证真实C++兽潮HTTP合同；流程：自测入口；根入口提供隔离存档目录，返回无，通过harness输出检查结果。
void runTideApiTests() {
    std::printf("\n--- 兽潮真实 API、只读语义与原子交易 ---\n");
    api::Session session; session.started=true; session.g=apiTideFixture();
    SessionBinding binding(session);
    {
        const auto before=saveToJson(session.g);
        const auto rng=session.g.rng;
        const auto stateResponse=tideRequest("state",json::Object{},"GET");
        const auto tideResponse=tideRequest("tide",json::Object{},"GET");
        const auto state=json::Value::parse(stateResponse.body), detail=json::Value::parse(tideResponse.body);
        checkEq(tideResponse.status,200,"兽潮状态允许GET查询");
        checkStr(detail["tide"].dump(),state["state"]["tide"].dump(),"独立兽潮接口与完整state.tide一致");
        checkStr(detail["tide"].dump(),detail["state"]["tide"].dump(),"兽潮响应的顶级状态和完整快照一致");
        checkStr(json::Value::parse(tideRequest("tide",json::Object{},"GET").body)["tide"].dump(),detail["tide"].dump(),"重复查询不重掷NPC战绩");
        checkStr(saveToJson(session.g),before,"普通状态查询不修改任何游戏事实");
        checkEq(session.g.rng,rng,"纯查询不消耗随机数");

        // 陈旧十年档专门暴露只读路由误用okWithState造成的自动存档写入。
        checkpointEnable(session.g);
        session.g.clock.days=3600; session.g.year=10;
        session.g.player.age=session.g.clock.ageBase+session.g.year;
        const auto stale=saveToJson(session.g);
        tideRequest("tide",json::Object{},"GET");
        checkStr(saveToJson(session.g),stale,"GET兽潮不更新陈旧十年自动档或快照");
        check(session.lastAutomatic.empty(),"GET兽潮不触发自动档落盘记录");
    }
    {
        // 长安排跨年遇到新兽潮时必须把控制权交还玩家，不能直接越过整段战期。
        session.g=apiTideFixture();
        session.g.clock.days=1079; session.g.year=2;
        session.g.player.age=session.g.clock.ageBase+session.g.year;
        session.g.tide=BeastTide{}; session.g.rng=2000;
        Job work;
        work.actionType="plan"; work.plan="rest"; work.total=work.remaining=100;
        work.started=1079; work.paid=true;
        session.g.job=work;
        json::Value body(json::Object{});
        body.set("days",json::Value(360)); body.set("autoStop",json::Value(true));
        const auto response=tideRequest("advance",body);
        const auto result=json::Value::parse(response.body);
        checkEq(response.status,200,"长推进遇新兽潮的请求正常响应");
        checkEq(session.g.clock.days,1080,"自动暂停只推进跨年当天，不跳过六十天兽潮");
        check(session.g.tide.active && tide::daysLeft(session.g)==60,"暂停时新兽潮仍有完整六十天战期");
        check(session.g.job && session.g.job->remaining==99,"兽潮暂停保留未完成安排及其九十九天余量");
        checkStr(result["report"]["stopReason"].asString(),"event","自动暂停报告明确按世界事件停止");
    }
    for (int battlefield=0;battlefield<4;++battlefield) {
        session.g=apiTideFixture();
        const auto response=tideRequest("tide/fight",battlefieldBody(json::Value(battlefield)));
        checkEq(response.status,200,"合法战场"+std::to_string(battlefield)+"能通过真实接口入场");
        check(session.g.player.arts.battle && session.g.player.arts.battle->kind=="tide" &&
              session.g.player.arts.battle->enemyBeast &&
              session.g.player.arts.battle->tideBattlefield==battlefield &&
              session.g.player.arts.battle->enemyStage>=battlefield*3 &&
              session.g.player.arts.battle->enemyStage<=battlefield*3+2,"入场生成所选境界的真实妖兽和来源账本");
    }
    session.g=apiTideFixture(0);
    for (int battlefield=1;battlefield<4;++battlefield)
        rejectTideRequest(session,"tide/fight",battlefieldBody(json::Value(battlefield)),"炼气角色不能进入高阶战场");
    for (const auto& invalid:{json::Value(-1),json::Value(4),json::Value(1.5),json::Value("1"),json::Value(true)})
        rejectTideRequest(session,"tide/fight",battlefieldBody(invalid),"非法战场编号或类型");
    session.g=apiTideFixture();
    checkEq(tideRequest("tide/fight").status,200,"旧版省略battlefield请求仍可入场");
    check(session.g.player.arts.battle && session.g.player.arts.battle->tideBattlefield==3,"默认选择角色最高合法元婴战场");
    rejectTideRequest(session,"tide/fight",battlefieldBody(json::Value(0)),"已有斗法时重复入场");
    session.g=apiTideFixture(); session.g.pending=Pending{};
    rejectTideRequest(session,"tide/fight",battlefieldBody(json::Value(0)),"未处理事件时入场");
    session.g=apiTideFixture(); session.g.job=Job{};
    rejectTideRequest(session,"tide/fight",battlefieldBody(json::Value(0)),"已有安排时入场");
    session.g=apiTideFixture();
    rejectTideRequest(session,"tide/fight",battlefieldBody(json::Value(0)),"迎战GET方法", "GET");
    checkEq(tideRequest("tide/fight",battlefieldBody(json::Value(0)),"GET").status,405,"迎战GET返回405");

    session.g=apiMarketFixture();
    const auto base=purchaseBody(session.g);
    auto wrong=base; wrong.set("eventId",json::Value("other-event"));
    rejectTideRequest(session,"tide/market/buy",wrong,"错误赛事来源");
    wrong=base; wrong.set("eventId",json::Value(""));
    rejectTideRequest(session,"tide/market/buy",wrong,"缺少赛事来源");
    wrong=base; wrong.set("expectedUnitPrice",json::Value(base["expectedUnitPrice"].asInt()+1));
    rejectTideRequest(session,"tide/market/buy",wrong,"过期或伪造商品报价");
    for (const auto& field:{"count","expectedUnitPrice"}) {
        for (const auto& invalid:{json::Value(-1),json::Value(1.5),json::Value("2"),json::Value(true)}) {
            wrong=base; wrong.set(field,invalid);
            rejectTideRequest(session,"tide/market/buy",wrong,string("非法购买参数 ")+field);
        }
    }
    wrong=base; wrong.set("count",json::Value(0));
    rejectTideRequest(session,"tide/market/buy",wrong,"零份材料交易");
    wrong=base; wrong.set("count",json::Value(9));
    rejectTideRequest(session,"tide/market/buy",wrong,"购买超过本期库存");
    session.g.player.stones=0;
    rejectTideRequest(session,"tide/market/buy",base,"余额不足交易");
    session.g=apiMarketFixture(); session.g.clock.days=session.g.tide.marketExpiresDay;
    rejectTideRequest(session,"tide/market/buy",purchaseBody(session.g),"兽潮坊市过期交易");
    session.g=apiMarketFixture();
    rejectTideRequest(session,"tide/market/buy",purchaseBody(session.g),"兽潮购买GET方法","GET");
    checkEq(tideRequest("tide/market/buy",purchaseBody(session.g),"GET").status,405,"兽潮购买GET返回405");

    for (const auto& kind:{string("material"),string("herb")}) {
        session.g=apiMarketFixture();
        const string id=kind=="herb"?"6201":"gold";
        const auto request=purchaseBody(session.g,kind,id,2);
        const int beforeStones=session.g.player.stones, beforeHerbs=session.g.player.herbs;
        const auto beforeClock=session.g.clock.days;
        const auto beforeRng=session.g.rng;
        const auto response=tideRequest("tide/market/buy",request);
        const auto result=json::Value::parse(response.body);
        checkEq(response.status,200,"真实接口成功购买"+kind);
        checkEq(beforeStones-session.g.player.stones,request["expectedUnitPrice"].asInt()*2,"成交扣除两份权威报价而非客户端任意金额");
        checkEq(result["cost"].asInt(),beforeStones-session.g.player.stones,"响应金额等于真实扣款");
        const auto inventory=tide::marketItems(session.g);
        const auto item=std::find_if(inventory.begin(),inventory.end(),[&](const auto& row){return row.kind==kind && row.id==id;});
        check(item!=inventory.end() && item->stock==6 && item->owned==2,"响应后的有限库存和行囊数量真实更新");
        checkEq(session.g.clock.days,beforeClock,"即时交易不消耗日历");
        checkEq(session.g.rng,beforeRng,"即时交易不重新模拟战绩");
        if (kind=="herb") {
            const auto* herb=findHerbById(id);
            check(herb && session.g.player.arts.herbs[herb->name]==2,"购买药材使用实际名称库存键");
            checkEq(session.g.player.herbs,beforeHerbs+2,"购买药材同步总药材数量");
        } else checkEq(session.g.player.arts.materials[id],2,"购买器材使用稳定ID库存键");
    }
}
} // namespace nztest
