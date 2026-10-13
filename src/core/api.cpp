// 宁州一梦 · C++ 版 — 游戏接口（前端通过 HTTP 调用这里的规则）
//
// 这一层是「浏览器界面」与「C++ 规则」之间的桥：
//   前端发 POST /api/<动作>  ->  这里解析参数、调用 core 函数  ->  返回 JSON
//
// 设计原则：这一层只做「翻译」，不写任何规则。
// 所有数值都来自 core，保证和自测覆盖的是同一套代码。
#include "core/api.h"
#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/interface/view_policy.h"
#include "core/interface/routes.h"
#include "core/api_exploration.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/source_limits.h"
#include "core/talents.h"
#include "core/sect_trial.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "core/adventure.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/json.h"
#include "core/conflict.h"
#include "core/cultivate.h"
#include "core/economy.h"
#include "core/crafting/material_supply.h"
#include "core/friendship.h"
#include "core/tide.h"
#include "core/legacy.h"
#include "core/play.h"
#include "core/renown.h"
#include "core/reward_receipt.h"
#include "core/roster.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/sword.h"
#include "core/npc_learning.h"
#include "core/location_services.h"
#include "core/travel.h"
#include "data/reference_tables.h"

namespace nz::api {

using json::Value;
using view::num;
using view::str;
using view::actionBlockReason;
using view::breakthroughReason;

namespace {
// 用途：取得默认单机会话。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：缺少账号绑定时使用的默认会话引用。
Session& defaultSession() { static Session session; return session; }
Session* currentSession = nullptr;
// 用途：选择线程绑定账号或默认会话。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：当前线程账号会话的可写引用。
Session& sessionState() { return currentSession ? *currentSession : defaultSession(); }
} // namespace

// 用途：绑定当前线程的独立角色会话。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Session* session，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void bindSession(Session* session) { currentSession = session; }

// 用途：校验并恢复账号游戏文本。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Session& session, const std::string& text, std::string* err，按本模块入口调用。
// 返回值：校验成功为真并替换会话；失败为假且保留旧游戏。
bool restoreGame(Session& session, const std::string& text, std::string* err) {
    Game loaded;
    if (!loadFromJson(text, loaded, err)) return false;
    session.g = std::move(loaded);
    checkpointEnable(session.g);
    session.started = true;
    return true;
}

// 用途：取得当前会话游戏并初始化缺失状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：当前账号游戏的可写引用。
Game& game() {
    return sessionState().g;
}

// 这一份 game() 里是否真的开过局。
// 冷启动（服务刚起、还没点「新的一世」）时是 false ——
// 前端要靠它区分「空壳默认值」和「真的存档」，
// 否则会把默认值当存档渲染，一读 S.clock 就崩。
// 用途：取得当前会话是否建号的可写标记。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：当前账号是否已建号标记的引用，可由入口更新。
bool& started() {
    return sessionState().started;
}

namespace {

// ------------------------------------------------------------
// 小工具
// ------------------------------------------------------------
// 用途：读取请求字符串字段与默认值。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Value& input, const char* key, const std::string& def = ""，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string strOf(const Value& input, const char* key,
                  const std::string& def = "") {
    const Value& argumentValue = input[key];
    return argumentValue.isString() ? argumentValue.asString() : def;
}

// 用途：读取请求整数并限制到支持范围。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Value& input, const char* key, int def = 0，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int intOf(const Value& input, const char* key, int def = 0) {
    const Value& argumentValue = input[key];
    if (!argumentValue.isNumber()) return def;
    const double n = argumentValue.asNumber();
    if (!std::isfinite(n) || n < std::numeric_limits<int>::min() ||
        n > std::numeric_limits<int>::max()) return def;
    return static_cast<int>(n);
}

// 用途：读取可表示为无符号整数的随机种子。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Value& input, uint32_t def = 20261004，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
uint32_t seedOf(const Value& input, uint32_t def = 20261004) {
    const Value& argumentValue = input["seed"];
    return argumentValue.isNumber() ? static_cast<uint32_t>(argumentValue.asNumber()) : def;
}



// 统一响应
// 用途：编码成功JSON响应。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Value& v，按本模块入口调用。
// 返回值：包含状态码与JSON内容的HTTP响应。
http::Response jsonOk(const Value& v) {
    http::Response r;
    r.status = 200;
    r.body = v.dump();
    return r;
}

// 用途：编码指定状态码与中文失败原因。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const std::string& msg, int status = 400，按本模块入口调用。
// 返回值：包含状态码与JSON内容的HTTP响应。
http::Response jsonError(const std::string& msg, int status = 400) {
    Value o(json::Object{});
    o.set("ok", Value(false));
    o.set("error", str(msg));
    http::Response r;
    r.status = status;
    r.body = o.dump();
    return r;
}

// ------------------------------------------------------------
// 传承档案的存读（放存档目录下的 legacy.json）
// ------------------------------------------------------------
// 用途：取得当前账号传承档案路径。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string legacyPath() { return defaultSaveDir() + "/legacy.json"; }

// 用途：把UTF8字符串转换为原生文件路径。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const std::string& text，按本模块入口调用。
// 返回值：std::filesystem::path结果，包含说明所述状态或计算数据。
std::filesystem::path utf8Path(const std::string& text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

// 用途：读取并校验当前账号传承档案。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：legacy::Archive结果，包含说明所述状态或计算数据。
legacy::Archive legacyLoad() {
    std::ifstream f(utf8Path(legacyPath()), std::ios::binary);
    if (!f) return legacy::create();
    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = false;
    legacy::Archive archive = legacy::fromJson(ss.str(), &ok);
    // 档案损坏时退回空白档案，不要卡死游戏
    return ok ? archive : legacy::create();
}

// 用途：写入临时传承文件后原子替换。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const legacy::Archive& archive，按本模块入口调用。
// 返回值：写入替换完成为真；失败为假并保留已有档案。
bool legacySave(const legacy::Archive& archive) {
    std::error_code ec;
    std::filesystem::create_directories(utf8Path(defaultSaveDir()), ec);
    if (ec) return false;
    const auto path = utf8Path(legacyPath());
    const auto temporary = utf8Path(legacyPath() + ".tmp");
    std::ofstream f(temporary, std::ios::binary);
    if (!f) return false;
    f << legacy::toJson(archive);
    f.close();
    if (f.fail()) return false;
#ifdef _WIN32
    return MoveFileExW(temporary.c_str(), path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temporary, path, ec);
    return !ec;
#endif
}

struct RestartLegacy {
    legacy::Archive archive;
    legacy::SettleResult result;
};

// 用途：预演重开时上一世的新成就；参与流程：建号查询、方案校验、正式重开。
// 使用方式：传当前人生，只修改临时副本；返回值：预计传承档案与本次结算结果，不写磁盘、不消费 RNG。
RestartLegacy restartLegacy(const Game& current) {
    RestartLegacy preview{legacyLoad(), {}};
    if (!started()) return preview;
    Game previous = current;
    if (!previous.ended) finish(previous, "主动重开");
    preview.result = legacy::settle(previous, preview.archive);
    return preview;
}

// ------------------------------------------------------------
// 状态快照：前端每次动作后都拿它整页重绘
// ------------------------------------------------------------


// 用途：组合各领域前端视图。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value buildState(const Game& game) {
    view::ViewContext context;
    if (game.ending) {
        const auto archive = legacyLoad();
        context.endingClaimed = std::find(archive.claimed.begin(), archive.claimed.end(), game.lifeId) != archive.claimed.end();
    }
    return view::state(game, context);
}

// 统一响应：带上完整状态
// 用途：封装成功标记和当前角色快照。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Game& gameState，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value okWithState(Game& gameState) {
    checkpointTick(gameState);
    // 月历可能在一次长行动中创建自动快照；请求返回时将其落入独立槽位。
    string& lastAutomatic = sessionState().lastAutomatic;
    string& writeError = sessionState().writeError;
    if (gameState.checkpoint.enabled && gameState.checkpoint.source == "auto" &&
        !gameState.checkpoint.snapshot.empty() && lastAutomatic != gameState.checkpoint.snapshot) {
        Game automatic;
        string error;
        if (loadFromJson(gameState.checkpoint.snapshot, automatic, &error)) {
            automatic.checkpoint = gameState.checkpoint;
            if (saveToFile(automatic, savePathFor(4), &error)) {
                lastAutomatic = gameState.checkpoint.snapshot;
                writeError.clear();
            } else writeError = "自动存档写入失败：" + error;
        } else writeError = "自动存档校验失败：" + error;
    }
    Value o(json::Object{});
    o.set("ok", Value(true));
    o.set("state", buildState(gameState));
    if (!writeError.empty()) o.set("checkpointWarning", str(writeError));
    return o;
}

// ------------------------------------------------------------
// 推进时间（前端点「推进行动」）
// ------------------------------------------------------------
// 记录推进过程中发生的事，返回给前端做「这段时间发生了什么」的提示
struct AdvanceReport {
    int daysPassed = 0;
    std::string stopReason;      // event / battle / ended / done
    std::vector<std::string> highlights;
};

// 推进。autoStop 为 true 时会连续跨月，直到发生事件、进入斗法、或到达目标天数。
// 这是为了解决「悟道炼丹过月反复点击」的坏体验：
// 玩家点一次，就该一直走到有事发生为止。
// 用途：按目标天数推进并在事件战斗死亡处停止。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Game& gameState, int days, bool autoStop，按本模块入口调用。
// 返回值：AdvanceReport结果，包含说明所述状态或计算数据。
AdvanceReport advanceEx(Game& gameState, int days, bool autoStop) {
    AdvanceReport rep;
    const int startDays = gameState.clock.days;
    const size_t startLogs = gameState.logs.size();

    int guard = 0;
    while (gameState.clock.days - startDays < days && guard++ < 200) {
        if (gameState.ended) { rep.stopReason = "ended"; break; }
        if (gameState.pending.has_value()) { rep.stopReason = "event"; break; }
        if (gameState.player.arts.battle.has_value()) { rep.stopReason = "battle"; break; }

        if (!gameState.job.has_value()) {
            Job j;
            j.actionType = "plan";
            j.plan = gameState.plan;
            j.name = findPlan(gameState.plan) ? findPlan(gameState.plan)->name : "计划";
            // 一次安排不超过剩余目标天数，跨月由循环自然接续
            const int remain = days - (gameState.clock.days - startDays);
            const int chunk = std::max(1, std::min(remain, calendarDate(gameState).left));
            j.total = chunk;
            j.remaining = chunk;
            j.started = gameState.clock.days;
            const double yearRatio = chunk / 360.0;
            const int cost = (gameState.plan == "cultivate")
                                 ? calendarTake(gameState, "cultivationCost", cultivationCost(gameState.player) * yearRatio)
                                 : 0;
            j.cost = cost;
            const int reserve = gameState.plan == "cultivate" && gameState.clock.carry["cultivationCost"] > 1e-9 ? 1 : 0;
            j.paid = gameState.player.stones >= cost + reserve;
            if (j.paid) gameState.player.stones -= cost;
            gameState.job = j;
        }

        const string previousTideEvent=gameState.tide.eventId;
        const CalRunResult r = calendarRun(gameState, days - (gameState.clock.days - startDays));
        if (!r.ok) { rep.stopReason = "error"; break; }
        if (r.restored) { rep.stopReason = "restored"; break; }
        if (r.ended) { rep.stopReason = "ended"; break; }
        if (r.pending) { rep.stopReason = "event"; break; }
        if (gameState.player.arts.battle.has_value()) { rep.stopReason = "battle"; break; }
        // 新兽潮窗口只有60天；遇事暂停不能在一次长推进里直接跳过整场活动。
        if (autoStop && gameState.tide.active && gameState.tide.eventId!=previousTideEvent) {
            rep.stopReason = "event"; break;
        }
        if (!autoStop) break;   // 单步模式：跑完当前这段就返回
    }

    rep.daysPassed = std::max(0, gameState.clock.days - startDays);
    if (rep.stopReason.empty()) rep.stopReason = "done";

    // 收集这段时间里值得给玩家看的事
    for (size_t i = startLogs; i < gameState.logs.size(); ++i) {
        const LogEntry& e = gameState.logs[i];
        if (!e.important) continue;
        rep.highlights.push_back(e.text);
    }
    return rep;
}

// 用途：推进当前工作或计划并更新检查点。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入Game& gameState, int days，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void advance(Game& gameState, int days) {
    if (!gameState.job.has_value()) {

        Job j;
        j.actionType = "plan";
        j.plan = gameState.plan;
        j.name = findPlan(gameState.plan) ? findPlan(gameState.plan)->name : "计划";
        j.total = days;
        j.remaining = days;
        j.started = gameState.clock.days;
        const int cost = (gameState.plan == "cultivate")
                             ? calendarTake(gameState, "cultivationCost", cultivationCost(gameState.player) * days / 360.0)
                             : 0;
        j.cost = cost;
        const int reserve = gameState.plan == "cultivate" && gameState.clock.carry["cultivationCost"] > 1e-9 ? 1 : 0;
        j.paid = gameState.player.stones >= cost + reserve;
        if (j.paid) gameState.player.stones -= cost;
        gameState.job = j;
    }
    int guard = 0;
    while (gameState.job.has_value() && guard++ < 400) {
        const CalRunResult r = calendarRun(gameState);
        if (!r.ok || r.restored || r.ended || r.pending || gameState.player.arts.battle.has_value())
            break;
    }
}

} // namespace

// 用途：公开当前斗法预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& gameState，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value battleView(const Game& gameState) { return view::battle(gameState); }
// 用途：公开法器能力和装备效果。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value equipmentView(const Player& player, const Gear& gear) { return view::equipment(player, gear); }

// ------------------------------------------------------------
// 路由
// ------------------------------------------------------------
// 用途：按稳定路由分派请求并校验状态与参数。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const http::Request& req，按本模块入口调用。
// 返回值：包含状态码与JSON内容的HTTP响应。
http::Response handle(const http::Request& req) {
    Game& gameState = game();
    const std::string& path = req.path;
    Value in(json::Object{});
    if (!req.body.empty()) {
        std::string err;
        in = Value::parse(req.body, &err);
        if (!err.empty() || !in.isObject())
            return jsonError("请求正文必须是有效的 JSON 对象。");
    }

    const RouteInfo& routeInfo = lookupRoute(path);
    const RouteId route = routeInfo.id;
    const bool readOnly = routeInfo.effect == RouteEffect::ReadOnly;
    if (req.method != "POST" && !(req.method == "GET" && readOnly))
        return jsonError("此操作需要 POST 请求。", 405);
    // 数值先验范围，避免浮点转整数溢出或非法参数悄悄执行默认操作。
    for (const char* key : {"days", "slot", "count", "choice", "level", "roll", "batch", "expectedCost", "battlefield", "expectedUnitPrice"}) {
        const Value& argumentValue = in[key];
        if (!argumentValue.isNull() && (!argumentValue.isNumber() || !std::isfinite(argumentValue.asNumber()) ||
            std::floor(argumentValue.asNumber()) != argumentValue.asNumber() ||
            argumentValue.asNumber() < std::numeric_limits<int>::min() ||
            argumentValue.asNumber() > std::numeric_limits<int>::max()))
            return jsonError(std::string("参数 ") + key + " 必须是有效整数。");
    }
    if (!in["seed"].isNull() && (!in["seed"].isNumber() ||
        !std::isfinite(in["seed"].asNumber()) ||
        std::floor(in["seed"].asNumber()) != in["seed"].asNumber() ||
        in["seed"].asNumber() < 0 || in["seed"].asNumber() > 4294967295.0))
        return jsonError("种子必须是 0 到 4294967295 之间的整数。");
    if (in["allocation"].isObject()) {
        for (const auto& [key, value] : in["allocation"].asObject())
            if (!value.isNumber() || !std::isfinite(value.asNumber()) ||
                std::floor(value.asNumber()) != value.asNumber() ||
                value.asNumber() < 0 || value.asNumber() > MAX_POINTS)
                return jsonError("属性分配必须是有效的非负整数。");
    }
    const bool replaceGame = routeInfo.effect == RouteEffect::ReplaceGame;
    if (!readOnly && !replaceGame) {
        if (!started()) return jsonError("请先创建角色或读取存档。");
        const bool persist = routeInfo.effect == RouteEffect::Persistence;
        if (!persist) {
            const string blocked = actionBlockReason(gameState, path);
            if (!blocked.empty()) return jsonError(blocked);
        }
    }
    if (route == RouteId::ExportGame && !started())
        return jsonError("当前没有可导出的角色存档。");

    // ---- 新游戏 ----
    if (route == RouteId::NewGame) {
        CreateOptions opts;
        opts.name = strOf(in, "name", "沈知微");
        opts.gender = strOf(in, "gender", "女");
        opts.seed = seedOf(in);

        // 天赋：支持多选（traits 数组）；兼容旧的单个 talent 字段
        if (in["traits"].isArray()) {
            for (const auto& argumentValue : in["traits"].asArray())
                if (argumentValue.isString()) opts.traits.push_back(argumentValue.asString());
            // 明确给了数组（哪怕是空的）就按它来，不再补默认天赋
            opts.traitsExplicit = true;
        } else {
            opts.traits.push_back(strOf(in, "talent", "sword"));
        }

        // 属性点分配
        if (in["allocation"].isObject())
            for (const auto& [k, v] : in["allocation"].asObject())
                if (v.isNumber()) opts.allocation[k] = v.asInt();

        // 建号方案非法时给出具体原因，而不是直接把角色建坏
        const RestartLegacy inheritance = restartLegacy(gameState);
        const int budget = legacy::talentBudget(inheritance.archive);
        const play::BuildCheck c = play::checkBuild(opts.traits, opts.allocation, budget);
        if (!c.ok) return jsonError(c.error.empty() ? "建号方案不合法。" : c.error);
        opts.budget = budget;
        opts.lifeId = legacy::newLifeId(opts.seed);

        Game next = createGame(opts);
        if (inheritance.result.eligible && !legacySave(inheritance.archive))
            return jsonError("上一世传承保存失败，当前角色与成就均已保留，请稍后再试。", 500);
        gameState = std::move(next);
        checkpointEnable(gameState);
        started() = true;
        Value response = okWithState(gameState);
        response.set("legacyPoints", num(inheritance.result.points));
        response.set("budget", num(budget));
        return jsonOk(response);
    }

    // ---- 查询状态 ----
    if (route == RouteId::State) {
        // 还没开过局：明确告诉前端「没有存档」，
        // 不要把一个空壳默认值当成存档发过去 —— 前端会照着渲染然后崩掉。
        if (!started()) {
            Value o(json::Object{});
            o.set("ok", Value(true));
            o.set("hasGame", Value(false));
            return jsonOk(o);
        }
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("state", buildState(gameState));
        if (!sessionState().writeError.empty()) o.set("checkpointWarning", str(sessionState().writeError));
        o.set("hasGame", Value(true));
        return jsonOk(o);
    }

    // ---- 存档 ----
    if (route == RouteId::CheckpointRestore) {
        if (!gameState.ended || !gameState.ending || gameState.ending->cause=="寿终正寝")
            return jsonError("只有意外死亡且尚未接受结局时，才能选择回到最近存档。");
        const auto archive=legacyLoad();
        if (std::find(archive.claimed.begin(),archive.claimed.end(),gameState.lifeId)!=archive.claimed.end())
            return jsonError("已接受本世结局并结算传承，不能再恢复这一世。");
        const string reason=gameState.ending->cause;
        if (!checkpointRestore(gameState,reason)) return jsonError("最近存档不存在或损坏，无法恢复。");
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::Save) {
        std::string err;
        const int slot = intOf(in, "slot", 1);
        if (slot < 1 || slot > 3) return jsonError("存档位需为 1 到 3。");
        const auto previous = gameState.checkpoint;
        checkpointManual(gameState,slot);
        if (!saveToFile(gameState, savePathFor(slot), &err)) {
            gameState.checkpoint = previous;
            return jsonError("保存失败：" + err, 500);
        }
        return jsonOk(okWithState(gameState));
    }
    // ---- 导出存档为 JSON 文本 ----
    // 对应 JS app.mjs 的 exportSave：{format, version, game, legacy}
    // 玩家可以把这一世存成文件带走、备份、或改完再导回来。
    if (route == RouteId::ExportGame) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("format", str("ningzhou-dream"));
        o.set("version", num(VERSION));
        // game 用字符串塞进去，避免前端重新序列化时改变数值
        o.set("game", str(saveToJson(gameState)));
        o.set("legacy", str(legacy::toJson(legacyLoad())));
        o.set("name", str(gameState.player.name));
        o.set("year", num(gameState.year));
        o.set("realm", str(realmOf(gameState.player.stage).name));
        return jsonOk(o);
    }

    // ---- 从 JSON 文本导入存档 ----
    if (route == RouteId::ImportGame) {
        // 接受两种输入：整包 {game:"...", legacy:"..."}，或直接给 game 的 JSON
        std::string gameText = strOf(in, "game");
        if (gameText.empty() && in["save"].isString())
            gameText = in["save"].asString();
        if (gameText.empty() && in["text"].isString())
            gameText = in["text"].asString();
        if (gameText.empty())
            return jsonError("没有读到存档内容。");

        Game loaded;
        std::string err;
        if (!loadFromJson(gameText, loaded, &err))
            return jsonError("存档解析失败：" + err);
        // 传承档案可选：带上了就一并导入
        const std::string legText = strOf(in, "legacy");
        if (!legText.empty()) {
            bool okLegacy = false;
            legacy::Archive archive = legacy::fromJson(legText, &okLegacy);
            if (!okLegacy) return jsonError("传承档案格式或数值无效。");
            if (!legacySave(archive)) return jsonError("传承档案写入失败，当前角色未被替换。", 500);
        }
        gameState = std::move(loaded);
        checkpointEnable(gameState);
        started() = true;
        return jsonOk(okWithState(gameState));
    }

    if (route == RouteId::LoadGame) {

        const int slot = intOf(in, "slot", 1);
        if (slot < 1 || slot > 4) return jsonError("读取存档位需为 1 到 4（4 为自动存档）。");
        Game loaded;
        std::string err;
        if (!loadFromFile(savePathFor(slot), loaded, &err))
            return jsonError("读取失败：" + err, 404);
        gameState = loaded;
        checkpointEnable(gameState);
        started() = true;
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::Slots) {
        Value o(json::Object{});
        json::Array arr;
        for (const auto& s : listSaveSlots()) {
            Value so(json::Object{});
            so.set("slot", num(s.slot));
            so.set("automatic", Value(s.automatic));
            so.set("exists", Value(s.exists));
            so.set("name", str(s.name));
            so.set("realm", str(s.realm));
            so.set("year", num(s.year));
            so.set("age", num(s.age));
            arr.push_back(std::move(so));
        }
        o.set("ok", Value(true));
        o.set("slots", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 推进 ----
    if (route == RouteId::Advance) {
        const int days = intOf(in, "days", calendarDate(gameState).left);
        if (days < 1 || days > 3600) return jsonError("每次推进天数需为 1 到 3600 天。");
        // autoStop 默认开启：一直走到有事发生为止，不用玩家反复点
        const bool autoStop = in["autoStop"].isBool() ? in["autoStop"].asBool() : true;
        const AdvanceReport rep = advanceEx(gameState, days, autoStop);
        Value o = okWithState(gameState);
        Value ro(json::Object{});
        ro.set("daysPassed", num(rep.daysPassed));
        ro.set("stopReason", str(rep.stopReason));
        json::Array hl;
        for (const auto& h : rep.highlights) hl.push_back(str(h));
        ro.set("highlights", Value(std::move(hl)));
        o.set("report", std::move(ro));
        return jsonOk(o);
    }
    if (route == RouteId::Plan) {
        const std::string plan = strOf(in, "plan", gameState.plan);
        if (!findPlan(plan)) return jsonError("没有这种修行计划。");
        gameState.plan = plan;
        return jsonOk(okWithState(gameState));
    }

    // ---- 突破 ----
    if (route == RouteId::Breakthrough) {
        const BreakthroughInfo info = breakthroughInfo(gameState);
        const string blocked = breakthroughReason(gameState, info);
        if (!blocked.empty()) return jsonError(blocked);
        gameState.player.stones -= info.cost;
        // 大境界要消耗凝元丹与灵核各一枚。
        // 之前不扣，但成功率公式仍然吃 +18% / +10% 的加成，
        // 等于每次突破白拿 28% —— 核心成长曲线会被破坏。
        if (info.major) {
            if (gameState.player.pills > 0) gameState.player.pills--;
            if (gameState.player.cores > 0) gameState.player.cores--;
            gameState.player.arts.breakBuff.clear();
        }
        Job j;
        j.actionType = "breakthrough";
        j.name = "闭关叩关";
        j.total = info.major ? 90 : 30;
        j.remaining = j.total;
        j.started = gameState.clock.days;
        j.cost = info.cost;
        j.paid = true;
        j.breakthroughPrepared = true;
        j.breakthroughMajor = info.major;
        j.breakthroughChance = info.chance;
        j.breakthroughFailureLoss = info.failureLoss;
        gameState.job = j;
        advance(gameState, j.total);
        return jsonOk(okWithState(gameState));
    }

    // ---- 抉择 ----
    if (route == RouteId::Choose) {
        if (!gameState.pending.has_value()) return jsonError("当前没有待处理的抉择。");
        const std::string id = strOf(in, "id");
        // 选项必须真的存在于这次抉择里，且未被禁用。
        // 之前直接往下走，随便传个 id 也能「选成功」，
        // 等于绕过了事件的正确分支。
        {
            bool found = false;
            for (const auto& o : gameState.pending->options) {
                if (o.id != id) continue;
                found = true;
                if (o.disabled) return jsonError("这个选项当前不可选。");
                break;
            }
            if (!found) return jsonError("没有这个选项。");
        }
        if (gameState.pending->type == "adventure") {
            adventureChoose(gameState, id);
        } else {
            Player& player = gameState.player;
            const Pending ev = *gameState.pending;
            const RewardReceipt receipt(gameState);
            if (ev.type == "sectInvitation") {
                if (id == "join" && player.sect.empty()) {
                    logEvent(gameState, "sect",
                             "你接受金虹剑派的邀请，记下山门所在。前往山门通过入门考核后，方可正式拜师。",
                             true);
                } else if (id == "study") {
                    player.insight = std::min(INSIGHT_MAX, player.insight + 8);
                    logEvent(gameState, "self", "你向五宗使者请教，记下各自的修炼门径。");
                } else if (id == "free") {
                    player.stones += 20;
                    logEvent(gameState, "self", "你替使者捎带一封回信，获赠路资。");
                }
            } else if (ev.type == "valley") {
                Npc* npc = nullptr;
                if (!ev.npcId.empty())
                    for (auto& n : gameState.npcs)
                        if (n.id == ev.npcId && n.alive) npc = &n;
                if (id == "help") {
                    adventureHarvestHerbs(gameState, 3);
                    if (npc) addRelation(gameState, npc->id, 20);
                    if (randomOf(gameState) > 0.48 + player.luck * 0.003) {
                        applyDamage(gameState, 16);
                        player.health = std::max(1, player.health);
                    }
                    logEvent(gameState, "self", "你合力破阵，按灵药存量与同行道友结算收获。", true);
                } else if (id == "take") {
                    const int found = adventureHarvestHerbs(gameState, 5);
                    if (npc) addRelation(gameState, npc->id, -15);
                    logEvent(gameState, "self",
                             "你采走 " + std::to_string(found) + " 份灵药，避开了禁制。");
                } else if (id == "leave") {
                    player.insight = std::min(INSIGHT_MAX, player.insight + 5);
                    logEvent(gameState, "self", "你记下方位与阵纹，留待日后再访。");
                }
            }
            logEvent(gameState, "self", "「" + ev.title + "」实际结算：" + receipt.changes(gameState) + "。");
            gameState.pending.reset();
        }
        return jsonOk(okWithState(gameState));
    }

    // ---- 斗法 ----
    if (route == RouteId::DungeonEnter || route == RouteId::DungeonChoose ||
        route == RouteId::DungeonClaim || route == RouteId::DungeonLeave || route == RouteId::DungeonIntel) {
        string error;
        const int restores=gameState.checkpoint.restores;
        if (route == RouteId::DungeonEnter) error = dungeonEnter(gameState,strOf(in,"id"));
        else if (route == RouteId::DungeonChoose) error = dungeonChoose(gameState,strOf(in,"id"));
        else if (route == RouteId::DungeonClaim) error = dungeonClaim(gameState,strOf(in,"id"));
        else if (route == RouteId::DungeonLeave) error = dungeonLeave(gameState);
        else error = dungeonIntel(gameState,strOf(in,"kind"),strOf(in,"id"));
        if (!error.empty()) return jsonError(error);
        if (gameState.checkpoint.restores==restores && !gameState.player.arts.battle && gameState.job && gameState.job->actionType == "socialElapsed" && !gameState.ended)
            advanceEx(gameState,gameState.job->remaining,true);
        Value response = okWithState(gameState); response.set("targetTab",str("dungeon")); return jsonOk(response);
    }
    if (route == RouteId::BattleTurn) {
        if (!gameState.player.arts.battle.has_value())
            return jsonError("当前没有斗法。");
        const std::string id = strOf(in, "id");
        const int restores = gameState.checkpoint.restores;
        const TurnResult r = battleTurn(gameState, id, strOf(in, "target"));
        if (!r.ok) return jsonError(r.error);
        // 奖励结算已收口在 core 的 battleFinish 里，
        // 所以这里不需要再做什么 —— 见 battle.cpp 的说明。
        if (gameState.checkpoint.restores == restores && !gameState.player.arts.battle && gameState.job && gameState.job->actionType == "socialElapsed" && !gameState.ended) advanceEx(gameState, gameState.job->remaining, true);
        return jsonOk(okWithState(gameState));
    }

    // ---- 设主修 / 辅修功法 ----
    // 主修决定修炼与战斗的主要加成；辅修效果减半。
    // 之前只有 CLI 能设主修，web 完全没有入口。
    if (route == RouteId::ArtsMain || route == RouteId::ArtsAux) {
        Arts& arts = gameState.player.arts;
        const std::string mid = strOf(in, "id");
        if (arts.manuals.find(mid) == arts.manuals.end() || arts.manuals.at(mid) <= 0)
            return jsonError("尚未修习这部功法。");
        if (route == RouteId::ArtsMain) {
            if (arts.main == mid) return jsonError("已经是主修了。");
            // 从辅修里摘掉，避免同时是主修又辅修
            arts.aux.erase(std::remove(arts.aux.begin(), arts.aux.end(), mid),
                        arts.aux.end());
            arts.main = mid;
            ManualInfo mv;
            logEvent(gameState, "self",
                     "改以《" + (findManualInfo(mid, mv) ? mv.name : mid) +
                         "》为主修功法。",
                     true);
        } else {
            if (arts.main == mid) return jsonError("主修功法不能同时作辅修。");
            const auto found = std::find(arts.aux.begin(), arts.aux.end(), mid);
            if (found != arts.aux.end()) arts.aux.erase(found);
            else {
                if (arts.aux.size() >= 4) return jsonError("辅修最多四部。");
                arts.aux.push_back(mid);
            }
            ManualInfo mv;
            logEvent(gameState, "self",
                     "以《" + (findManualInfo(mid, mv) ? mv.name : mid) +
                         "》为辅修功法。",
                     false);
        }
        return jsonOk(okWithState(gameState));
    }

    // ---- 携带 / 卸下神通 ----
    // 与 JS arts-ui.mjs 的「携带神通 / 卸下神通」对应。
    // 之前 C++ 版完全没有这个入口（artInstant 未实现），
    // 玩家只能一直用开局那四个初始神通，换了功法也带不上新神通。
    if (route == RouteId::ArtsSpell) {
        const std::string sid = strOf(in, "id");
        // 传 spell 字段也接受，方便前端两种写法
        const std::string target = sid.empty() ? strOf(in, "spell") : sid;
        const ActionError r = artInstant(gameState, "spell", target);
        if (!r.ok) return jsonError(r.error);
        const SpellDef* sp = battleSpell(target);
        const bool nowCarried =
            std::find(gameState.player.arts.spells.begin(), gameState.player.arts.spells.end(),
                      target) != gameState.player.arts.spells.end();
        Value o = okWithState(gameState);
        o.set("text",
              str((nowCarried ? "已携带神通「" : "已卸下神通「") +
                  (sp ? sp->name : target) + "」。"));
        o.set("carried", Value(nowCarried));
        return jsonOk(o);
    }

    // ---- 法器：装备 / 卸下 / 铭刻 / 分解 ----
    if (route == RouteId::ArtsGear) {
        const std::string action = strOf(in, "action", "equip");
        const std::string gid = strOf(in, "id");
        const ActionError r = artInstant(gameState, action, gid);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }

    // ---- 洞府与配给 ----
    // 灵石配给到处都算；灵气配给只在洞府闭关时生效。
    // 两者都按月扣灵石，境界越高越贵。
    if (route == RouteId::Supply) {
        const auto r = shop::caveSupplySet(gameState,strOf(in,"kind"),intOf(in,"level",0));
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }

    // 迁府：把洞府安在当下所在地（灵气配给只在这里生效）
    if (route == RouteId::CaveMove) {
        const auto r = shop::caveMove(gameState);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::CaveUpgrade) {
        const auto r = shop::caveUpgrade(gameState);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }

    if (route == RouteId::ArtsNode) {
        const std::string nid = strOf(in, "node");
        const ActionError result = artInstant(gameState, "node", nid);
        if (!result.ok) return jsonError(result.error);
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::ArtsFocus) {
        const std::string dao = strOf(in, "dao");
        if (daoIndex(dao) < 0) return jsonError("没有这个大道。");
        gameState.player.arts.focus = dao;
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::ArtsLearn) {
        const std::string id = strOf(in, "id");
        const std::string err = ecoActionError(gameState, "learn", id);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(gameState, "learn", id);
        if (!info.valid) return jsonError("无此功法。");
        ecoBegin(gameState, "learn", id);
        advance(gameState, info.days);
        return jsonOk(okWithState(gameState));
    }

    // ---- 全部神通（含是否已携带、是否够境界）----
    // 前端「携带神通」面板要能列出 40 张神通并逐张切换，
    // 光给已携带的 spellList 是没法换神通的。
    if (route == RouteId::ArtsSpells) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        auto addSpell = [&](const SpellDef& sp) {
            Value so(json::Object{});
            so.set("id", str(sp.id));
            so.set("name", str(sp.name));
            so.set("dao", str(sp.dao));
            so.set("kind", str(sp.kind));
            so.set("cost", num(sp.cost));
            so.set("actualCost", num(battleCost(gameState.player, sp)));
            so.set("cooldown", num(sp.cooldown));
            so.set("stage", num(sp.stage));
            so.set("stageName", str(realmName(sp.stage)));
            so.set("description", str(battleSpellDescription(sp)));
            so.set("preview", view::spellPreview(gameState.player, sp));
            so.set("requiresTarget", Value(battleNeedsTarget(sp)));
            const bool mandatory = sp.id == "strike" || sp.id == "defend";
            const bool carried = mandatory || std::find(gameState.player.arts.spells.begin(),
                                   gameState.player.arts.spells.end(),
                                   sp.id) != gameState.player.arts.spells.end();
            so.set("carried", Value(carried));
            so.set("mandatory", Value(mandatory));
            const bool known = mandatory || std::find(gameState.player.arts.knownSpells.begin(), gameState.player.arts.knownSpells.end(), sp.id) != gameState.player.arts.knownSpells.end();
            so.set("known", Value(known));
            so.set("learned", Value(known)); so.set("equipped", Value(carried));
            so.set("learnable", Value(!known && gameState.player.stage >= sp.stage));
            so.set("canEquip", Value(!mandatory && !carried && known && gameState.player.stage >= sp.stage &&
                gameState.player.arts.spells.size() < static_cast<size_t>(battleSpellSlots(gameState.player))));
            so.set("canUnequip", Value(!mandatory && carried));
            so.set("unlocked", Value(gameState.player.stage >= sp.stage));
            std::string reason;
            if (mandatory) reason = "普通招式固定携带，不占额外神通名额。";
            else if (!carried && gameState.player.stage < sp.stage) reason = "需达到" + realmName(sp.stage) + "。";
            else if (!carried && !known) reason = "尚未领悟，请向熟悉此术的道友请教。";
            else if (!carried && gameState.player.arts.spells.size() >= static_cast<size_t>(battleSpellSlots(gameState.player)))
                reason = "额外神通已满，请先卸下一个。";
            so.set("enabled", Value(reason.empty()));
            so.set("reason", str(reason));
            arr.push_back(std::move(so));
        };
        bool includesDefend = false;
        for (const auto& sp : allSpells()) {
            addSpell(sp);
            includesDefend = includesDefend || sp.id == "defend";
        }
        if (!includesDefend) addSpell(*battleSpell("defend"));
        o.set("spells", Value(std::move(arr)));
        o.set("max", num(battleSpellSlots(gameState.player)));
        o.set("manaRegen", num(battleEnergy(gameState.player)));
        o.set("carriedCount", num(static_cast<int>(gameState.player.arts.spells.size())));
        return jsonOk(o);
    }

    // ---- 十二大道的全部节点定义（含名称与效果说明）----
    // 前端要能把「金道入门：金系伤害 +10%」这类信息显示出来，
    // 所以这里把 60 个节点的定义整份发过去。
    if (route == RouteId::ArtsNodes) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& n : artNodes()) {
            Value no(json::Object{});
            const std::string nid =
                std::to_string(n.dao) + "-" + std::to_string(n.level);
            no.set("id", str(nid));
            no.set("dao", str(ART_DAOS[n.dao]));
            no.set("daoIndex", num(n.dao));
            no.set("level", num(n.level));
            no.set("cost", num(n.cost));
            no.set("name", str(n.name));
            no.set("desc", str(n.desc));
            no.set("unlocked",
                   Value(std::find(gameState.player.arts.nodes.begin(),
                                   gameState.player.arts.nodes.end(),
                                   nid) != gameState.player.arts.nodes.end()));
            arr.push_back(std::move(no));
        }
        o.set("nodes", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 参悟大道（30 天）----
    if (route == RouteId::ArtsMeditate) {
        const std::string dao = strOf(in, "dao");
        const std::string err = artAnnualError(gameState, "meditate", dao);
        if (!err.empty()) return jsonError(err);
        // 参悟是一段 30 天的工作，走正常的在途工作流程
        const EcoActionInfo info = ecoActionInfo(gameState, "meditate", dao);
        if (!info.valid) return jsonError("无法参悟此道。");
        ecoBegin(gameState, "meditate", dao);
        advance(gameState, info.days);
        return jsonOk(okWithState(gameState));
    }

    // ---- 师徒与道侣日常 ----
    if (route == RouteId::Bond) {
        const std::string kind = strOf(in, "kind");
        const std::string err = bondActionError(gameState, kind);
        if (!err.empty()) return jsonError(err);
        const BondActionInfo info = bondActionInfo(gameState, kind);
        if (!info.valid) return jsonError("没有这项师门往来。");
        societyBondBegin(gameState, kind);
        if (gameState.job.has_value()) advance(gameState, info.days);
        return jsonOk(okWithState(gameState));
    }

    // ---- 宗门与知交 ----
    if (route == RouteId::Stipend) {
        const StipendResult r = socCollectStipend(gameState);
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::Story) {
        const std::string id = strOf(in, "id");
        const int choice = intOf(in, "choice", 0);
        if (choice < 0 || choice > 1) return jsonError("请选择这段故事提供的选项。");
        for (const auto& st : data::StoryDef_TABLE()) {
            if (st.id != id) continue;
            const std::string err = storyError(gameState, &st);
            if (!err.empty()) return jsonError(err);
            const std::string choiceError = storyChoiceError(gameState, st, choice);
            if (!choiceError.empty()) return jsonError(choiceError);
            const auto beginError = storyBegin(gameState, st, choice);
            if (!beginError.empty()) return jsonError(beginError);
            advance(gameState, st.days);
            return jsonOk(okWithState(gameState));
        }
        return jsonError("没有这段机缘。");
    }
    if (route == RouteId::Wild) {
        const std::string id = strOf(in, "id");
        const std::string err = wildActionError(gameState, id);
        if (!err.empty()) return jsonError(err);
        societyWildBegin(gameState, id);
        advance(gameState, gameState.job->total);
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::Expedition) {
        const std::string err = adventureActionError(gameState, "backhill");
        if (!err.empty()) return jsonError(err);
        adventureBegin(gameState, "backhill");
        advance(gameState, 7);
        return jsonOk(okWithState(gameState));
    }

    // ---- 移动 ----
    if (route == RouteId::Travel) {
        const auto modeField = in.asObject().find("mode");
        const auto beginField = in.asObject().find("begin");
        if (modeField != in.asObject().end() && !modeField->second.isString())
            return jsonError("赶路方式 mode 必须是字符串。");
        if (beginField != in.asObject().end() && !beginField->second.isBool())
            return jsonError("赶路参数 begin 必须是布尔值。");
        const std::string dest = strOf(in, "to");
        const std::string mode = strOf(in, "mode", "boat");
        const string error = travelBegin(gameState, dest, mode);
        if (!error.empty()) return jsonError(error);
        AdvanceReport rep;
        if (in["begin"].asBool(false)) rep.stopReason = "started";
        else rep = advanceEx(gameState, gameState.job->remaining, true);
        const bool arrived = rep.stopReason != "restored" && !gameState.ended && gameState.player.location == dest &&
                             (!gameState.job || gameState.job->actionType != "travel");
        Value out = okWithState(gameState);
        out.set("arrived", Value(arrived));
        out.set("text", str(rep.stopReason == "restored" ? "赶路途中死亡，已恢复最近存档；原行程已中止。" :
                         arrived ? "已抵达" + string(findLoc(dest)->name) + "。" :
                         gameState.ended ? "赶路途中此世已结束，未抵达目的地。" :
                         gameState.pending ? "赶路暂停，请先处理抉择。" :
                         gameState.player.arts.battleActive() ? "赶路暂停，请先完成斗法。" :
                         "已启程，请继续推进这段行程。"));
        Value report(json::Object{});
        report.set("daysPassed", num(rep.daysPassed));
        report.set("stopReason", str(rep.stopReason));
        json::Array highlights;
        for (const auto& line : rep.highlights) highlights.push_back(str(line));
        report.set("highlights", Value(std::move(highlights)));
        out.set("report", std::move(report));
        return jsonOk(out);
    }

    // ---- 营生 ----
    if (route == RouteId::Task) {
        const std::string id = strOf(in, "id", "errand");
        const std::string err = ecoActionError(gameState, "task", id);
        if (!err.empty()) return jsonError(err);
        ecoBegin(gameState, "task", id);
        advance(gameState, ecoActionInfo(gameState, "task", id).days);
        return jsonOk(okWithState(gameState));
    }

    // ---- 工坊缺料采购：预览与实际购买共用同一份报价 ----
    if (route == RouteId::CraftMaterialsQuote || route == RouteId::CraftMaterialsBuy) {
        if (!in["expectedCost"].isNull() && in["expectedCost"].asNumber() < 0) return jsonError("expectedCost 必须为非负整数。");
        const string kind=strOf(in,"kind",strOf(in,"type"));
        string id=strOf(in,"id");
        if(kind=="forge" && id.empty()) id=strOf(in,"main")+"|"+strOf(in,"aux")+"|"+strOf(in,"core")+"|"+strOf(in,"shape","sword");
        const int batch=intOf(in,"batch",1);
        const auto quote=crafting::materialQuote(gameState,kind,id,batch);
        if(route==RouteId::CraftMaterialsQuote) {
            Value o(json::Object{}); o.set("quote",crafting::materialQuoteJson(quote)); return jsonOk(o);
        }
        const auto error=crafting::buyMaterials(gameState,kind,id,batch,intOf(in,"expectedCost",-1));
        if(!error.empty()) return jsonError(error);
        Value o=okWithState(gameState); o.set("quote",crafting::materialQuoteJson(quote));
        o.set("text",str("购齐所选配方缺少的材料，消耗"+std::to_string(quote.totalCost)+"灵石。")); return jsonOk(o);
    }

    // ---- 坊市买卖 ----
    if (route == RouteId::Buy) {
        const std::string kind = strOf(in, "kind");
        const std::string id = strOf(in, "id");
        const int count = intOf(in, "count", 1);
        if (count < 1 || count > 1000) return jsonError("购买数量需为 1 到 1000。");
        const std::string err = play::marketBuy(gameState, kind, id, count);
        if (!err.empty()) return jsonError(err);
        return jsonOk(okWithState(gameState));
    }

    if (route == RouteId::LocationService) {
        if (!in["begin"].isNull() && !in["begin"].isBool()) return jsonError("begin 必须为布尔值。");
        std::string tab;
        const auto error = locationServiceBegin(gameState, strOf(in, "id"), tab);
        if (!error.empty()) return jsonError(error);
        if (gameState.job && !in["begin"].asBool()) advanceEx(gameState, gameState.job->remaining, true);
        Value o = okWithState(gameState); o.set("targetTab", str(tab)); return jsonOk(o);
    }
    if (route == RouteId::NpcLearn) {
        if (!in["begin"].isNull() && !in["begin"].isBool()) return jsonError("begin 必须为布尔值。");
        const auto error = npcLearnBegin(gameState, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!error.empty()) return jsonError(error);
        if (gameState.job && !in["begin"].asBool()) advanceEx(gameState, gameState.job->remaining, true);
        return jsonOk(okWithState(gameState));
    }
    // ---- NPC 往来 ----
    if (route == RouteId::Social) {
        const std::string npcId = strOf(in, "npc");
        const std::string act = strOf(in, "action");
        int days = 0;
        for (const auto& action : play::socialActions(gameState, npcId))
            if (action.id == act) days = action.days;
        const play::SocialResult r = play::socialDo(gameState, npcId, act);
        if (!r.ok) return jsonError(r.error);
        if (days > 0 && !gameState.ended) {
            Job job;
            job.actionType = "socialElapsed";
            job.name = "道友往来";
            job.total = days;
            job.remaining = days;
            job.started = gameState.clock.days;
            gameState.job = job;
            if (!gameState.player.arts.battle) advance(gameState, days);
        }
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 某个人的可用往来（前端点开某人时拉取）----
    if (route == RouteId::SocialActions) {
        const std::string npcId = strOf(in, "npc");
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& action : play::socialActions(gameState, npcId)) {
            Value ao(json::Object{});
            ao.set("id", str(action.id));
            ao.set("name", str(action.name));
            ao.set("desc", str(action.desc));
            ao.set("days", num(action.days));
            ao.set("cost", num(action.cost));
            ao.set("enabled", Value(action.enabled));
            ao.set("reason", str(action.reason));
            arr.push_back(std::move(ao));
        }
        o.set("actions", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 炼丹：使用同一套配料、耗时和在途工作规则 ----
    if (route == RouteId::Craft) {
        const std::string kind = strOf(in, "kind", "brew");
        const std::string id = strOf(in, "id");
        const int batch = intOf(in, "batch", 1);
        if (kind != "brew") return jsonError("炼器请在工坊选择材料与器型。");
        const std::string err = ecoActionError(gameState, kind, id, batch);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(gameState, kind, id, batch);
        if (!info.valid) return jsonError("无法炼制此丹。");
        ecoBegin(gameState, kind, id, batch);
        advance(gameState, info.days);
        Value o = okWithState(gameState);
        o.set("text", str("已安排「" + info.name + "」，共 " +
            std::to_string(batch) + " 炉，需 " + std::to_string(info.days) + " 天。"));
        return jsonOk(o);
    }

    // ---- 装备法器 ----
    if (route == RouteId::Equip) {
        const std::string gearId = strOf(in, "id");
        const ActionError result = artInstant(gameState, "equip", gearId);
        if (!result.ok) return jsonError(result.error);
        return jsonOk(okWithState(gameState));
    }

    // ---- 服用丹药 ----
    if (route == RouteId::UsePill) {
        const string error = artUsePill(gameState, strOf(in, "id"));
        if (!error.empty()) return jsonError(error);
        return jsonOk(okWithState(gameState));
    }

    // ---- 传承档案 ----
    // 存在 build/legacy.json，跨存档共享（对应 JS 的 localStorage）
    // ---- 拜入宗门（自选五宗）----
    // 之前唯一入宗途径是第 3 年的「山门来信」，而且只能进金虹剑派。
    // 26 部拓展功法全靠宗门藏经阁，等于大半养成线被卡死。
    if (route == RouteId::Join) {
        const std::string sid = strOf(in, "sect");
        const string error = sectTrialBegin(gameState, sid);
        if (!error.empty()) return jsonError(error);
        return jsonOk(okWithState(gameState));
    }

    // ---- 可拜入的宗门清单 ----
    if (route == RouteId::Sects) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& s : SECTS()) {
            Value so(json::Object{});
            so.set("id", str(s.id));
            so.set("name", str(s.name));
            so.set("element", str(s.element));
            so.set("motto", str(s.motto));
            so.set("description", str(s.description));
            auto it = gameState.world.sects.find(s.id);
            so.set("members", num(it == gameState.world.sects.end() ? 0 : it->second.members));
            so.set("influence", num(it == gameState.world.sects.end() ? 0 : it->second.influence));
            so.set("treasury", num(it == gameState.world.sects.end() ? 0 : it->second.treasury));
            const bool femaleOnly = (std::string(s.id) == "xinghe");
            so.set("femaleOnly", Value(femaleOnly));
            const string why = sectTrialError(gameState, s.id);
            if (const auto* def = findSectTrial(s.id)) {
                Value trial(json::Object{});
                trial.set("name", str(def->name));
                trial.set("desc", str(def->desc));
                trial.set("style", str(def->style));
                trial.set("cost", num(def->fee));
                trial.set("days", num(def->days));
                trial.set("minStage", num(def->minStage));
                trial.set("stageName", str(realmName(def->minStage)));
                so.set("trial", std::move(trial));
            }
            so.set("enabled", Value(why.empty()));
            so.set("reason", str(why));
            so.set("current", Value(gameState.player.sect == s.id));
            arr.push_back(std::move(so));
        }
        o.set("sects", Value(std::move(arr)));
        o.set("masterRequirement", num(renown::masterRequirement()));
        o.set("mentorAffinityNeed", num(40));
        o.set("sectTaskUsed", num(sectTasksTakenThisMonth(gameState))); o.set("sectTaskLimit", num(3));
        json::Array mentors;
        for (const auto* npc : sectMentorCandidates(gameState)) {
            Value item(json::Object{});
            item.set("id", str(npc->id)); item.set("name", str(npc->name)); item.set("stage", num(npc->stage));
            item.set("realmName", str(realmName(npc->stage))); item.set("location", str(npc->location));
            const auto* location = findLoc(npc->location);
            item.set("locationName", str(location ? location->name : npc->location));
            const auto relation = gameState.player.relations.find(npc->id);
            item.set("relation", num(relation == gameState.player.relations.end() ? 0 : relation->second));
            item.set("isMaster", Value(gameState.bonds.master == npc->id));
            const string reason = sectMasterError(gameState, npc->id);
            item.set("enabled", Value(reason.empty())); item.set("reason", str(reason));
            item.set("portrait", str(npcPortrait(*npc)));
            item.set("cost", num(20)); item.set("days", num(5));
            mentors.push_back(std::move(item));
        }
        o.set("mentors", Value(std::move(mentors)));
        return jsonOk(o);
    }

    // ---- 成就（14 项）----
    // artAchievements() 早就写好了，但从来没有调用点，
    // 所以 achievementTexts 一直是空的，14 个成就完全不可得。
    if (route == RouteId::Achievements) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& achievement : data::AchievementDef_TABLE()) {
            const int prog = artProgress(gameState.player, achievement.key);
            const bool got = prog >= achievement.target;
            Value ao(json::Object{});
            ao.set("id", str(achievement.id));
            ao.set("name", str(achievement.name));
            ao.set("key", str(achievement.key));   // AchievementDef 没有 desc，用 key 代替展示
            ao.set("key", str(achievement.key));
            ao.set("target", num(achievement.target));
            ao.set("progress", num(prog));
            ao.set("points", num(achievement.points));
            ao.set("done", Value(got));
            arr.push_back(std::move(ao));
        }
        o.set("achievements", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 拜入宗门（自选五宗）占位结束 ----
    if (route == RouteId::Legacy) {
        const legacy::Archive archive = legacyLoad();
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("earned", num(archive.earned));
        o.set("runs", num(archive.runs));
        o.set("budget", num(legacy::talentBudget(archive)));
        o.set("basePoints", num(BASE_POINTS));
        o.set("maxPoints", num(MAX_POINTS));
        {
            json::Array arr;
            for (const auto& m : legacy::milestonesOf(gameState, archive)) {
                Value mo(json::Object{});
                mo.set("key", str(m.key));
                mo.set("label", str(m.label));
                mo.set("points", num(m.points));
                mo.set("reached", Value(m.reached));
                mo.set("claimed", Value(m.alreadyHad));
                arr.push_back(std::move(mo));
            }
            o.set("milestones", Value(std::move(arr)));
        }
        if (gameState.ending.has_value()) {
            Value e(json::Object{});
            e.set("cause", str(gameState.ending->cause));
            e.set("age", num(gameState.ending->age));
            e.set("realm", str(gameState.ending->realm));
            e.set("friends", num(gameState.ending->friends));
            e.set("score", num(gameState.ending->score));
            const bool claimed = std::find(archive.claimed.begin(),archive.claimed.end(),gameState.lifeId)!=archive.claimed.end();
            e.set("settled", Value(claimed));
            e.set("canRestore",Value(gameState.ending->cause!="寿终正寝" && gameState.checkpoint.enabled && !claimed));
            o.set("ending", std::move(e));
        }
        return jsonOk(o);
    }

    // 结算这一世，换取传承点
    if (route == RouteId::LegacySettle) {
        legacy::Archive archive = legacyLoad();
        const legacy::SettleResult r = legacy::settle(gameState, archive);
        if (!r.eligible) return jsonError(r.note);
        if (!legacySave(archive)) return jsonError("传承档案保存失败，请检查存档目录是否可写。", 500);
        gameState.checkpoint.enabled=false;
        gameState.checkpoint.snapshot.clear();
        Value o = okWithState(gameState);
        o.set("points", num(r.points));
        o.set("earned", num(archive.earned));
        o.set("budget", num(legacy::talentBudget(archive)));
        {
            json::Array arr;
            for (const auto& s : r.reasons) arr.push_back(str(s));
            o.set("reasons", Value(std::move(arr)));
        }
        return jsonOk(o);
    }

    // 建号抽天赋：抽 12 项候选，roll 是重抽次数
    if (route == RouteId::LegacyDraft) {
        const uint32_t seed = seedOf(in);
        const int roll = intOf(in, "roll", 0);
        if (roll < 0 || roll > 2) return jsonError("天赋最多重抽两次。");
        json::Array arr;
        for (const auto& id : legacy::draft(seed, roll)) {
            Value o(json::Object{});
            o.set("id", str(id));
            const TalentDef* t = findTalent(id);
            o.set("name", str(t ? t->name : id));
            o.set("tag", str(t ? t->tag : ""));
            o.set("cost", num(t ? t->cost : 0));
            o.set("desc", str(t ? t->desc : ""));
            arr.push_back(std::move(o));
        }
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("talents", Value(std::move(arr)));
        const RestartLegacy inheritance = restartLegacy(gameState);
        o.set("budget", num(legacy::talentBudget(inheritance.archive)));
        o.set("pendingLegacyPoints", num(inheritance.result.points));
        return jsonOk(o);
    }

    // ---- 野外冲突：拦路索财 / 野外截杀 ----
    // ---- 野外冲突：拦路索财 / 野外截杀 ----
    if (route == RouteId::Conflict) {
        const std::string npcId = strOf(in, "npc");
        const std::string intent = strOf(in, "intent", "rob");
        const conflict::ConflictResult r =
            conflict::doConflict(gameState, npcId, intent);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("response", str(r.responseText));
        o.set("notoriety", num(r.notoriety));
        return jsonOk(o);
    }

    // ---- 对方可能的反应（给界面做预告）----
    if (route == RouteId::ConflictPreview) {
        const std::string npcId = strOf(in, "npc");
        const std::string intent = strOf(in, "intent", "rob");
        const std::string err = conflict::conflictError(gameState, npcId, intent);
        Value o(json::Object{});
        o.set("ok", Value(err.empty()));
        if (!err.empty()) {
            o.set("error", str(err));
            return jsonOk(o);
        }
        for (const auto& n : gameState.npcs) {
            if (n.id != npcId) continue;
            o.set("response", str(conflict::response(gameState, n, intent)));
            o.set("name", str(n.name));
            o.set("stones", num(n.stones));
            o.set("herbs", num(n.herbs));
            break;
        }
        return jsonOk(o);
    }

    // ---- 通用营生行动（对应 JS 的 economy 年度行动）----
    // commission / hunt / mine / arena / quest / forge 的规则层早就写好了，
    // 但一直没有接口，属于「函数在、入口不在」。这里统一接上。
    if (route == RouteId::Eco) {
        const std::string type = strOf(in, "type");
        const std::string id = strOf(in, "id");
        const int batch = intOf(in, "batch", 1);
        const std::string err = ecoActionError(gameState, type, id, batch);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(gameState, type, id, batch);
        if (!info.valid) return jsonError("无法进行此项行动。");
        ecoBegin(gameState, type, id, batch);
        advance(gameState, info.days);
        Value o = okWithState(gameState);
        o.set("text", str("已安排「" + info.name + "」，用时 " +
                          std::to_string(info.days) + " 天。"));
        return jsonOk(o);
    }

    if (route == RouteId::ForgePreview) {
        const string main = strOf(in, "main"), aux = strOf(in, "aux"), core = strOf(in, "core"), shape = strOf(in, "shape", "sword");
        const ForgeInfo forge = artForgeInfo(gameState.player, main, aux, core, shape);
        const string id = main + "|" + aux + "|" + core + "|" + shape;
        const string reason = forge.valid ? ecoActionError(gameState, "forge", id) : "请选择有效的主料、辅料、灵核与器型。";
        const EcoActionInfo info = forge.valid ? ecoActionInfo(gameState, "forge", id) : EcoActionInfo{};
        Value result(json::Object{}), o(json::Object{});
        o.set("valid", Value(forge.valid)); o.set("grade", num(forge.grade)); o.set("cost", num(forge.cost));
        o.set("days", num(info.days)); o.set("bonus", num(forge.bonus)); o.set("chance", num(forge.chance));
        o.set("shapeId", str(forge.shapeId)); o.set("shapeName", str(forge.shapeName)); o.set("shapeSlot", str(forge.shapeSlot));
        o.set("mainName", str(forge.mainName)); o.set("mainElement", str(forge.mainElement)); o.set("mainPolarity", str(forge.mainPolarity));
        o.set("enabled", Value(forge.valid && reason.empty())); o.set("reason", str(reason));
        json::Array materials, blockers;
        const int stonesRequired = forge.valid ? forge.cost : 0;
        const int stonesOwned = std::max(0, gameState.player.stones);
        const int stonesMissing = std::max(0, stonesRequired - stonesOwned);
        o.set("stonesRequired", num(stonesRequired)); o.set("stonesOwned", num(stonesOwned));
        o.set("stonesMissing", num(stonesMissing));
        if (forge.valid) {
            for (const auto& material : artForgeMaterials(gameState.player, main, aux, core)) {
                Value item(json::Object{}); json::Array roles;
                item.set("id", str(material.id)); item.set("name", str(material.name));
                item.set("required", num(material.required)); item.set("owned", num(material.owned));
                item.set("missing", num(material.missing));
                for (const auto& role : material.roles) roles.push_back(str(role));
                item.set("roles", Value(std::move(roles))); materials.push_back(std::move(item));
                if (material.missing > 0)
                    blockers.push_back(str(material.name + "不足：需 " + std::to_string(material.required) +
                                           " 份，持有 " + std::to_string(material.owned) + " 份，缺 " +
                                           std::to_string(material.missing) + " 份。"));
            }
            if (stonesMissing > 0)
                blockers.push_back(str("灵石不足：需 " + std::to_string(stonesRequired) + " 枚，持有 " +
                                       std::to_string(stonesOwned) + " 枚，缺 " +
                                       std::to_string(stonesMissing) + " 枚。"));
        }
        // 保留原行动校验优先级；资源缺口单独完整列出，事件等门槛不能被足料覆盖。
        if (!reason.empty() && (blockers.empty() ||
            (reason.find("材料不足") == string::npos && reason.find("灵石不足") == string::npos)))
            blockers.insert(blockers.begin(), str(reason));
        o.set("materials", Value(std::move(materials))); o.set("blockers", Value(std::move(blockers)));
        Gear projected; projected.id = "preview"; projected.crafted = forge.valid; projected.shape = forge.shapeId;
        projected.name = forge.mainName + forge.shapeName; projected.slot = forge.shapeSlot; projected.grade = forge.grade;
        projected.element = forge.mainElement; projected.polarity = forge.mainPolarity; projected.bonus = forge.bonus;
        o.set("ability", view::artifactView(gameState.player, artifactAbility(projected)));
        if (forge.valid) {
            o.set("passive", view::artifactPassiveView(gameState.player, projected));
            projected.inscribed = true;
            o.set("inscribedAbility", view::artifactView(gameState.player, artifactAbility(projected)));
            o.set("inscribedPassive", view::artifactPassiveView(gameState.player, projected));
            o.set("inscribeCost", num(30 * forge.grade));
            o.set("inscribeRequirement", str("器道入门；成器后花费30×品阶灵石铭刻。"));
        }
        result.set("ok", Value(true)); result.set("info", std::move(o)); return jsonOk(result);
    }

    // ---- 炼器（要选主料/辅料/灵核/器型）----
    if (route == RouteId::Forge) {
        const std::string mainId = strOf(in, "main");
        const std::string auxId = strOf(in, "aux");
        const std::string coreId = strOf(in, "core");
        const std::string shapeId = strOf(in, "shape", "sword");
        const std::string id =
            mainId + "|" + auxId + "|" + coreId + "|" + shapeId;
        const std::string err = ecoActionError(gameState, "forge", id);
        if (!err.empty()) return jsonError(err);
        const EcoActionInfo info = ecoActionInfo(gameState, "forge", id);
        if (!info.valid) return jsonError("炼器配料无效。");
        ecoBegin(gameState, "forge", id);
        advance(gameState, info.days);
        return jsonOk(okWithState(gameState));
    }

    // ---- 炼器可选项（材料 / 器型）----
    if (route == RouteId::ForgeOptions) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        {
            json::Array arr;
            for (const auto& mt : data::MaterialDef_TABLE()) {
                Value mo(json::Object{});
                mo.set("id", str(mt.id));
                mo.set("name", str(mt.name));
                mo.set("grade", num(mt.grade));
                mo.set("element", str(mt.element));
                mo.set("polarity", str(mt.polarity));
                auto it = gameState.player.arts.materials.find(mt.id);
                mo.set("owned", num(it == gameState.player.arts.materials.end() ? 0 : it->second));
                mo.set("price", num(mt.price));
                mo.set("buyable", Value(ordinaryMaterialError(gameState.player,mt.grade).empty()));
                mo.set("source", str(mt.grade <= 2 ? "普通坊市、野外采矿" : "天机阁定向材料秘境，通关保底十份"));
                arr.push_back(std::move(mo));
            }
            o.set("materials", Value(std::move(arr)));
        }
        {
            json::Array arr;
            for (const auto& sh : data::ShapeDef_TABLE()) {
                Value so(json::Object{});
                so.set("id", str(sh.id));
                so.set("name", str(sh.name));
                so.set("slot", str(sh.slot));
                arr.push_back(std::move(so));
            }
            o.set("shapes", Value(std::move(arr)));
        }
        return jsonOk(o);
    }

    // ---- 机缘（12 项限时机缘）----
    if (route == RouteId::Quest) {
        const std::string qid = strOf(in, "id");
        const data::QuestDef* q = nullptr;
        for (const auto& argumentValue : data::QuestDef_TABLE())
            if (argumentValue.id == qid) q = &argumentValue;
        if (!q) return jsonError("没有这项机缘。");
        const std::string err = artQuestError(gameState, q);
        if (!err.empty()) return jsonError(err);
        artAnnual(gameState, "quest", qid);
        return jsonOk(okWithState(gameState));
    }

    // ---- 当前可参与的行动清单（供界面直接渲染）----
    if (route == RouteId::EcoOptions) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        // 任务、委托、猎妖、寻矿、演武、机缘
        // quest 单独在下面按 12 项机缘列，不要在这里重复
        const char* types[] = {"task", "commission", "hunt", "mine", "arena"};
        for (const char* ty : types) {
            const std::string type(ty);
            if (type == "task") {
                // 城中差事及与 sectTaskDefs 共用定义的宗门差事。
                for (const char* tid : {"errand", "escort", "inspect", "sect", "sect-patrol", "sect-records", "sect-array"}) {
                    const std::string err = ecoActionError(gameState, "task", tid);
                    const EcoActionInfo info = ecoActionInfo(gameState, "task", tid);
                    Value eo(json::Object{});
                    eo.set("type", str("task"));
                    eo.set("id", str(tid));
                    eo.set("name", str(info.name));
                    eo.set("desc", str(std::string(tid) == "errand" ? "替商铺递送书信、整理账簿"
                                    : std::string(tid) == "escort" ? "护送往来商旅，有小概率受伤"
                                    : std::string(tid) == "inspect" ? "巡检灵阵，考验阵道见识"
                                                                    : "山门差事，积累宗门贡献"));
                    eo.set("days", num(info.days));
                    eo.set("pay", num(info.fee));
                    eo.set("enabled", Value(err.empty()));
                    eo.set("reason", str(err));
                    if (const auto* task = findSectTask(tid)) {
                        eo.set("desc", str(task->desc)); eo.set("sect", Value(true));
                        eo.set("contribution", num(task->contribution));
                        const auto* sect = findSect(gameState.player.sect); eo.set("dao", str(sect ? sect->element : ""));
                        Player rewardPreview = gameState.player;
                        if (sect && task->daoExp > 0) artGain(rewardPreview, sect->element, task->daoExp);
                        const auto expBefore = sect ? gameState.player.arts.exp.find(sect->element) : gameState.player.arts.exp.end();
                        const double originalExp = expBefore == gameState.player.arts.exp.end() ? 0 : expBefore->second;
                        eo.set("daoExp", num(sect ? jsRound(rewardPreview.arts.exp[sect->element] - originalExp) : 0));
                        eo.set("daoPoints", num(rewardPreview.arts.points - gameState.player.arts.points));
                        eo.set("monthlyUsed", num(sectTasksTakenThisMonth(gameState))); eo.set("monthlyLimit", num(3));
                    }
                    arr.push_back(std::move(eo));
                }
            } else if (type == "commission") {
                for (const char* mode : {"brew", "forge"}) {
                    const std::string err = ecoActionError(gameState, "commission", mode);
                    const EcoActionInfo info = ecoActionInfo(gameState, "commission", mode);
                    Value eo(json::Object{});
                    eo.set("type", str("commission"));
                    eo.set("id", str(mode));
                    eo.set("name", str(info.name.empty()
                        ? (std::string("商会委托·") + (std::string(mode) == "brew" ? "炼丹" : "炼器"))
                        : info.name));
                    eo.set("desc", str(std::string(mode) == "brew"
                        ? "代工炼丹，交付后领工费"
                        : "代工炼器，交付后领工费"));
                    eo.set("days", num(info.days));
                    eo.set("pay", num(info.fee));
                    eo.set("cost", num(info.cost));
                    eo.set("chance", num(info.chance));
                    eo.set("enabled", Value(err.empty()));
                    eo.set("reason", str(err));
                    arr.push_back(std::move(eo));
                }
            } else {
                const std::string err = ecoActionError(gameState, type, "");
                const EcoActionInfo info = ecoActionInfo(gameState, type, "");
                if (!info.valid && err.empty()) continue;
                Value eo(json::Object{});
                eo.set("type", str(type));
                eo.set("id", str(""));
                eo.set("name", str(info.name));
                eo.set("desc", str(type == "hunt" ? "与妖兽斗法，胜后得赏金与材料"
                                : type == "mine" ? "开采炼器矿材"
                                : type == "arena" ? "演武切磋，增进剑道与斗法心得"
                                                  : "限时机缘"));
                eo.set("days", num(info.days));
                eo.set("pay", num(info.fee));
                eo.set("cost", num(info.cost));
                eo.set("enabled", Value(err.empty()));
                eo.set("reason", str(err));
                arr.push_back(std::move(eo));
            }
        }
        // 机缘单独列（12 项各有条件）
        for (const auto& q : data::QuestDef_TABLE()) {
            const std::string err = artQuestError(gameState, &q);
            const bool done = gameState.player.arts.quests.count(q.id) > 0;
            Value eo(json::Object{});
            eo.set("type", str("quest"));
            eo.set("id", str(q.id));
            eo.set("name", str(q.name));
            eo.set("desc", str(q.desc));
            eo.set("days", num(0));   // 机缘本身不占固定天数
            eo.set("reward", str(q.reward));
            eo.set("done", Value(done));
            eo.set("enabled", Value(!done && err.empty()));
            eo.set("reason", str(done ? "已完成" : err));
            arr.push_back(std::move(eo));
        }
        o.set("options", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 坊市出售 ----
    if (route == RouteId::Sell) {
        const shop::SellResult r =
            shop::sell(gameState, strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 灵核 / 凝元丹 交易 ----
    if (route == RouteId::TradeStock) {
        const shop::SellResult r = shop::tradeStock(
            gameState, strOf(in, "kind"), in["buy"].isBool() ? in["buy"].asBool() : true,
            intOf(in, "count", 1));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 书坊买功法 ----
    if (route == RouteId::BookBuy) {
        const shop::SellResult r = shop::bookBuy(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 藏经阁兑换 ----
    if (route == RouteId::Library) {
        const shop::SellResult r = shop::libraryExchange(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 法器：卸下 / 分解 / 铭刻 ----
    if (route == RouteId::LibrarySpell) {
        const auto result = shop::librarySpellExchange(gameState,strOf(in,"id"));
        if (!result.ok) return jsonError(result.error);
        Value o=okWithState(gameState);
        o.set("text",str(result.text));
        return jsonOk(o);
    }

    if (route == RouteId::GearUnequip) {
        const shop::SellResult r = shop::unequip(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        return jsonOk(okWithState(gameState));
    }
    if (route == RouteId::GearSalvage) {
        const shop::SellResult r = shop::salvage(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }
    if (route == RouteId::GearInscribe) {
        const shop::SellResult r = shop::inscribe(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 丹炉：修炉 / 换炉 ----
    if (route == RouteId::FurnaceRepair) {
        const shop::SellResult r = shop::repairFurnace(gameState);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }
    if (route == RouteId::FurnaceBuy) {
        const shop::SellResult r = shop::buyFurnace(gameState, strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 中止当前安排 ----
    if (route == RouteId::CancelJob) {
        if (gameState.job && gameState.job->actionType == "socialElapsed")
            return jsonError("此次往来已结算，请完成剩余的约定天数。");
        const shop::SellResult r = shop::cancelJob(gameState);
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        return jsonOk(o);
    }

    // ---- 道友往来（好感度 / 交情 / 送礼 / 讨要）----
    // 这几项都不消耗天数：轻交互即时完成，重交互（切磋/请教/游历/论道）才耗时。
    if (route == RouteId::Greet) {
        const friendship::GreetResult r = friendship::greet(gameState, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("firstTime", Value(r.firstTime));
        o.set("affinityDelta", num(r.affinityDelta));
        o.set("creditDelta", num(r.creditDelta));
        return jsonOk(o);
    }

    if (route == RouteId::Gift) {
        const friendship::GiftResult r = friendship::giftItem(
            gameState, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("affinityDelta", num(r.affinityDelta));
        o.set("creditDelta", num(r.creditDelta));
        return jsonOk(o);
    }

    if (route == RouteId::NpcBag) {
        const std::string npcId = strOf(in, "npc");
        Value o(json::Object{});
        o.set("ok", Value(true));
        o.set("credit", num(friendship::creditOf(gameState, npcId)));
        o.set("creditLabel",
              str(friendship::creditLabel(friendship::creditOf(gameState, npcId))));
        json::Array arr;
        for (const auto& b : friendship::npcBag(gameState, npcId)) {
            Value bo(json::Object{});
            bo.set("kind", str(b.kind));
            bo.set("id", str(b.id));
            bo.set("name", str(b.name));
            bo.set("desc", str(b.desc));
            bo.set("count", num(b.count));
            bo.set("value", num(b.value));
            bo.set("creditCost", num(b.creditCost));
            bo.set("affordable", Value(b.affordable));
            bo.set("reason", str(b.reason));
            arr.push_back(std::move(bo));
        }
        o.set("items", Value(std::move(arr)));
        return jsonOk(o);
    }

    if (route == RouteId::NpcTake) {
        const friendship::TradeResult r = friendship::takeFromNpc(
            gameState, strOf(in, "npc"), strOf(in, "kind"), strOf(in, "id"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("cost", num(r.cost));
        return jsonOk(o);
    }

    // ---- 兽潮 ----
    if (route == RouteId::Tide) {
        Value o(json::Object{});
        o.set("ok",Value(true));
        o.set("state",buildState(gameState));
        o.set("tide", view::tideState(gameState));
        return jsonOk(o);
    }

    if (route == RouteId::TideFight) {
        if (!in["battlefield"].isNull() && (intOf(in,"battlefield") < 0 || intOf(in,"battlefield") > 3))
            return jsonError("请选择炼气至元婴的有效战场。");
        const tide::FightResult r = tide::fight(gameState, intOf(in, "battlefield", -1));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("lethal", Value(r.lethal));
        return jsonOk(o);
    }

    if (route == RouteId::TideMarketBuy) {
        if (!in["expectedUnitPrice"].isNull() && intOf(in,"expectedUnitPrice") < 0)
            return jsonError("预计单价必须为非负整数。");
        const auto result=tide::buyMarket(gameState, strOf(in,"eventId"), strOf(in,"kind"),
            strOf(in,"id"), intOf(in,"count",1), intOf(in,"expectedUnitPrice",-1));
        if (!result.ok) return jsonError(result.error);
        Value out=okWithState(gameState); out.set("text",str(result.text)); out.set("cost",num(result.cost));
        return jsonOk(out);
    }

    if (route == RouteId::TideHunker) {
        const std::string txt = tide::hunkerDown(gameState);
        Value o = okWithState(gameState);
        o.set("text", str(txt));
        return jsonOk(o);
    }

    // ---- 通讯录 ----
    if (route == RouteId::Roster) {
        const bool strangers = in["strangers"].isBool() ? in["strangers"].asBool() : false;
        const std::string sortBy = strOf(in, "sort", "default");
        Value o(json::Object{});
        o.set("ok", Value(true));
        json::Array arr;
        for (const auto& e : play::roster(gameState, strangers, sortBy)) {
            Value ro(json::Object{});
            ro.set("id", str(e.id));
            ro.set("name", str(e.name));
            ro.set("gender", str(e.gender));
            ro.set("age", num(e.age));
            ro.set("stage", num(e.stage));
            ro.set("realmName", str(e.realmName));
            ro.set("sectName", str(e.sectName));
            ro.set("personality", str(e.personality));
            ro.set("goal", str(e.goal));
            ro.set("locationName", str(e.locationName));
            ro.set("relation", num(e.relation));
            ro.set("relationLabel", str(e.relationLabel));
            ro.set("here", Value(e.here));
            ro.set("isMaster", Value(e.isMaster));
            ro.set("isPartner", Value(e.isPartner));
            ro.set("isDisciple", Value(e.isDisciple));
            ro.set("sword", num(e.sword));
            ro.set("insight", num(e.insight));
            ro.set("apt", num(e.apt));
            ro.set("canMessage", Value(e.canMessage));
            ro.set("canInvite", Value(e.canInvite));
            arr.push_back(std::move(ro));
        }
        o.set("roster", Value(std::move(arr)));
        return jsonOk(o);
    }

    // ---- 传讯问候 ----
    if (route == RouteId::Message) {
        const play::MessageResult r = play::sendMessage(gameState, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 邀请对方前来 ----
    if (route == RouteId::Invite) {
        const play::MessageResult r = play::inviteToMe(gameState, strOf(in, "npc"));
        if (!r.ok) return jsonError(r.error);
        Value o = okWithState(gameState);
        o.set("text", str(r.text));
        o.set("relationDelta", num(r.relationDelta));
        return jsonOk(o);
    }

    // ---- 建号数据：全部天赋 + 五项属性 ----
    if (route == RouteId::BuildOptions) {
        Value o(json::Object{});
        o.set("ok", Value(true));
        const RestartLegacy inheritance = restartLegacy(gameState);
        o.set("budget", num(legacy::talentBudget(inheritance.archive)));
        o.set("pendingLegacyPoints", num(inheritance.result.points));
        o.set("maxPoints", num(MAX_POINTS));
        json::Array ts;
        for (const auto& t : play::allTalents()) {
            Value to(json::Object{});
            to.set("id", str(t.id));
            to.set("name", str(t.name));
            to.set("category", str(t.category));
            to.set("cost", num(t.cost));
            to.set("effect", str(t.effect));
            ts.push_back(std::move(to));
        }
        o.set("talents", Value(std::move(ts)));
        json::Array as;
        for (const auto& allocationOption : play::allocOptions({})) {
            Value ao(json::Object{});
            ao.set("id", str(allocationOption.id));
            ao.set("name", str(allocationOption.name));
            ao.set("desc", str(allocationOption.desc));
            ao.set("max", num(allocationOption.max));
            as.push_back(std::move(ao));
        }
        o.set("stats", Value(std::move(as)));
        // 先天气运：按地图种子掷出的初始资质/悟性。
        // 种子没给就现摇一个 —— 玩家可以点「换个种子」重掷。
        {
            const uint32_t seedIn = seedOf(in, 0);
            const unsigned seed =
                seedIn > 0 ? static_cast<unsigned>(seedIn)
                           : (static_cast<unsigned>(gameState.seed) ^
                              (static_cast<unsigned>(gameState.eventSeq + 1) * 2654435761u));
            const InnateRoll inn = rollInnate(seed);
            Value io(json::Object{});
            io.set("apt", num(inn.apt));
            io.set("insight", num(inn.insight));
            io.set("seed", num(static_cast<long long>(seed)));
            // 上下限一起下发：界面上要告诉玩家「还能涨到哪」
            io.set("aptMin", num(APT_MIN));
            io.set("aptMax", num(APT_MAX));
            io.set("aptRollMin", num(APT_ROLL_MIN));
            io.set("aptRollMax", num(APT_ROLL_MAX));
            io.set("insightMin", num(INSIGHT_MIN));
            io.set("insightMax", num(INSIGHT_MAX));
            io.set("insightRollMin", num(INSIGHT_ROLL_MIN));
            io.set("insightRollMax", num(INSIGHT_ROLL_MAX));
            o.set("innate", std::move(io));
        }
        return jsonOk(o);
    }

    // ---- 校验建号方案 ----
    if (route == RouteId::BuildCheck) {
        std::vector<std::string> traits;
        if (in["traits"].isArray())
            for (const auto& argumentValue : in["traits"].asArray())
                if (argumentValue.isString()) traits.push_back(argumentValue.asString());
        std::map<std::string, int> alloc;
        if (in["allocation"].isObject())
            for (const auto& [k, v] : in["allocation"].asObject())
                if (v.isNumber()) alloc[k] = v.asInt();
        const RestartLegacy inheritance = restartLegacy(gameState);
        const int budget = legacy::talentBudget(inheritance.archive);

        const play::BuildCheck c = play::checkBuild(traits, alloc, budget);
        Value o(json::Object{});
        o.set("ok", Value(true));       // 接口本身成功
        o.set("valid", Value(c.ok));    // 方案是否合法
        o.set("error", str(c.error));
        o.set("budget", num(c.budget));
        o.set("pendingLegacyPoints", num(inheritance.result.points));
        o.set("spent", num(c.spent));
        o.set("allocSpent", num(c.allocSpent));
        o.set("remain", num(c.remain));
        o.set("traitCount", num(c.traitCount));
        return jsonOk(o);
    }

    return jsonError("未知接口：" + path, 404);
}

} // namespace nz::api
