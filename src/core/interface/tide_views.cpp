#include "core/interface/views.h"
#include "core/interface/view_helpers.h"
#include "core/tide.h"
#include "core/simulation/tide_rules.h"
#include <algorithm>

namespace nz::api::view {
// 用途：统一兽潮活动、金榜和市场展示；参与完整state与独立查询；传只读Game，返回权威JSON，不推进日期或RNG。
json::Value tideState(const Game& game) {
    const auto& tide = game.tide;
    Value out(json::Object{});
    out.set("active",Value(tide.active)); out.set("severity",num(tide.severity));
    out.set("monthsLeft",num(tide.monthsLeft)); out.set("wavesFought",num(tide.wavesFought));
    out.set("wavesWon",num(tide.wavesWon)); out.set("npcDeaths",num(tide.npcDeaths));
    out.set("dangerBonus",num(tide::dangerBonus(game))); out.set("status",str(tide::statusText(game)));
    out.set("eventId",str(tide.eventId)); out.set("startDay",num(tide.startDay));
    out.set("endDay",num(tide.endDay)); out.set("durationDays",num(tide.durationDays));
    out.set("daysLeft",num(tide::daysLeft(game))); out.set("settled",Value(tide.settled));
    out.set("playerScore",num(tide.playerScore)); out.set("playerRank",num(tide.playerRank));
    out.set("rewardStones",num(tide.rewardStones)); out.set("rewardPaid",Value(tide.rewardPaid));
    out.set("totalScore",num(tide.totalScore));
    json::Array fallen, fields, ranking, items;
    for (const auto& name : tide.fallen) fallen.push_back(str(name));
    for (const auto& field : tide::battlefields(game)) {
        Value row(json::Object{});
        row.set("id",num(field.id)); row.set("name",str(field.name));
        row.set("minStage",num(field.minStage)); row.set("pointsPerKill",num(field.pointsPerKill));
        row.set("enabled",Value(field.enabled)); row.set("reason",str(field.reason));
        fields.push_back(std::move(row));
    }
    long long liveTotal = 0;
    for (const auto& actor : tide::leaderboard(game)) {
        liveTotal += std::max(0,actor.score);
        if (!tide.settled && actor.actorId=="player") out.set("playerRank",num(actor.rank));
        Value row(json::Object{});
        row.set("actorId",str(actor.actorId)); row.set("name",str(actor.name)); row.set("sect",str(actor.sect));
        const auto* sect=findSect(actor.sect);
        row.set("sectName",str(sect ? sect->name : "散修"));
        row.set("entryStage",num(actor.entryStage)); row.set("realm",str(realmName(actor.entryStage)));
        row.set("score",num(actor.score)); row.set("rank",num(actor.rank)); row.set("elite",Value(actor.elite));
        row.set("isPlayer",Value(actor.actorId=="player")); row.set("combatProfileId",str(actor.combatProfileId));
        ranking.push_back(std::move(row));
    }
    if (!tide.settled) out.set("totalScore",num(static_cast<int>(std::min(1000000000LL,liveTotal))));
    for (const auto& item : tide::marketItems(game)) {
        Value row(json::Object{});
        row.set("kind",str(item.kind)); row.set("id",str(item.id)); row.set("name",str(item.name));
        row.set("grade",num(item.grade)); row.set("basePrice",num(item.basePrice)); row.set("unitPrice",num(item.unitPrice));
        row.set("stock",num(item.stock)); row.set("owned",num(item.owned));
        row.set("enabled",Value(item.enabled)); row.set("reason",str(item.reason));
        items.push_back(std::move(row));
    }
    Value market(json::Object{});
    market.set("open",Value(tide::marketOpen(game))); market.set("expiresDay",num(tide.marketExpiresDay));
    market.set("daysLeft",num(tide::marketOpen(game)?std::max(0,tide.marketExpiresDay-game.clock.days):0));
    market.set("discountPercent",num(tide::discountPercent(game))); market.set("minDiscountPercent",num(tide::rules::MinimumPayablePercent));
    market.set("priceBasis",str("当前声望下普通坊市购入价")); market.set("items",Value(std::move(items)));
    out.set("fallen",Value(std::move(fallen))); out.set("battlefields",Value(std::move(fields)));
    out.set("leaderboard",Value(std::move(ranking))); out.set("market",std::move(market));
    return out;
}
} // namespace nz::api::view
