#include "harness.h"

#include <cmath>

#include "core/api.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/json.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/travel.h"

using namespace nz;

namespace nztest {
namespace {

Game traveller() {
    CreateOptions opts; opts.name = "赶路回归"; opts.seed = 734;
    Game g = createGame(opts);
    g.npcs.clear(); g.pending.reset(); g.job.reset();
    g.player.location = "wuling";
    g.player.stones = 1000;
    g.plan = "rest";
    return g;
}

http::Response request(const string& route, const json::Value& body = json::Object{}) {
    return api::handle({"POST", "/api/" + route, body.dump(), ""});
}

json::Value routeInput(const string& to, const string& mode = "boat", bool begin = true) {
    json::Value in(json::Object{});
    in.set("to", to); in.set("mode", mode); in.set("begin", begin);
    return in;
}

void importTraveller(const Game& g) {
    json::Value in(json::Object{}); in.set("game", saveToJson(g));
    check(request("import", in).status == 200, "导入赶路回归角色");
}

void rejectUnchanged(const json::Value& in, const string& label) {
    const string before = saveToJson(api::game());
    check(request("travel", in).status >= 400, label + "拒绝赶路");
    checkStr(saveToJson(api::game()), before, label + "不改变余额、日历、地点与任务");
}

} // namespace

void runTravelTests() {
    std::printf("\n-- 舆图赶路：距离、方式、进度与旧档 --\n");
    {
        Game g = traveller();
        bool valid = true;
        for (const auto& from : LOCATIONS()) {
            g.player.location = from.id;
            for (const auto& to : LOCATIONS()) {
                const TravelPlan plan = travelPlan(g, to.id);
                const int distance = jsRound(std::hypot(double(from.x) - to.x, double(from.y) - to.y));
                valid = valid && plan.distance == distance && plan.options.size() == 3;
                for (const auto& option : plan.options) {
                    valid = valid && option.cost >= 0 && !option.name.empty() && !option.desc.empty();
                    if (string(from.id) == to.id) valid = valid && !option.enabled && option.days == 0;
                    else valid = valid && option.days > 0;
                }
            }
        }
        check(valid, "全部地点组合使用安全欧氏距离、完整方式与正整数耗时");
        g.player.location = "wuling";
        const auto near = travelPlan(g, "heshan");
        const auto far = travelPlan(g, "qingshi");
        check(near.distance < far.distance, "目的地距离按实际坐标区分");
        check(travelOption(near, "walk")->days < travelOption(far, "walk")->days,
              "更远目的地步行耗时更长");
        check(travelOption(far, "boat")->days < travelOption(far, "walk")->days,
              "灵舟花费灵石换取更短耗时");
        check(!travelOption(far, "flight")->enabled, "炼气不能御器飞行");
        g.player.stage = 3;
        const auto foundation = travelPlan(g, "xinghe");
        check(travelOption(foundation, "flight")->enabled, "筑基解锁御器飞行");
        g.player.stage = 9;
        const auto nascent = travelPlan(g, "xinghe");
        check(travelOption(nascent, "flight")->days < travelOption(foundation, "flight")->days,
              "更高境界御器日行距离增加");
        const auto sea = travelPlan(g, "sea");
        check(!travelOption(sea, "walk")->enabled &&
              travelOption(sea, "walk")->reason.find("无尽海") != string::npos,
              "无尽海出入明确禁止步行");
        check(travelOption(sea, "boat")->enabled && travelOption(sea, "flight")->enabled,
              "灵舟和已解锁御器均可跨海");
        g.player.location = "sea";
        check(!travelOption(travelPlan(g, "wuling"), "walk")->enabled,
              "海上返回陆地同样不能步行");
    }
    {
        Game g = traveller(); g.player.stones = 0;
        const auto free = travelPlan(g, "qingshi");
        check(travelOption(free, "walk")->enabled && !travelOption(free, "boat")->enabled,
              "没钱仍可免费步行，付费方式给出不足原因");
        const string before = saveToJson(g);
        check(!travelBegin(g, "qingshi", "missing").empty(), "未知赶路方式被拒绝");
        checkStr(saveToJson(g), before, "未知方式不改变任何状态");
        check(!travelBegin(g, "qingshi", "boat").empty(), "灵石不足不能建付费赶路任务");
        checkStr(saveToJson(g), before, "付费不足不改变任何状态");
        check(travelBegin(g, "qingshi", "walk").empty(), "零灵石可建立免费步行任务");
        checkEq(g.player.stones, 0, "步行不扣灵石");
        checkEq(g.clock.days, 0, "建立赶路任务不推进日历");
        checkStr(g.player.location, "wuling", "建立赶路任务不提前改地点");
        checkStr(g.job->origin, "wuling", "赶路任务记录出发地");
        checkStr(g.job->travelMode, "walk", "赶路任务记录方式");
        const auto busy = travelPlan(g, "xinghe");
        check(std::all_of(busy.options.begin(), busy.options.end(), [](const TravelOption& o) {
            return !o.enabled && !o.reason.empty();
        }), "已有任务时所有新赶路方式均禁用");
    }
    {
        Game g = traveller(); g.clock.days = 29;
        const auto plan = travelPlan(g, "xinghe");
        check(travelBegin(g, "xinghe", "walk").empty(), "可建立跨月长途步行");
        g.job->travelAmbushes = 1; // 本组单独检查距离与跨月；真实截杀见专项回归。
        const int total = g.job->total;
        const auto oneDay = calendarRun(g, 1);
        checkEq(oneDay.days, 1, "跨月路程可逐天推进");
        checkEq(g.clock.days, 30, "途中进入下个月");
        checkEq(g.job->remaining, total - 1, "跨月仅减少实际一天行程");
        checkStr(g.player.location, "wuling", "跨月途中尚未抵达，地点保持出发地");
        Game loaded; string error;
        check(loadFromJson(saveToJson(g), loaded, &error), "途中进度可以保存读取");
        checkStr(loaded.job->origin, "wuling", "读档保留出发地");
        checkStr(loaded.job->travelMode, "walk", "读档保留步行方式");
        checkEq(loaded.job->remaining, total - 1, "读档保留剩余行程");
        while (loaded.job) calendarRun(loaded, loaded.job->remaining);
        checkStr(loaded.player.location, "xinghe", "读档继续后仅在完成时抵达");
        checkEq(loaded.clock.days, 29 + travelOption(plan, "walk")->days,
                "读档续程总天数与报价一致");
    }
    {
        Game g = traveller(); const int stones = g.player.stones;
        const auto plan = travelPlan(g, "yunxi");
        check(travelBegin(g, "yunxi", "boat").empty(), "建立灵舟赶路任务");
        const int fare = travelOption(plan, "boat")->cost;
        calendarRun(g, 1);
        const auto cancelled = shop::cancelJob(g);
        check(cancelled.ok && !g.job, "可中止未抵达的行程");
        checkStr(g.player.location, "wuling", "中止后保留出发地");
        checkEq(g.player.stones, stones - fare, "已支付船资不返还也不重复扣款");
        check(cancelled.text.find("出发地") != string::npos && cancelled.text.find("船资") != string::npos,
              "中止提示说明停留地点及船资不返还");
    }
    {
        Game g = traveller();
        Job old; old.actionType = "travel"; old.name = "赶路安顿";
        old.destination = "yunxi"; old.total = 5; old.remaining = 3; old.cost = 10;
        g.job = old;
        auto save = json::Value::parse(saveToJson(g));
        auto& job = save.asObject()["job"].asObject();
        job.erase("origin"); job.erase("travelMode");
        Game loaded; string error;
        check(loadFromJson(save.dump(), loaded, &error), "旧档赶路任务缺新字段仍可读取");
        checkStr(loaded.job->origin, "wuling", "旧档出发地恢复为当前地点");
        checkStr(loaded.job->travelMode, "boat", "旧档固定船资赶路恢复为灵舟");
        checkEq(loaded.job->total, 5, "旧档保留已支付的原耗时报价");
        checkEq(loaded.job->cost, 10, "旧档保留原船资，不重新收费");
        calendarRun(loaded, 3);
        checkStr(loaded.player.location, "yunxi", "旧档剩余路程正确完成");
        save.asObject()["job"].set("origin", "missing");
        check(!loadFromJson(save.dump(), loaded, &error), "损坏赶路出发地拒绝读档");
    }

    std::printf("\n-- 赶路API：开始、继续、中止与真实停止报告 --\n");
    {
        Game original = traveller(); importTraveller(original);
        const auto state = json::Value::parse(request("state").body)["state"];
        bool quoted = true;
        for (const auto& location : state["locations"].asArray()) {
            const auto plan = travelPlan(api::game(), location["id"].asString());
            const auto& route = location["travel"];
            quoted = quoted && route["from"].asString() == plan.from &&
                     route["to"].asString() == plan.to && route["distance"].asInt() == plan.distance;
            for (const auto& mode : route["options"].asArray()) {
                const auto* option = travelOption(plan, mode["id"].asString());
                quoted = quoted && option && mode["days"].asInt() == option->days &&
                         mode["cost"].asInt() == option->cost && mode["enabled"].asBool() == option->enabled;
            }
        }
        check(quoted, "地图报价逐项来自赶路规划器，与实际规则一致");
        auto invalid = routeInput("yunxi"); invalid.set("mode", 1);
        rejectUnchanged(invalid, "方式类型错误");
        invalid.set("mode", json::Value{}); rejectUnchanged(invalid, "方式null");
        invalid.set("mode", "missing"); rejectUnchanged(invalid, "未知方式");
        invalid = routeInput("yunxi"); invalid.set("begin", "true"); rejectUnchanged(invalid, "begin类型错误");
        invalid.set("begin", json::Value{}); rejectUnchanged(invalid, "begin null");
        rejectUnchanged(routeInput("sea", "walk"), "海路步行");
        rejectUnchanged(routeInput("yunxi", "flight"), "炼气御器");
        rejectUnchanged(routeInput("wuling", "walk"), "当前位置");
        rejectUnchanged(routeInput("missing", "walk"), "未知目的地");
        const auto quote = travelPlan(api::game(), "yunxi");
        const int cost = travelOption(quote, "boat")->cost;
        const auto begun = json::Value::parse(request("travel", routeInput("yunxi")).body);
        check(!begun["arrived"].asBool() && begun["report"]["stopReason"].asString() == "started",
              "只开始行程明确返回未抵达和started");
        checkEq(api::game().clock.days, 0, "begin=true没有暗中推进天数");
        checkEq(api::game().player.stones, original.player.stones - cost, "建立灵舟行程只扣实际报价一次");
        const auto& job = begun["state"]["job"];
        check(job["from"].asString() == "wuling" && job["to"].asString() == "yunxi" &&
              job["mode"].asString() == "boat" && job["elapsed"].asInt() == 0 && job["cost"].asInt() == cost,
              "在途行程下发出发地目的地方式进度和实付费用");
        rejectUnchanged(routeInput("xinghe", "walk"), "途中覆盖目的地");
        json::Value day(json::Object{}); day.set("days", 1);
        check(request("advance", day).status == 200, "途中可以推进一天");
        checkStr(api::game().player.location, "wuling", "推进一天不会提前抵达");
        checkEq(json::Value::parse(request("state").body)["state"]["job"]["elapsed"].asInt(), 1,
                "推进后行程进度同步真实天数");
        check(request("cancel-job").status == 200, "接口可以中止行程");
        checkEq(api::game().player.stones, original.player.stones - cost, "中止接口不返船资");
    }
    {
        Game g = traveller(); importTraveller(g);
        json::Value to(json::Object{}); to.set("to", "yunxi");
        const auto quote = travelPlan(g, "yunxi");
        const auto arrived = json::Value::parse(request("travel", to).body);
        check(arrived["arrived"].asBool() && arrived["report"]["stopReason"].asString() == "done",
              "旧API缺省mode与begin仍乘灵舟并返回实际抵达");
        checkEq(api::game().clock.days, travelOption(quote, "boat")->days, "自动赶路推进报价天数");
        check(!api::game().job, "自动赶路完成不偷偷追加修炼任务");
        checkStr(api::game().player.location, "yunxi", "自动赶路结束才修改地点");
    }
    {
        Game g = traveller(); g.clock.days = 359; g.player.lifespan = g.player.age + 1;
        importTraveller(g);
        const Game baseline = api::game();
        const auto quote = travelPlan(baseline, "xinghe");
        check(travelOption(quote, "boat")->cost > 0 && travelOption(quote, "boat")->days > 1,
              "途中寿尽覆盖付费且尚未完成的整段旅程");
        const auto ended = json::Value::parse(request("travel", routeInput("xinghe", "boat", false)).body);
        check(!ended["arrived"].asBool() && ended["report"]["stopReason"].asString() == "ended",
              "寿尽中断行程报告ended，不谎报抵达");
        checkEq(ended["report"]["daysPassed"].asInt(), 1, "寿尽只推进到年界实际一天");
        checkEq(api::game().clock.days, baseline.clock.days + 1, "寿尽保留实际经过的一天，不自动回档");
        checkEq(api::game().player.stones, baseline.player.stones - travelOption(quote, "boat")->cost,
                "寿尽结算保留本次已支付船资，不凭自动回档退款");
        checkEq(api::game().checkpoint.restores, 0, "寿尽没有自动恢复存档");
        check(api::game().ended && api::game().ending && api::game().ending->cause == "寿终正寝" && !api::game().job,
              "自然寿尽进入人生结算，旧行程停止");
        checkStr(api::game().player.location, "wuling", "途中寿尽地点保持出发地");
        const auto state = ended["state"];
        bool blocked = true;
        for (const auto& location : state["locations"].asArray())
            for (const auto& option : location["travel"]["options"].asArray())
                blocked = blocked && !option["enabled"].asBool();
        check(blocked, "人生结束后地图所有赶路选项均禁用");
        const string beforeRestore = saveToJson(api::game());
        check(request("checkpoint/restore").status >= 400, "自然寿尽不能通过接口选择回档");
        check(saveToJson(api::game()) == beforeRestore, "拒绝寿尽回档保留完整结算进度");
    }
    {
        Game g = traveller(); g.clock.days = 1080; g.year = 3;
        g.player.age = g.clock.ageBase + 3; g.flags.erase("invitation"); g.flags.erase("choiceYear");
        importTraveller(g);
        const auto event = json::Value::parse(request("travel", routeInput("yunxi", "boat", false)).body);
        check(event["arrived"].asBool() && event["report"]["stopReason"].asString() == "event",
              "抵达后触发已有抉择，同时如实报告抵达与事件停止");
        check(api::game().pending.has_value() && !api::game().job,
              "事件停止不额外安排其他工作");
        rejectUnchanged(routeInput("wuling", "walk"), "待处理抉择期间赶路");
    }
}

} // namespace nztest
