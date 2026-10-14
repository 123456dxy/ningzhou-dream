#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/interface/service_views.h"
#include "core/api_exploration.h"
#include "core/shop.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

// 用途：拼接完整客户端状态和领域子视图。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const ViewContext& context，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value state(const Game& game, const ViewContext& context) {
    Value state(json::Object{});
    const auto MarketContext = shop::mfMarketSaleContext(game);

    state.set("dungeons", dungeonView(game));
    state.set("cave", caveView(game));
    state.set("checkpoint", checkpointView(game));
    mfAppendServiceRoutes(game, state);
    appendPlayerState(game, state);
    appendArtsState(game, state);


    // ---- 计划与在途工作 ----
    state.set("plan", str(game.plan));
    if (game.job.has_value()) {
        Value entry(json::Object{});
        entry.set("type", str(game.job->actionType));
        entry.set("name", str(game.job->name));
        entry.set("remaining", num(game.job->remaining));
        entry.set("total", num(game.job->total));
        entry.set("from", str(game.job->origin.empty() && game.job->actionType == "travel" ?
                          game.player.location : game.job->origin));
        entry.set("to", str(game.job->destination));
        entry.set("mode", str(game.job->travelMode.empty() && game.job->actionType == "travel" ?
                          "boat" : game.job->travelMode));
        entry.set("elapsed", num(game.job->total - game.job->remaining));
        entry.set("cost", num(game.job->cost));
        state.set("job", std::move(entry));
    }

    // ---- 待处理抉择 ----
    if (game.pending.has_value()) {
        Value entry(json::Object{});
        entry.set("type", str(game.pending->type));
        entry.set("title", str(game.pending->title));
        entry.set("subtitle", str(game.pending->subtitle));
        entry.set("text", str(game.pending->text));
        json::Array opts;
        for (const auto& c : game.pending->options) {
            Value co(json::Object{});
            co.set("id", str(c.id));
            co.set("name", str(c.name));
            co.set("desc", str(c.desc));
            co.set("disabled", Value(c.disabled));
            opts.push_back(std::move(co));
        }
        entry.set("options", Value(std::move(opts)));
        state.set("pending", std::move(entry));
    }

    if (game.player.arts.battle) state.set("battle", battle(game));
    appendWorldState(game, state);
    mfAppendSocialState(game, state, MarketContext);
    mfAppendEconomyState(game, state, MarketContext);
    mfAppendEquipmentState(game, state, MarketContext);


    state.set("ended", Value(game.ended));

    // 结局详情：前端要显示「因何落笔、最终境界、评分」，
    // 之前只下发了一个布尔值，结算界面等于没有。
    if (game.ending.has_value()) {
        const Ending& e = *game.ending;
        Value entry(json::Object{});
        entry.set("cause", str(e.cause));
        const bool claimed = context.endingClaimed;
        entry.set("settled", Value(claimed));
        entry.set("canRestore",Value(e.cause!="寿终正寝" && game.checkpoint.enabled && !claimed));
        entry.set("year", num(e.year));
        entry.set("age", num(e.age));
        entry.set("realm", str(e.realm));
        entry.set("friends", num(e.friends));
        entry.set("score", num(e.score));
        state.set("ending", std::move(entry));
    }
    state.set("version", num(game.version));
    return state;
}

} // namespace nz::api::view
