#include "core/interface/view_policy.h"
#include "core/interface/routes.h"

namespace nz::api::view {

// Shared with read-only action previews: keep the exact route guard order.
// 用途：按路由策略检查秘境结局战斗事件工作阻塞。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const string& path，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string actionBlockReason(const Game& game, const string& path) {
    const RouteInfo& route = lookupRoute(path);
    if (game.dungeons.run && !route.duringDungeon)
        return "请先结束当前秘境探索。";
    if (game.ended) return "这一世已经结束，请查看结局或开始新的一世。";
    if (game.player.arts.battle.has_value() && !route.duringBattle)
        return "请先完成当前斗法。";
    if (game.pending.has_value() && !route.duringPending)
        return "请先处理当前抉择。";
    if (game.job.has_value() && !route.duringJob)
        return "已有进行中的安排，请继续推进或先中止安排。";
    return "";
}
// 用途：检查突破修为灵石伤势及全局阻塞。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const BreakthroughInfo& info，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string breakthroughReason(const Game& game, const BreakthroughInfo& info) {
    const string blocked = actionBlockReason(game, routePath(RouteId::Breakthrough));
    if (!blocked.empty()) return blocked;
    if (!info.ready) return "修为尚未圆满。";
    if (game.player.stones < info.cost) return "灵石不足。";
    if (game.player.health < 35) return "伤势过重，请先休养至 35 气血。";
    return "";
}


} // namespace nz::api::view
