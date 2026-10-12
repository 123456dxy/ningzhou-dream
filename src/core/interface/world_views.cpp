#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/calendar.h"
#include "core/tide.h"
#include "core/travel.h"
#include "core/location_services.h"
#include "core/society.h"
#include "core/adventure.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

// 用途：追加天下行情人口任务与地点数据。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendWorldState(const Game& game, Value& state) {

    // ---- 时间 ----
    {
        const CalDate d = calendarDate(game);
        Value entry(json::Object{});
        entry.set("days", num(game.clock.days));
        entry.set("year", num(game.year));
        entry.set("month", num(d.month));
        entry.set("day", num(d.day));
        entry.set("left", num(d.left));
        entry.set("label", str(calendarLabel(game)));
        state.set("clock", std::move(entry));
    }

    // ---- 兽潮（全图事件）----
    {
        Value t(json::Object{});
        t.set("active", Value(game.tide.active));
        t.set("severity", num(game.tide.severity));
        t.set("monthsLeft", num(game.tide.monthsLeft));
        t.set("wavesFought", num(game.tide.wavesFought));
        t.set("wavesWon", num(game.tide.wavesWon));
        t.set("npcDeaths", num(game.tide.npcDeaths));
        t.set("dangerBonus", num(tide::dangerBonus(game)));
        t.set("status", str(tide::statusText(game)));
        json::Array fa;
        for (const auto& nm : game.tide.fallen) fa.push_back(str(nm));
        t.set("fallen", Value(std::move(fa)));
        state.set("tide", std::move(t));
    }

    // ---- 世界 ----
    {
        Value entry(json::Object{});
        entry.set("herbs", num(game.world.herbs));
        entry.set("coreStock", num(game.world.coreStock));
        entry.set("pillStock", num(game.world.pillStock));
        entry.set("market", num(game.world.market));
        entry.set("weather", str(game.world.weather));
        entry.set("mineOwner", str(game.world.mineOwner));
        entry.set("npcCount", num(static_cast<int>(game.npcs.size())));
        entry.set("marketCash", num(game.economy.marketCash));
        state.set("world", std::move(entry));
    }

    // ---- 游历 ----
    {
        const Adventure& adventure = game.adventure;
        Value entry(json::Object{});
        entry.set("since", num(adventure.since));
        entry.set("total", num(adventure.total));
        entry.set("events", num(adventure.events));
        entry.set("wins", num(adventure.wins));
        state.set("adventure", std::move(entry));
    }

    // ---- 纪事 ----
    {
        json::Array arr;
        const int npc = std::min<int>(60, static_cast<int>(game.logs.size()));
        for (int i = static_cast<int>(game.logs.size()) - npc;
             i < static_cast<int>(game.logs.size()); ++i) {
            const LogEntry& e = game.logs[static_cast<size_t>(i)];
            Value entry(json::Object{});
            entry.set("id", num(e.id));
            entry.set("year", num(e.year));
            entry.set("age", num(e.age));
            entry.set("days", num(e.days.value_or(e.year * 360)));
            entry.set("category", str(e.category));
            entry.set("text", str(e.text));
            entry.set("important", Value(e.important));
            entry.set("delta", str(e.delta));
            arr.push_back(std::move(entry));
        }
        // 反转，最新在前
        std::reverse(arr.begin(), arr.end());
        state.set("logs", Value(std::move(arr)));
    }

    // ---- 地点（舆图用）----
    {
        json::Array arr;
        for (const auto& location : LOCATIONS()) {
            Value entry(json::Object{});
            entry.set("id", str(location.id));
            entry.set("name", str(location.name));
            entry.set("type", str(location.type));
            entry.set("x", num(location.x));
            entry.set("y", num(location.y));
            entry.set("desc", str(location.description));
            entry.set("danger", num(location.danger));
            entry.set("here", Value(game.player.location == location.id));
            {
                json::Array services;
                for (const auto& item : locationServices(game, location.id)) {
                    Value service(json::Object{}); service.set("id", str(item.id));
                    service.set("name", str(item.name)); service.set("desc", str(item.desc));
                    service.set("days", num(item.days)); service.set("cost", num(item.cost));
                    service.set("enabled", Value(item.enabled)); service.set("reason", str(item.reason));
                    service.set("targetTab", str(item.targetTab));
                    services.push_back(std::move(service));
                }
                entry.set("services", Value(std::move(services)));
            }
            const TravelPlan plan = travelPlan(game, location.id);
            Value route(json::Object{});
            route.set("from", str(plan.from));
            route.set("to", str(plan.to));
            route.set("distance", num(plan.distance));
            route.set("escapeSpeed", num(plan.escapeSpeed));
            json::Array modes;
            for (const auto& option : plan.options) {
                Value mode(json::Object{});
                mode.set("id", str(option.id));
                mode.set("name", str(option.name));
                mode.set("days", num(option.days));
                mode.set("speed", num(option.speed));
                mode.set("ambushChance", num(option.ambushChance));
                mode.set("cost", num(option.cost));
                mode.set("enabled", Value(option.enabled));
                mode.set("reason", str(option.reason));
                mode.set("desc", str(option.desc));
                modes.push_back(std::move(mode));
            }
            route.set("options", Value(std::move(modes)));
            entry.set("travel", std::move(route));
            arr.push_back(std::move(entry));
        }
        state.set("locations", Value(std::move(arr)));
    }

    // ---- 可玩内容清单（供界面列出）----
    {
        json::Array arr;
        for (const auto& st : data::StoryDef_TABLE()) {
            const std::string err = storyError(game, &st);
            Value entry(json::Object{});
            entry.set("id", str(st.id));
            entry.set("name", str(st.name));
            entry.set("desc", str(st.desc));
            entry.set("days", num(st.days));
            entry.set("hidden", Value(st.hidden != 0));
            entry.set("available", Value(err.empty()));
            entry.set("reason", str(err));
            // 正文与两个选项：让前端能真正把故事读给玩家，
            // 而不是只显示一句简介
            entry.set("location", str(st.location));
            entry.set("opening", str(st.opening));
            entry.set("opt1Name", str(st.opt1Name));
            entry.set("opt1Effect", str(st.opt1Effect));
            const std::string firstError = err.empty() ? storyChoiceError(game, st, 0) : err;
            const std::string secondError = err.empty() ? storyChoiceError(game, st, 1) : err;
            entry.set("opt1Enabled", Value(firstError.empty())); entry.set("opt1Reason", str(firstError));
            entry.set("opt2Enabled", Value(secondError.empty())); entry.set("opt2Reason", str(secondError));
            entry.set("opt2Name", str(st.opt2Name));
            entry.set("opt2Effect", str(st.opt2Effect));
            entry.set("done", Value(game.bonds.stories.count(st.id) > 0));
            arr.push_back(std::move(entry));
        }
        state.set("stories", Value(std::move(arr)));
    }
    {
        json::Array arr;
        for (const auto& t : data::WildTaskDef_TABLE()) {
            Value entry(json::Object{});
            entry.set("id", str(t.id));
            entry.set("name", str(t.name));
            entry.set("desc", str(t.desc));
            entry.set("days", num(t.days));
            entry.set("fee", num(t.fee + (game.player.stage / 3) * 25));
            const std::string reason = wildActionError(game, t.id);
            entry.set("enabled", Value(reason.empty()));
            entry.set("reason", str(reason));
            arr.push_back(std::move(entry));
        }
        state.set("wildTasks", Value(std::move(arr)));
    }
}

} // namespace nz::api::view
