// 宁州一梦 · C++ 版 — 即时经济动作（实现）
#include "core/shop.h"
#include "core/crafting/market_cycle.h"
#include "core/foundation/state_limits.h"
#include "core/crafting/cave_garden.h"
#include "core/crafting/craft_prices.h"
#include "core/crafting/material_supply.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/arts.h"
#include "core/battle.h"
#include "core/cultivate.h"
#include "core/economy.h"
#include "core/renown.h"
#include "core/sim.h"
#include "core/talents.h"
#include "core/content/npc_combat_profiles.h"
#include "data/reference_tables.h"

namespace nz::shop {

using std::string;
using std::vector;

namespace {

// 行情系数（与 JS 一致：直接乘 world.market）
// 用途：读取当前坊市行情系数。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double market(const Game& g) { return g.world.market; }

// 用途：按法器ID查玩家行囊。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const Gear* findGear(const Game& g, const string& id) {
    for (const auto& gr : g.player.arts.gear)
        if (gr.id == id) return &gr;
    return nullptr;
}

// 用途：查法器是否占据装备槽。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool isEquipped(const Game& g, const string& id) {
    for (const auto& [slot, gid] : g.player.arts.equipped)
        if (gid == id) return true;
    return false;
}

// 用途：构造带中文原因的交易失败结果。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& msg，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult fail(const string& msg) {
    SellResult r;
    r.error = msg;
    return r;
}

// 用途：构造交易成功文本与灵石变化。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& msg, int gain = 0，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult ok(const string& msg, int gain = 0) {
    SellResult r;
    r.ok = true;
    r.text = msg;
    r.gain = gain;
    return r;
}

} // namespace

// ============================================================
// 出售
// ============================================================
// 用途：按物品类别计算真实出售价格。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& kind, const string& id，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int salePrice(const Game& g, const string& kind, const string& id) {
    if (kind == "potion") {
        const auto Quote = crafting::mfQuotePotionPrice(g, id);
        return Quote.Valid ? Quote.SellPrice : 0;
    }
    if (kind == "gear") {
        const Gear* GearState = findGear(g, id);
        if (!GearState) return 0;
        const auto Quote = crafting::mfQuoteGearPrice(g, *GearState);
        return Quote.Valid ? Quote.SellPrice : 0;
    }
    return 0;
}

MarketSaleContext mfMarketSaleContext(const Game& State) {
    return {ecoIsCity(State.player.location), std::max(0, State.economy.marketCash),
        ecoMarketBudget(State), 0, crafting::MarketCycleDurationDays, crafting::mfMarketDaysUntilRefresh(State)};
}

MarketSaleQuote mfQuoteMarketSale(const Game& State, const string& Kind, const string& ItemId, int UnitSalePrice) {
    return mfQuoteMarketSale(State, Kind, ItemId, UnitSalePrice, mfMarketSaleContext(State));
}

MarketSaleQuote mfQuoteMarketSale(const Game& State, const string& Kind, const string& ItemId, int UnitSalePrice,
                                const MarketSaleContext& Context) {
    MarketSaleQuote Quote;
    Quote.InCity = Context.InCity;
    Quote.RemainingCash = Context.RemainingCash;
    Quote.NextMonthBudget = Context.NextMonthBudget;
    Quote.RefreshCycleDays = Context.RefreshCycleDays;
    Quote.DaysUntilRefresh = Context.DaysUntilRefresh;
    if ((Kind != "potion" && Kind != "gear") || UnitSalePrice <= 0) {
        Quote.Reason = "此物无法出售。";
        return Quote;
    }
    static_cast<void>(ItemId); // 保留旧报价签名；三年账期只受资金限制，不再消耗同类需求额度。
    Quote.AffordableCount = Quote.RemainingCash / UnitSalePrice;
    if (!Quote.InCity) {
        Quote.AffordableCount = 0;
        Quote.Reason = "此处没有坊市。请前往五城之一。";
    } else if (Quote.RemainingCash < UnitSalePrice) {
        Quote.Reason = "坊市收购资金不足（余 " + std::to_string(Quote.RemainingCash) + "，单价 " +
            std::to_string(UnitSalePrice) + "）。";
        if (UnitSalePrice > Quote.NextMonthBudget)
            Quote.Reason += "此单价超过当前条件的下期额度 " + std::to_string(Quote.NextMonthBudget) +
                "，需提高境界或声望；换城不另补资金。";
        else Quote.Reason += "本期余额不补充，下个三年账期额度 " + std::to_string(Quote.NextMonthBudget) +
            "，距刷新 " + std::to_string(Quote.DaysUntilRefresh) + " 天。";
    }
    return Quote;
}

// 用途：预览出售资格需求与收益。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& kind, const string& id，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote saleQuote(const Game& g, const string& kind, const string& id) {
    return saleQuote(g, kind, id, mfMarketSaleContext(g));
}

ActionQuote saleQuote(const Game& g, const string& kind, const string& id, const MarketSaleContext& Context) {
    ActionQuote quote;
    quote.returnStones = salePrice(g, kind, id);
    const auto reject = [&](const string& error) { auto out = quote; out.reason = error; return out; };
    const auto& p = g.player;
    const auto& a = p.arts;
    if (g.ended) return reject("这一世已经结束。");
    if (g.pending.has_value()) return reject("请先完成当前事件。");
    if (a.battle.has_value()) return reject("斗法中无法交易。");
    if (quote.returnStones <= 0) return reject("此物无法出售。");
    if (static_cast<long long>(p.stones) + quote.returnStones > StateLimits::MaxPersistedInteger)
        return reject("出售后灵石余额超过支持范围。");
    const auto MarketQuote = mfQuoteMarketSale(g, kind, id, quote.returnStones, Context);
    quote.demandChecked = true;
    if (!MarketQuote.Reason.empty()) return reject(MarketQuote.Reason);
    if (kind == "potion") {
        quote.potionChecked = true;
        const auto owned = a.potions.find(id);
        if (owned == a.potions.end() || owned->second <= 0) return reject("没有这枚丹药。");
    } else if (kind == "gear") {
        if (!findGear(g, id)) return reject("没有这件法器。");
        if (isEquipped(g, id)) return reject("请先卸下装备再出售。");
    } else return reject("此物无法出售。");
    quote.enabled = true;
    return quote;
}

// 用途：检查出售条件后转移物品并发放灵石。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& kind, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult sell(Game& g, const string& kind, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    Economy& e = g.economy;
    const ActionQuote quote = saleQuote(g, kind, id);
    // 拒绝路径不提前插入需求或库存键；成功提交才更新关联账本。
    if (!quote.enabled) return fail(quote.reason);
    const int price = quote.returnStones;
    string label;
    if (kind == "potion") {
        const data::RecipeDef* r = findRecipe(id);
        label = r ? r->name : id;
        a.potions[id] -= 1;
    } else if (kind == "gear") {
        const Gear* gr = findGear(g, id);
        label = gr->name;
        a.gear.erase(std::remove_if(a.gear.begin(), a.gear.end(),
                                    [&](const Gear& x) { return x.id == id; }),
                     a.gear.end());
    } else {
        return fail("此物无法出售。");
    }

    e.marketCash -= price;
    p.stones += price;
    a.progress.wealth = std::max(a.progress.wealth, p.stones);

    const string text = "坊市收购" + label + "，灵石 +" + std::to_string(price) + "。";
    logEvent(g, "self", text, false, "", "灵石 +" + std::to_string(price));
    return ok(text, price);
}

// ============================================================
// 灵核 / 凝元丹
// ============================================================
// 用途：计算灵核当前买入或卖出单价。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, bool buying，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int corePrice(const Game& g, bool buying) {
    // 灵核稀有：买价随境界涨，卖价打七折
    const double base = 120.0 + g.player.stage * 30.0;
    return jsRound(base * market(g) * (buying ? 1.0 : 0.7));
}

// 用途：计算凝元丹当前买入或卖出单价。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, bool buying，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int pillPrice(const Game& g, bool buying) {
    const double base = 60.0 + g.player.stage * 12.0;
    return jsRound(base * market(g) * (buying ? 1.0 : 0.7));
}

// 用途：结算灵核与凝元丹的批量买卖。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& kind, bool buying, int count，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult tradeStock(Game& g, const string& kind, bool buying, int count) {
    Player& p = g.player;
    if (g.ended) return fail("这一世已经结束。");
    if (g.pending.has_value()) return fail("请先处理当前事件。");
    if (p.arts.battle.has_value()) return fail("斗法中无法交易。");
    if (count < 1 || count > 1000) return fail("交易数量须为 1 至 1000。");
    if (!ecoIsCity(p.location)) return fail("此处没有坊市。请前往五城之一。");

    if (kind == "cores") {
        if (buying) {
            const int unit = corePrice(g, true);
            const int total = unit * count;
            if (p.stones < total)
                return fail("灵石不足：需要 " + std::to_string(total) + "。");
            if (g.world.coreStock < count)
                return fail("坊市灵核存量不足（余 " +
                            std::to_string(g.world.coreStock) + "）。");
            p.stones -= total;
            g.world.coreStock -= count;
            p.cores += count;
            const string t = "购入灵核 ×" + std::to_string(count) + "。";
            logEvent(g, "self", t, false, "", "灵石 −" + std::to_string(total));
            return ok(t, -total);
        }
        if (p.cores < count) return fail("灵核不足。");
        const int unit = corePrice(g, false);
        p.cores -= count;
        g.world.coreStock += count;
        p.stones += unit * count;
        const string t = "售出灵核 ×" + std::to_string(count) + "。";
        logEvent(g, "self", t, false, "", "灵石 +" + std::to_string(unit * count));
        return ok(t, unit * count);
    }

    if (kind == "pills") {
        if (buying) {
            const int unit = pillPrice(g, true);
            const int total = unit * count;
            if (p.stones < total)
                return fail("灵石不足：需要 " + std::to_string(total) + "。");
            if (g.world.pillStock < count)
                return fail("坊市凝元丹存量不足。");
            p.stones -= total;
            g.world.pillStock -= count;
            p.pills += count;
            const string t = "购入凝元丹 ×" + std::to_string(count) + "。";
            logEvent(g, "self", t, false, "", "灵石 −" + std::to_string(total));
            return ok(t, -total);
        }
        if (p.pills < count) return fail("凝元丹不足。");
        const int unit = pillPrice(g, false);
        p.pills -= count;
        g.world.pillStock += count;
        p.stones += unit * count;
        const string t = "售出凝元丹 ×" + std::to_string(count) + "。";
        logEvent(g, "self", t, false, "", "灵石 +" + std::to_string(unit * count));
        return ok(t, unit * count);
    }
    return fail("没有这种货品。");
}

// ============================================================
// 洞府档位与配给
// ============================================================
// 用途：取得洞府档位成本声望与供给上限。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应目录或计算结果列表。
const vector<CaveTierDef>& caveTierDefs() {
    static const vector<CaveTierDef> tiers = {
        {0, 1, 0, 0, "寻常洞府", "可设一档灵气配给。高级灵气须先升级洞府。"},
        {1, 2, 20, 120, "灵脉洞府", "可设二档灵气配给；在洞府闭关时，气场令修炼速度额外+10%。"},
        {2, 3, 60, 400, "聚灵仙府", "可设三档灵气配给；在洞府闭关时，气场令修炼速度额外+20%。"},
    };
    return tiers;
}

// 用途：取得已购洞府的灵气配给上限。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int caveMaxSupply(const Player& p) {
    return caveTierDefs()[clampInt(p.caveTier, 0, 2)].maxSupply;
}

namespace {
// 用途：检查洞府操作的进行中状态阻塞。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string caveBusyError(const Game& g) {
    if (g.ended) return "这一世已经结束。";
    if (g.pending) return "请先处理当前事件。";
    if (g.player.arts.battle) return "斗法中无法改动洞府或配给。";
    if (g.job) return "请先完成或中止当前安排，再改动洞府或配给。";
    return "";
}
}

// 用途：检查洞府升档声望与灵石门槛。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string caveUpgradeError(const Game& g) {
    const string busy = caveBusyError(g);
    if (!busy.empty()) return busy;
    if (g.player.cave.empty()) return "请先在所在地安置洞府。";
    if (!cultivate::atCave(g.player)) return "请先回到洞府再升级。";
    if (g.player.caveTier >= 2) return "洞府已是最高档的聚灵仙府。";
    const auto& next = caveTierDefs()[clampInt(g.player.caveTier, 0, 1) + 1];
    if (g.player.renown < next.minRenown)
        return "升级为「" + next.name + "」需声望 " + std::to_string(next.minRenown) + "。";
    if (g.player.stones < next.cost)
        return "升级为「" + next.name + "」需 " + std::to_string(next.cost) + " 灵石。";
    return "";
}

// 用途：支付已验证成本并解锁更高配给。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveUpgrade(Game& g) {
    const string error = caveUpgradeError(g);
    if (!error.empty()) return fail(error);
    const auto& next = caveTierDefs()[g.player.caveTier + 1];
    g.player.stones -= next.cost;
    g.player.caveTier = next.tier;
    const string text = "洞府升级为「" + next.name + "」，解锁" + std::to_string(next.maxSupply) + "档灵气配给。";
    logEvent(g, "self", text, true, "", "灵石 −" + std::to_string(next.cost));
    return ok(text, -next.cost);
}

// 用途：校验并设置灵石或灵气配给档位。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& kind, int level，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveSupplySet(Game& g, const string& kind, int level) {
    const string busy = caveBusyError(g);
    if (!busy.empty()) return fail(busy);
    if (kind != "stone" && kind != "qi") return fail("没有这种配给。");
    if (level < 0 || level > cultivate::maxSupplyLevel()) return fail("配给档位须为 0 至 3。");
    if (g.player.cave.empty() && kind == "qi" && level > 0) return fail("请先安置洞府再设置灵气配给。");
    if (kind == "qi" && level > caveMaxSupply(g.player)) {
        const auto& required = caveTierDefs()[level - 1];
        return fail("此档配给需「" + required.name + "」；升级需声望 " + std::to_string(required.minRenown) + "。");
    }
    if (kind == "stone") g.player.stoneSupply = level;
    else g.player.qiSupply = level;
    const int cost = cultivate::supplyCost(g.player, kind, level);
    const string text = string(kind == "qi" ? "灵气" : "灵石") + "配给调整为「" + cultivate::supplyName(kind, level) + "」" +
        (cost > 0 ? "，每月 " + std::to_string(cost) + " 灵石。" : "，不再投入灵石。");
    logEvent(g, "self", text);
    return ok(text);
}

// 用途：支付迁府费用并保留已购洞府等级。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult caveMove(Game& g) {
    const auto Quote = mfQuoteCaveMove(g);
    if (!Quote.enabled) return fail(Quote.reason);
    Game Candidate = g;
    Candidate.player.stones -= Quote.costStones;
    Candidate.player.cave = Candidate.player.location;
    const string Text = "在" + string(findLoc(Candidate.player.location)->name) + "安下「" + caveTierDefs()[clampInt(Candidate.player.caveTier, 0, 2)].name + "」，已购档位保留。";
    logEvent(Candidate, "self", Text, true, "", "灵石 −" + std::to_string(Quote.costStones));
    g = std::move(Candidate);
    return ok(Text, -Quote.costStones);
}

// 用途：统一安置/迁府费用与完整预检；流程：只读页面及真实caveMove；返回：报价值，不写状态，种苗/储备未清阻断迁府。
ActionQuote mfQuoteCaveMove(const Game& State) {
    constexpr int BaseSettlementCost = 40;
    constexpr int ExtraCostPerMajorRealm = 30;
    ActionQuote Quote;
    Quote.costStones = BaseSettlementCost + clampInt(State.player.stage / 3, 0, 4) * ExtraCostPerMajorRealm;
    Quote.reason = caveBusyError(State);
    if (Quote.reason.empty() && State.dungeons.run) Quote.reason = "请先离开秘境再安置洞府。";
    if (Quote.reason.empty() && !findLoc(State.player.location)) Quote.reason = "所在地无法安置洞府。";
    if (Quote.reason.empty() && State.player.cave == State.player.location) Quote.reason = "洞府已经在这里了。";
    if (Quote.reason.empty()) for (const auto& Plot : State.Garden.Plots)
        if (!Plot.Cohorts.empty() || Plot.ReserveStones > 0) {
            Quote.reason = "迁府前请先收获或清理全部灵田；旧灵田余额会在下个真实世界日自动退回。";
            break;
        }
    if (Quote.reason.empty() && State.player.stones < Quote.costStones)
        Quote.reason = "迁府需要 " + std::to_string(Quote.costStones) + " 灵石。";
    Quote.enabled = Quote.reason.empty(); return Quote;
}

// ============================================================
// 功法获取
// ============================================================
namespace {
// 用途：检查功法与神通获取时的状态阻塞。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string learningBusyError(const Game& g) {
    if (g.ended || g.player.health <= 0) return "此世已结束或伤势过重。";
    if (g.pending || g.job || g.player.arts.battle || g.dungeons.run) return "请先完成当前安排、事件或斗法。";
    return "";
}
// 用途：判断宗门是否传承指定大道。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const string& sect, const string& dao，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool sectDaoAllowed(const string& sect, const string& dao) {
    if (sect == "jinhong") return dao == "金" || dao == "剑";
    if (sect == "xinghe") return dao == "水" || dao == "剑";
    if (sect == "zhushan") return dao == "木";
    if (sect == "lihuo") return dao == "火";
    if (sect == "huachen") return dao == "土" || dao == "体";
    if (sect == "xuandao") return dao == "阵" || dao == "剑";
    return false;
}
// 用途：确定功法对应宗门藏经阁。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& dao，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string manualSect(const Game& g, const string& dao) {
    if (dao == "金" || dao == "剑") return "jinhong";
    if (dao == "水") return "xinghe";
    if (dao == "木") return "zhushan";
    if (dao == "火") return "lihuo";
    if (dao == "土" || dao == "体") return "huachen";
    if (dao == "阵") return "xuandao";
    return findSect(g.player.sect) ? g.player.sect : "";
}
// 用途：检查抄本与已修功法避免重复发放。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool ownedManual(const Game& g, const string& id) {
    const auto& books = g.economy.books;
    const auto& list = g.player.arts.bookList;
    const auto studied = g.player.arts.manuals.find(id);
    return std::find(books.begin(), books.end(), id) != books.end() ||
        std::find(list.begin(), list.end(), id) != list.end() ||
        (studied != g.player.arts.manuals.end() && studied->second > 0);
}
// 用途：计算声望折扣后的藏经阁贡献需求。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, int stage, bool spell = false，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int libraryPoints(const Game& g, int stage, bool spell = false) {
    return std::max(1, static_cast<int>(std::ceil(((spell ? 18 : 25) + stage * (spell ? 12 : 18)) *
        (1.0 - renown::libraryDiscount(g.player.renown)))));
}
// 用途：判断可在各宗学习的通用神通。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const SpellDef& spell，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool commonSpell(const SpellDef& spell) {
    static const vector<string> ids = {"skim", "refresh", "purge", "scheme", "insightdraw"};
    return spell.dao == "气" || std::find(ids.begin(), ids.end(), spell.id) != ids.end();
}
}

// 用途：查询功法抄本的书坊与宗门获取途径。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：ManualSource结果，包含说明所述状态或计算数据。
ManualSource manualSource(const Game& g, const string& id) {
    ManualSource ms;
    // 新传承不修改生成表；玄道宗只共享其五行剑完整模板所需抄本。
    const auto* XuandaoProfile = [&]() -> const content::NpcCombatProfile* {
        for (const auto& Profile : content::npcCombatProfiles()) if (Profile.sect == "xuandao") return &Profile;
        return nullptr;
    }();
    if (id == "fiveSword" || (g.player.sect == "xuandao" && XuandaoProfile &&
        std::find(XuandaoProfile->manuals.begin(), XuandaoProfile->manuals.end(), id) != XuandaoProfile->manuals.end())) {
        ManualInfo Info;
        if (!findManualInfo(id, Info)) return ms;
        ms.valid = true; ms.id = Info.id; ms.name = Info.name; ms.stage = Info.stage; ms.price = Info.price;
        ms.sect = "xuandao"; ms.points = libraryPoints(g, Info.stage);
        ms.how = "玄道宗藏经阁兑换（" + std::to_string(ms.points) + " 贡献）；完整传承弟子亦可逐项请教";
        return ms;
    }

    // 先查拓展功法表：只有这张表带 source（所属宗门）。
    // 顺序很关键 —— BaseManualDef_TABLE 里其实也包含这 26 部，
    // 先查它的话每部都会被当成「书坊有售」，藏经阁那条路就成了死代码，
    // 宗门贡献也就永远花不出去。
    for (const auto& m : data::ManualDef_TABLE()) {
        if (m.id != id) continue;
        ms.valid = true;
        ms.id = m.id;
        ms.name = m.name;
        ms.stage = m.stage;
        ms.price = m.price;
        if (!m.source.empty() && (!findSect(m.source) ||
            !(sectDaoAllowed(m.source, m.dao) || (m.source == "huachen" && m.id == "wheel")))) return ManualSource{};
        ms.sect = m.source.empty() ? manualSect(g, m.dao) : m.source;
        if (m.source.empty()) ms.city = "wuling";
        // 名声高的人，藏经阁给折扣
        ms.points = libraryPoints(g, m.stage);
        ms.how = ms.sect.empty() ? "武陵城书坊购入" : string(findSect(ms.sect)->name) + "藏经阁兑换（" + std::to_string(ms.points) + " 贡献）";
        if (!ms.city.empty() && !ms.sect.empty()) ms.how += "；武陵城书坊亦有抄本";
        return ms;
    }

    // 其余是入门功法：书坊用灵石买
    for (const auto& m : data::BaseManualDef_TABLE()) {
        if (m.id != id) continue;
        ms.valid = true;
        ms.id = m.id;
        ms.name = m.name;
        ms.stage = m.stage;
        ms.price = m.price;
        ms.city = "wuling";                 // 书坊设在武陵城
        ms.sect = manualSect(g, m.dao);
        ms.points = libraryPoints(g, m.stage);
        ms.how = "武陵城书坊购入（" + std::to_string(m.price) + " 灵石）";
        if (!ms.sect.empty()) ms.how += "；" + string(findSect(ms.sect)->name) + "藏经阁可兑";
        return ms;
    }
    return ms;
}

// 用途：筛出当前位置与持有状态可见的功法。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<ManualSource> availableManuals(const Game& g) {
    vector<ManualSource> out;
    // BaseManualDef_TABLE 已含全部 40 部，遍历它即可，不必再叠 ManualDef
    for (const auto& m : data::BaseManualDef_TABLE()) {
        ManualSource ms = manualSource(g, m.id);
        if (ms.valid && (ownedManual(g, m.id) || (!ms.city.empty() && g.player.location == ms.city) ||
            (!ms.sect.empty() && ms.sect == g.player.sect && g.player.location == ms.sect))) out.push_back(ms);
    }
    auto FiveSword = manualSource(g, "fiveSword");
    if (FiveSword.valid && (ownedManual(g, "fiveSword") ||
        (g.player.sect == FiveSword.sect && g.player.location == FiveSword.sect))) out.push_back(std::move(FiveSword));
    return out;
}

// 用途：在对应书坊支付灵石购入未持有抄本。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult bookBuy(Game& g, const string& id) {
    if (const string error = learningBusyError(g); !error.empty()) return fail(error);
    Player& p = g.player;
    Economy& e = g.economy;
    const ManualSource ms = manualSource(g, id);
    if (!ms.valid) return fail("没有这部功法。");
    if (ms.city.empty()) return fail("这部功法书坊没有，需从宗门藏经阁或机缘获取。");
    if (ownedManual(g, id))
        return fail("已经拥有此功法。");
    if (p.location != ms.city)
        return fail("请前往" +
                    string(findLoc(ms.city) ? findLoc(ms.city)->name : ms.city) +
                    "的书坊。");
    if (p.stage < ms.stage) return fail("境界未达到获取要求。");
    if (p.stones < ms.price) return fail("灵石不足。");

    p.stones -= ms.price;
    e.books.push_back(id);
    if (std::find(p.arts.bookList.begin(), p.arts.bookList.end(), id) ==
        p.arts.bookList.end())
        p.arts.bookList.push_back(id);

    const string t = "在书坊购入《" + ms.name + "》抄本。";
    logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(ms.price));
    return ok(t, -ms.price);
}

// 用途：在本宗藏经阁支付贡献兑换抄本。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult libraryExchange(Game& g, const string& id) {
    if (const string error = learningBusyError(g); !error.empty()) return fail(error);
    Player& p = g.player;
    Economy& e = g.economy;
    const ManualSource ms = manualSource(g, id);
    if (!ms.valid) return fail("没有这部功法。");
    if (ms.sect.empty()) return fail("这部功法不在藏经阁。");
    if (p.sect != ms.sect)
        return fail("仅可在所属宗门藏经阁兑换。");
    if (p.location != ms.sect)
        return fail("请回到山门藏经阁。");
    if (ownedManual(g, id))
        return fail("已经拥有此功法。");
    if (p.stage < ms.stage) return fail("境界未达到获取要求。");
    if (e.contribution < ms.points)
        return fail("宗门贡献不足：需要 " + std::to_string(ms.points) +
                    "，你有 " + std::to_string(e.contribution) + "。");

    e.contribution -= ms.points;
    e.books.push_back(id);
    if (std::find(p.arts.bookList.begin(), p.arts.bookList.end(), id) ==
        p.arts.bookList.end())
        p.arts.bookList.push_back(id);

    const string t = "在藏经阁以 " + std::to_string(ms.points) + " 贡献换得《" +
                     ms.name + "》。";
    logEvent(g, "sect", t, true, "", "贡献 −" + std::to_string(ms.points));
    return ok(t, -ms.points);
}

// 用途：预览本门神通传承门槛与贡献价格。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：LibrarySpellSource结果，包含说明所述状态或计算数据。
LibrarySpellSource librarySpellSource(const Game& g, const string& id) {
    LibrarySpellSource source;
    const auto* spell = battleSpell(id);
    if (!spell || id == "strike" || id == "defend" || !findSect(g.player.sect)) return source;
    const bool IsElementalSword = id == "metalSword" || id == "waterSword" || id == "woodSword" ||
        id == "fireSword" || id == "earthSword";
    if (IsElementalSword ? g.player.sect != "xuandao" :
        (!commonSpell(*spell) && !sectDaoAllowed(g.player.sect, spell->dao))) return source;
    source.valid = true;
    source.id = spell->id; source.name = spell->name; source.dao = spell->dao;
    source.sect = g.player.sect; source.stage = spell->stage;
    source.desc = battleSpellDescription(*spell);
    source.points = libraryPoints(g, spell->stage, true);
    const auto& known = g.player.arts.knownSpells;
    source.known = std::find(known.begin(), known.end(), id) != known.end() ||
        std::find(g.player.arts.spells.begin(), g.player.arts.spells.end(), id) != g.player.arts.spells.end();
    source.reason = learningBusyError(g);
    if (source.reason.empty() && g.player.location != source.sect) source.reason = "请回到本宗藏经阁。";
    if (source.reason.empty() && source.known) source.reason = "已经领悟此神通。";
    if (source.reason.empty() && g.player.stage < source.stage) source.reason = "至少需要" + realmName(source.stage) + "。";
    if (source.reason.empty() && g.economy.contribution < source.points) source.reason = "宗门贡献不足。";
    source.enabled = source.reason.empty();
    return source;
}

// 用途：汇总本宗可传授的神通。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<LibrarySpellSource> availableLibrarySpells(const Game& g) {
    vector<LibrarySpellSource> sources;
    for (const auto& spell : allSpells()) {
        auto source = librarySpellSource(g, spell.id);
        if (source.valid) sources.push_back(std::move(source));
    }
    return sources;
}

// 用途：支付贡献并领悟当前本门神通。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult librarySpellExchange(Game& g, const string& id) {
    const auto source = librarySpellSource(g, id);
    if (!source.valid) return fail("此神通不在本门传承中，通用攻击与防御也无需兑换。");
    if (!source.enabled) return fail(source.reason);
    g.economy.contribution -= source.points;
    g.player.arts.knownSpells.push_back(id);
    const string text = "在藏经阁以" + std::to_string(source.points) + "贡献领悟「" + source.name + "」，可在神通页自行装备。";
    logEvent(g, "sect", text, true, "", "贡献 −" + std::to_string(source.points));
    return ok(text, -source.points);
}

// ============================================================
// 法器
// ============================================================
// 用途：预览法器装备出售铭刻与拆解资格。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g, const string& action, const string& id，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote gearActionQuote(const Game& g, const string& action, const string& id) {
    if (action == "sell") return saleQuote(g, "gear", id);
    ActionQuote quote;
    const Gear* gear = findGear(g, id);
    if (action == "equip") quote.reason = artEquipError(g.player, id);
    else if (action == "unequip") {
        if (!isEquipped(g, id)) quote.reason = "这件法器并未装备。";
    } else if (action == "salvage") {
        const auto salvage = crafting::salvageQuote(g, id);
        quote.costStones = salvage.cost;
        quote.reason = salvage.reason;
    } else if (action == "inscribe") {
        quote.costStones = gear ? 30 * gear->grade : 0;
        if (!artHas(g.player.arts, "器", 1)) quote.reason = "需先领悟「器道入门」才能铭刻。";
        else if (!gear) quote.reason = "没有这件法器。";
        else if (gear->ForgeVersion == 2) quote.reason = "新版法器的灵纹已在成器前确定，不能追加旧式铭刻；如需更改请重新炼制。";
        else if (gear->inscribed) quote.reason = "这件法器已经铭刻过了。";
        else if (g.player.stones < quote.costStones)
            quote.reason = "铭刻需 " + std::to_string(quote.costStones) + " 灵石。";
    } else quote.reason = "未知法器操作。";
    quote.enabled = quote.reason.empty();
    return quote;
}

// 用途：预览丹炉修理费用与资格。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：ActionQuote结果，包含说明所述状态或计算数据。
ActionQuote furnaceRepairQuote(const Game& g) {
    ActionQuote quote; quote.costStones = 20;
    if (g.player.arts.durability >= 40) quote.reason = "丹炉完好，无需修理。";
    else if (g.player.stones < quote.costStones) quote.reason = "修炉需 20 灵石。";
    quote.enabled = quote.reason.empty();
    return quote;
}

// 用途：移除法器占据的装备槽位。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult unequip(Game& g, const string& id) {
    const auto quote = gearActionQuote(g, "unequip", id);
    if (!quote.enabled) return fail(quote.reason);
    Arts& a = g.player.arts;
    for (auto it = a.equipped.begin(); it != a.equipped.end(); ++it) {
        if (it->second != id) continue;
        const string slot = it->first;
        a.equipped.erase(it);
        return ok("卸下了" + slot + "位的法器。");
    }
    return fail("这件法器并未装备。");
}

// 用途：按确认报价回收法器原材料并支付手续费。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult salvage(Game& g, const string& id) {
    const Gear* gr = findGear(g, id);
    const auto quote = gearActionQuote(g, "salvage", id);
    if (!quote.enabled) return fail(quote.reason);

    const string name = gr->name;
    const auto preview = crafting::salvageQuote(g, id);
    const string error = crafting::salvageMaterials(g, id);
    if (!error.empty()) return fail(error);
    string materials;
    for (const auto& row : preview.materials) materials += (materials.empty() ? "" : "、") + row.name + "×" + std::to_string(row.required);
    const string t = "分解「" + name + "」，回收" + materials + "，手续费" + std::to_string(preview.cost) + "灵石。";
    logEvent(g, "self", t, false, "", "灵石 −" + std::to_string(preview.cost) + "；" + materials);
    return ok(t, -preview.cost);
}

// 用途：支付铭刻费用并增强指定法器。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult inscribe(Game& g, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    const auto quote = gearActionQuote(g, "inscribe", id);
    if (!quote.enabled) return fail(quote.reason);
    for (auto& gr : a.gear) {
        if (gr.id != id) continue;
        const int cost = quote.costStones;
        p.stones -= cost;
        gr.inscribed = true;
        const string t = "为「" + gr.name + "」铭刻符文，效果 +12%。";
        logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(cost));
        return ok(t, -cost);
    }
    return fail("没有这件法器。");
}

// ============================================================
// 丹炉
// ============================================================
// 用途：支付修理费用恢复丹炉耐久。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult repairFurnace(Game& g) {
    Player& p = g.player;
    Arts& a = p.arts;
    const auto quote = furnaceRepairQuote(g);
    if (!quote.enabled) return fail(quote.reason);
    p.stones -= quote.costStones;
    a.durability = 40;
    const string t = "修补丹炉，耐久回满。";
    logEvent(g, "self", t, false, "", "灵石 −20");
    return ok(t, -quote.costStones);
}

// 用途：筛选当前境界可以使用的丹炉。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
vector<const data::FurnaceDef*> buyableFurnaces(const Game& g) {
    vector<const data::FurnaceDef*> out;
    // 与真实开炉资格同源，突破准备阶段可购对应丹炉，六品炉也可正常解锁。
    const int maxGrade = artTier(g.player);
    for (const auto& f : data::FurnaceDef_TABLE())
        if (f.grade <= maxGrade) out.push_back(&f);
    return out;
}

// 用途：支付价格更换可用丹炉并恢复耐久。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g, const string& id，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult buyFurnace(Game& g, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;
    if (a.furnace == id) return fail("这已经是当前丹炉。");
    for (const auto& f : data::FurnaceDef_TABLE()) {
        if (f.id != id) continue;
        const int maxGrade = artTier(p);
        if (f.grade > maxGrade)
            return fail("境界不足，暂时用不了这尊丹炉。");
        const int price = f.price > 0 ? f.price : 100 * f.grade * f.grade;
        if (p.stones < price)
            return fail("灵石不足：需要 " + std::to_string(price) + "。");
        p.stones -= price;
        a.furnace = f.id;
        a.durability = 40;
        const string t = "换用「" + string(f.name) + "」，耐久回满。";
        logEvent(g, "self", t, true, "", "灵石 −" + std::to_string(price));
        return ok(t, -price);
    }
    return fail("没有这尊丹炉。");
}

// ============================================================
// 中止安排
// ============================================================
// 用途：中止当前工作并保留已消耗成本。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：SellResult结果，包含说明所述状态或计算数据。
SellResult cancelJob(Game& g) {
    if (!g.job.has_value()) return fail("当前没有进行中的安排。");
    const Job job = *g.job;
    const string name = job.name;
    g.job.reset();
    string t = "中止「" + name + "」。已投入的材料与灵石不返还。";
    if (job.actionType == "travel") {
        const string origin = job.origin.empty() ? g.player.location : job.origin;
        const LocationDef* place = findLoc(origin);
        t = "中止「" + name + "」，仍停留在出发地" +
            (place ? string(place->name) : origin) + "。";
        if (job.cost > 0)
            t += "已支付的" + string(job.travelMode == "boat" || job.travelMode.empty() ? "船资" : "御器费用") +
                 std::to_string(job.cost) + "灵石不返还。";
        else t += "步行未支付灵石。";
    }
    logEvent(g, "self", t, false);
    return ok(t);
}

} // namespace nz::shop
