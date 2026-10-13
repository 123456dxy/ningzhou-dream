#include "harness.h"
#include "core/api.h"
#include "core/interface/views.h"
#include "core/crafting/craft_prices.h"
#include "core/crafting/cave_garden.h"
#include "core/calendar.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
namespace {
// 用途：访问隔离会话的真实API；流程：灵田参数和事务回归；返回：HTTP响应，不使用运行中的玩家服务。
nz::http::Response mfCaveRequest(const std::string& Route, const nz::json::Value& Body = nz::json::Object{}) {
    return nz::api::handle({"POST", "/api/" + Route, Body.dump(), ""});
}
// 用途：生成灵田动作正文；流程：测试真实路由；返回：只包含稳定地块和动作参数的JSON。
nz::json::Value mfGardenInput(const std::string& PlotId, int Count = 1, int Amount = 1) {
    nz::json::Value Input(nz::json::Object{});
    Input.set("plotId", PlotId); Input.set("herbId", "6001"); Input.set("count", Count); Input.set("amount", Amount);
    return Input;
}
// 用途：确认拒绝没有半笔扣款；流程：非法输入/不在府操作；返回：无，验证整档含RNG、时间、资源不变。
void mfCheckCaveReject(const std::string& Route, const nz::json::Value& Input, const std::string& Label) {
    const auto Before = nz::saveToJson(nz::api::game());
    check(mfCaveRequest(Route, Input).status >= 400, Label + "拒绝");
    checkStr(nz::saveToJson(nz::api::game()), Before, Label + "整档不变");
}
}

// 用途：验证灵田真实HTTP分派、权威价格JSON与洞府工坊权限；流程：核心接口回归；返回：无，隔离会话结束后解除绑定。
void mfRunCaveApiTests() {
    using namespace nz;
    std::printf("\n--- 洞府灵田与定价接口 ---\n");
    api::Session Session; CreateOptions Options; Options.traitsExplicit = true;
    Session.g = createGame(Options); Session.started = true; api::bindSession(&Session);
    auto& State = Session.g;
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.player.traits = {"greenBottle"};
    State.player.cave = "wuling"; State.player.location = "wuling"; State.player.stones = 10000;
    State.player.herbs = 120; State.player.arts.herbs = {{"银月花", 120}};
    const auto BeforeQuery = saveToJson(State);
    const auto View = api::view::state(State);
    checkStr(saveToJson(State), BeforeQuery, "洞府与价格完整视图纯只读");
    check(View["cave"]["access"]["atHome"].asBool(), "洞府访问条件表明已返府");
    checkEq(View["cave"]["garden"]["plots"][0]["remainingCapacity"].asInt(), 99, "空格报价容量99");
    check(View["cave"]["garden"]["plots"][0]["plantEnabled"].asBool(), "播种按钮来自领域报价");
    State.player.traits.clear();
    const auto NoBottleView = api::view::state(State);
    check(!NoBottleView["cave"]["garden"]["hasGreenBottle"].asBool()
          && !NoBottleView["cave"]["garden"]["plots"][0]["deposit"]["enabled"].asBool(),
          "无小绿瓶完整视图明确禁用存入");
    mfCheckCaveReject("cave/garden/reserve/deposit", mfGardenInput("plot-1", 1, 500), "无小绿瓶存入");
    State.player.traits = {"greenBottle"};
    for (const auto& Pill : View["pillCatalog"].asArray()) {
        const auto Quote = crafting::mfQuotePotionPrice(State, Pill["id"].asString());
        checkEq(Pill["pricing"]["materialCost"].asInt(), Quote.MaterialCost, "目录原料价值权威一致");
        checkEq(Pill["pricing"]["sellPrice"].asInt(), Quote.SellPrice, "目录出售单价权威一致");
    }
    check(mfCaveRequest("cave/garden/plant", mfGardenInput("plot-1", 90)).status == 200, "真实路由播种90份");
    check(mfCaveRequest("cave/garden/plant", mfGardenInput("plot-1", 9)).status == 200, "同格真实路由补种到99");
    mfCheckCaveReject("cave/garden/plant", mfGardenInput("plot-1", 1), "第100份种苗");
    mfCheckCaveReject("cave/garden/plant", mfGardenInput("plot-8", 1), "未解锁灵田");
    auto Fractional = mfGardenInput("plot-1"); Fractional.set("amount", 1.5);
    mfCheckCaveReject("cave/garden/reserve/deposit", Fractional, "小数转账金额");
    auto Overflow = mfGardenInput("plot-1"); Overflow.set("amount", 1e30);
    mfCheckCaveReject("cave/garden/reserve/deposit", Overflow, "超范围金额");
    mfCheckCaveReject("cave/garden/reserve/deposit", mfGardenInput("plot-1", 1, -1), "负金额");
    const int StonesBefore = State.player.stones;
    check(mfCaveRequest("cave/garden/reserve/deposit", mfGardenInput("plot-1", 1, 500)).status == 200, "显式转入格内500灵石");
    checkEq(State.player.stones, StonesBefore - 500, "转入只扣指定数量");
    checkEq(State.Garden.Plots[0].ReserveStones, 500, "格内保存完整储备");
    State.player.location = "yifeng";
    mfCheckCaveReject("cave/garden/harvest", mfGardenInput("plot-1"), "外出管理灵田");
    json::Value Supply(json::Object{}); Supply.set("kind", "qi"); Supply.set("level", 1);
    mfCheckCaveReject("supply", Supply, "外出改配给");
    json::Value Training(json::Object{}); Training.set("type", "arena");
    mfCheckCaveReject("eco", Training, "外出使用洞府训练");
    const int AwayStones = State.player.stones;
    State.plan = "rest";
    json::Value Advance(json::Object{}); Advance.set("days", 1);
    check(mfCaveRequest("advance", Advance).status == 200, "真实推进接口安排休养并推进一天");
    checkEq(State.Garden.Plots[0].ReserveStones, 401, "真实世界日在外扣本格99灵石");
    checkEq(State.player.stones, AwayStones, "后台加速不暗扣行囊");
    State.job.reset(); // 下一断言使用空闲返府夹具，不把尚余29天的休养误当可赶路状态。
    const auto Away = api::view::state(State);
    check(Away["cave"]["access"]["canReturn"].asBool(), "外出可沿地图真实返府");
    checkStr(Away["cave"]["access"]["returnMode"].asString(), "walk", "陆上默认免费步行返府");
    State.player.cave = "sea"; State.player.location = "wuling";
    const auto SeaHome = api::view::state(State);
    check(SeaHome["cave"]["access"]["canReturn"].asBool(), "海上洞府有合法返府方案");
    checkStr(SeaHome["cave"]["access"]["returnMode"].asString(), "boat", "炼气返海府选合法灵舟不错误步行");
    State.player.location = "qingshi"; State.player.cave = "qingshi";
    const auto WildHome = api::view::state(State);
    check(WildHome["workshopAvailable"].asBool() && !WildHome["inCity"].asBool(), "野外洞府工坊可用但不变成坊市");
    State.player.cave = "wuling"; State.player.location = "wuling";
    State.pending.reset(); State.job.reset();
    check(mfCaveRequest("cave/garden/reserve/withdraw", mfGardenInput("plot-1", 1, 401)).status == 200, "真实路由可完整取回储备");
    check(mfCaveRequest("cave/garden/clear", mfGardenInput("plot-1")).status == 200, "清除种苗走领域命令");
    check(State.Garden.Plots[0].Cohorts.empty(), "清理没有留下种苗");
    mfCheckCaveReject("cave/garden/harvest", mfGardenInput("plot-1"), "空格重复收获");
    api::bindSession(nullptr);
}
} // namespace nztest
