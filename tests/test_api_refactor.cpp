#include "harness.h"
#include "core/api.h"
#include "core/artifact.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/checkpoint.h"
#include "core/interface/routes.h"
#include "core/save.h"
#include "core/sim.h"
#include <chrono>
#include <filesystem>
#include <set>

namespace nztest {
namespace {
using namespace nz;
Game viewFixture(const std::string& name, uint32_t seed) {
    CreateOptions options; options.name = name; options.seed = seed; options.traitsExplicit = true;
    Game game = createGame(options);
    game.pending.reset(); game.job.reset(); game.player.arts.battle.reset(); game.npcs.clear();
    game.player.lifespan = 3000;
    return game;
}
struct ReadOnlySaveScope {
    std::string previous = defaultSaveDir();
    std::filesystem::path directory = std::filesystem::current_path() /
        ("api-refactor-readonly-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ReadOnlySaveScope() {
        std::filesystem::create_directory(directory);
        const auto utf8Directory = directory.u8string();
        setSaveDirOverride(std::string(reinterpret_cast<const char*>(utf8Directory.data()), utf8Directory.size()));
    }
    ~ReadOnlySaveScope() {
        api::bindSession(nullptr); setSaveDirOverride(previous);
        std::error_code error; std::filesystem::remove(directory, error); // Only remove an empty test-owned directory.
    }
};
http::Response request(const std::string& method, const std::string& path) {
    http::Request request; request.method = method; request.path = path;
    return api::handle(request);
}
json::Value responseBody(const http::Response& response) { return json::Value::parse(response.body); }
}

void runApiRefactorTests() {
    using namespace nz;
    std::printf("\n--- API模块只读、会话隔离与路由策略 ---\n");
    ReadOnlySaveScope saveScope;
    api::Session first; first.started = true; first.g = viewFixture("第一会话", 382001);
    api::Session second; second.started = true; second.g = viewFixture("第二会话", 382002);
    checkpointEnable(first.g);
    // A stale decade checkpoint previously made GET /state mutate and write slot 4.
    first.g.clock.days = 3600; first.g.year = 10;
    first.g.player.age = first.g.clock.ageBase + first.g.year;
    first.writeError = "测试保存警告";
    const std::string firstBefore = saveToJson(first.g), secondBefore = saveToJson(second.g);
    api::bindSession(&first);
    const auto firstResponse = request("GET", "/api/state");
    const auto firstBody = responseBody(firstResponse);
    const auto repeated = responseBody(request("GET", "/api/state"));
    checkEq(firstResponse.status, 200, "完整状态允许GET读取");
    checkStr(firstBody.dump(), repeated.dump(), "重复GET状态响应确定一致");
    checkStr(saveToJson(first.g), firstBefore, "旧十年自动档的GET读取不改游戏、时间、RNG或快照");
    check(std::filesystem::is_empty(saveScope.directory), "状态查询不创建自动档或写磁盘");
    checkStr(firstBody["checkpointWarning"].asString(), first.writeError, "只读查询保留保存警告协议");
    const auto state = firstBody["state"];
    bool complete = true;
    for (const char* key : {"player", "arts", "clock", "tide", "world", "bonds", "bondActions", "adventure",
         "logs", "locations", "stories", "wildTasks", "plans", "breakthrough", "nearby", "pillCatalog", "market",
         "brewOptions", "gears", "poison", "rosterStat", "renownInfo", "manualCatalog", "manualSources",
         "librarySpells", "furnaces", "furnaceRepair", "dungeons", "cave", "checkpoint", "artBuilds"})
        complete = complete && !state[key].isNull();
    check(complete, "拆分后完整状态保留跨领域公开字段");
    checkStr(state["player"]["realmName"].asString(), realmName(first.g.player.stage), "中文境界协议值不变");
    api::bindSession(&second);
    const auto secondState = responseBody(request("GET", "/api/state"))["state"];
    checkStr(secondState["player"]["name"].asString(), "第二会话", "切换会话后状态来自所绑定账号");
    checkStr(saveToJson(second.g), secondBefore, "另一会话查询不改变另一账号");

    first.g = viewFixture("战斗视图", 382003);
    first.g.player.stage = first.g.player.highestStage = 6;
    Gear gear; gear.id = "view-sword"; gear.name = "视图灵剑"; gear.slot = "weapon";
    gear.shape = "sword"; gear.element = "火"; gear.polarity = "天阳"; gear.grade = 2;
    gear.bonus = .1; gear.crafted = true; gear.inscribed = true;
    first.g.player.arts.gear = {gear}; first.g.player.arts.equipped[gear.slot] = gear.id;
    const auto gearBefore = saveToJson(first.g);
    const auto equipment = api::equipmentView(first.g.player, gear);
    const auto ability = artifactAbility(gear);
    check(equipment["ability"]["preview"]["reference"].asBool() && equipment["ability"]["canEquip"].asBool(),
          "装备视图保留权威预览与可装备协议");
    checkEq(equipment["ability"]["preview"]["cost"].asInt(), battleCost(first.g.player, ability.spell), "装备预览消耗取自规则函数");
    checkStr(saveToJson(first.g), gearBefore, "装备投影预览不改变原背包、装备或随机状态");
    Player shadow = first.g.player; shadow.name = "天机影录对手";
    const Player challenger = first.g.player; battleStartShadow(first.g, challenger, shadow, 8822);
    const auto battleBefore = saveToJson(first.g);
    const auto battle = api::battleView(first.g);
    checkStr(battle["kind"].asString(), "tianji", "战斗视图保留天机影录类型");
    checkEq(battle["enemyMana"].asInt(), first.g.player.arts.battle->enemyCards.mana, "天机对手灵力字段来自真人战斗状态");
    check(!battle["intentDescription"].asString().empty() && battle["hand"].isArray() && battle["artifacts"].isArray(),
          "战斗视图保留敌方意图、手牌与法宝展示");
    checkStr(saveToJson(first.g), battleBefore, "读取指定游戏战斗视图不受当前绑定会话影响且无副作用");
    checkStr(saveToJson(second.g), secondBefore, "独立战斗视图未访问或改变当前绑定会话");

    std::set<std::string> paths; std::set<api::RouteId> ids; bool routeRoundTrip = true, methodsProtected = true;
    for (const auto& route : api::routeTable()) {
        routeRoundTrip = routeRoundTrip && paths.insert(route.path).second && ids.insert(route.id).second &&
            api::lookupRoute(route.path).id == route.id && std::string(api::routePath(route.id)) == route.path;
        if (route.effect != api::RouteEffect::ReadOnly)
            methodsProtected = methodsProtected && request("GET", route.path).status == 405;
    }
    check(routeRoundTrip, "集中路由表无重复且标识和兼容路径双向一致");
    check(methodsProtected, "每条写操作路由都拒绝GET");
    checkEq(request("GET", "/api/not-a-route").status, 405, "未知GET不能获得只读许可");
    checkEq(request("POST", "/api/not-a-route").status, 404, "空闲已开始会话的未知POST返回404");
    const auto unknownBefore = saveToJson(second.g);
    second.g.pending = Pending{};
    checkStr(responseBody(request("POST", "/api/not-a-route"))["error"].asString(),
             "请先处理当前抉择。", "未知POST仍遵守原动作保护");
    second.g.pending.reset();
    second.g.dungeons.run = DungeonRun{};
    checkStr(responseBody(request("POST", "/api/dungeon/not-a-route"))["error"].asString(),
             "请先结束当前秘境探索。", "未知秘境前缀不绕过秘境保护");
    second.g.dungeons.run.reset();
    checkStr(saveToJson(second.g), unknownBefore, "错误路由和方法不改变会话");
    second.started = false;
    checkEq(request("POST", "/api/not-a-route").status, 400, "未开局未知POST保留开局保护");
}
} // namespace nztest
