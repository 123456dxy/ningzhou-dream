#include "core/tide.h"
#include "core/simulation/tide_rules.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/sim.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <limits>

namespace nz::tide {
namespace {
struct MaterialDescriptor {
    string name;
    int grade = 0;
};
// 用途：验证有限市场的物品类别与稳定 ID；流程：报价和成交；用法：传库存条目；返回：合法高阶材料信息或空值。
MaterialDescriptor descriptor(const TideMarketStock& stock) {
    if (stock.kind == "herb") {
        const auto* herb = findHerbById(stock.id);
        if (herb && herb->grade >= 3 && herb->grade <= 5) return {herb->name, herb->grade};
    } else if (stock.kind == "material") {
        for (const auto& material : data::MaterialDef_TABLE())
            if (material.id == stock.id && material.grade >= 3 && material.grade <= 5) return {material.name, material.grade};
    }
    return {};
}
// 用途：读取材料持有数；流程：市场视图与容量验证；用法：药材传名称，器材传ID；返回：已持有数量，不插入背包键。
int owned(const Game& game, const string& kind, const string& key) {
    const auto& inventory = kind == "herb" ? game.player.arts.herbs : game.player.arts.materials;
    const auto found = inventory.find(key);
    return found == inventory.end() ? 0 : found->second;
}
// 用途：检查新增材料是否超背包容量；流程：成交前原子校验；用法：传类别、背包键和数量；返回：失败原因或空串。
string capacityError(const Game& game, const string& kind, const string& key, int count) {
    const int current = owned(game, kind, key);
    if (current < 0 || static_cast<long long>(current) + count > rules::InventoryLimit)
        return "行囊无法收存更多这份材料。";
    if (kind == "herb") {
        long long total = count;
        for (const auto& [name, amount] : game.player.arts.herbs) {
            if (amount < 0) return "行囊药材记录无效，请先恢复存档。";
            total += amount;
            if (total > rules::InventoryLimit) return "行囊无法收存更多药材。";
        }
        if (game.player.herbs < 0 || static_cast<long long>(game.player.herbs) + count > rules::InventoryLimit)
            return "行囊无法收存更多药材。";
    }
    return {};
}
// 用途：集中检查交易时机；流程：报价门槛和成交；用法：传只读游戏；返回：不可交易原因或空串。
string tradingError(const Game& game) {
    if (game.ended) return "此世已结束，无法交易。";
    if (!marketOpen(game)) return "本期战后坊市尚未开放或已经结束。";
    if (!ecoIsCity(game.player.location)) return "请前往城池参加战后坊市。";
    if (game.pending) return "请先处理当前抉择。";
    if (game.player.arts.battleActive()) return "请先完成当前斗法。";
    if (game.job) return "请先完成或取消当前安排。";
    if (game.dungeons.run) return "请先结束秘境探索。";
    return {};
}
// 用途：创建无副作用的失败返回；流程：交易入口；用法：传明确原因；返回：失败对象。
MarketResult failure(const string& error) { MarketResult result; result.error = error; return result; }
} // namespace

// 用途：确认本期有限市场仍开放；流程：状态和交易门槛；用法：传只读游戏；返回：开放期间且人生未结束时为真。
bool marketOpen(const Game& game) {
    const auto& tide = game.tide;
    return !game.ended && tide.settled && !tide.active && tide.endDay >= 0 &&
        game.clock.days >= tide.endDay && game.clock.days < tide.marketExpiresDay;
}
// 用途：计算玩家排名的实付比例；流程：活动详情和交易；用法：传只读游戏；返回：正常买价的50..100%。
int discountPercent(const Game& game) {
    int rank = game.tide.playerRank;
    if (!game.tide.settled && game.tide.playerScore > 0)
        for (const auto& participant : leaderboard(game)) if (participant.actorId == "player") { rank = participant.rank; break; }
    return rules::payablePercent(rank, game.tide.playerJoined && game.tide.playerScore > 0);
}
// 用途：按全体总分生成一次有限高阶库存；流程：期末结榜；用法：仅结算后调用；返回：无，查询及读档不重新生成库存。
void initializeMarketStock(Game& game) {
    if (!game.tide.settled || !game.tide.marketStock.empty()) return;
    for (const auto& herb : data::HerbDef_TABLE()) if (herb.grade >= 3 && herb.grade <= 5)
        game.tide.marketStock.push_back({"herb", herb.id, rules::stockPerItem(game.tide.totalScore, herb.grade)});
    for (const auto& material : data::MaterialDef_TABLE()) if (material.grade >= 3 && material.grade <= 5)
        game.tide.marketStock.push_back({"material", material.id, rules::stockPerItem(game.tide.totalScore, material.grade)});
}
// 用途：生成权威市场目录；流程：桌面坊市与报价；用法：传只读游戏；返回：库存、正常价、折后价和具体不可购原因。
vector<MarketItem> marketItems(const Game& game) {
    vector<MarketItem> result;
    const int percent = discountPercent(game);
    const string blocked = tradingError(game);
    for (const auto& stock : game.tide.marketStock) {
        const auto material = descriptor(stock);
        if (material.grade == 0) continue;
        MarketItem item;
        item.kind = stock.kind; item.id = stock.id; item.name = material.name; item.grade = material.grade;
        // 此正常价复用普通坊市，包含当前声望、天赋、行情和本地产地优惠。
        item.basePrice = play::normalMaterialBuyPrice(game, stock.kind, stock.id);
        item.unitPrice = rules::discountedPrice(item.basePrice, percent);
        item.stock = clampInt(stock.stock, 0, rules::MaximumStock);
        const string key = stock.kind == "herb" ? material.name : stock.id;
        item.owned = owned(game, stock.kind, key);
        item.reason = blocked;
        if (item.reason.empty() && item.stock <= 0) item.reason = "本期库存已售罄。";
        if (item.reason.empty() && item.unitPrice <= 0) item.reason = "这份材料尚未登记有效价格。";
        if (item.reason.empty() && game.player.stones < item.unitPrice) item.reason = "灵石不足。";
        if (item.reason.empty()) item.reason = capacityError(game, stock.kind, key, 1);
        item.enabled = item.reason.empty();
        result.push_back(std::move(item));
    }
    return result;
}
// 用途：一次性购买本期材料；流程：战后坊市动作；用法：传场次、类别、ID、数量和可选报价；返回：成功成本或失败，失败不改任何事实。
MarketResult buyMarket(Game& game, const string& eventId, const string& kind,
                       const string& id, int count, int expectedUnitPrice) {
    if (eventId.empty() || eventId != game.tide.eventId) return failure("活动场次已变化，请刷新战后坊市。");
    if (count <= 0 || count > rules::InventoryLimit) return failure("购买数量无效。");
    if (expectedUnitPrice < -1) return failure("提交的报价无效。");
    const string blocked = tradingError(game);
    if (!blocked.empty()) return failure(blocked);
    const auto found = std::find_if(game.tide.marketStock.begin(), game.tide.marketStock.end(), [&](const TideMarketStock& stock) {
        return stock.kind == kind && stock.id == id;
    });
    if (found == game.tide.marketStock.end()) return failure("本期坊市没有这份材料。");
    const auto material = descriptor(*found);
    if (material.grade == 0) return failure("这份材料不属于战后坊市。");
    if (found->stock < count || found->stock > rules::MaximumStock) return failure("本期材料库存不足。");
    const int normalPrice = play::normalMaterialBuyPrice(game, kind, id);
    const int unitPrice = rules::discountedPrice(normalPrice, discountPercent(game));
    if (unitPrice <= 0) return failure("这份材料尚未登记有效价格。");
    if (expectedUnitPrice >= 0 && expectedUnitPrice != unitPrice) return failure("当前报价已经变化，请刷新后购买。");
    const long long cost = static_cast<long long>(unitPrice) * count;
    if (cost > std::numeric_limits<int>::max() || cost > game.player.stones) return failure("灵石不足以购入全部材料。");
    const string key = kind == "herb" ? material.name : id;
    const string capacity = capacityError(game, kind, key, count);
    if (!capacity.empty()) return failure(capacity);

    // 所有校验完成后才扣灵石、减库存、写背包，避免失败造成半笔交易。
    game.player.stones -= static_cast<int>(cost);
    found->stock -= count;
    if (kind == "herb") { game.player.arts.herbs[key] += count; game.player.herbs += count; }
    else game.player.arts.materials[key] += count;
    MarketResult result;
    result.ok = true; result.cost = static_cast<int>(cost);
    result.text = "在战后坊市购入" + material.name + " ×" + std::to_string(count) + "，支付" + std::to_string(cost) + "灵石。";
    logEvent(game, "self", result.text, false, "", "灵石 −" + std::to_string(cost));
    return result;
}
} // namespace nz::tide
