#include "harness.h"
#include "core/arts.h"
#include "core/crafting/forge_rules.h"
#include "core/crafting/material_sources.h"
#include "core/crafting/material_supply.h"
#include "core/crafting/tianji_steward.h"
#include "core/interface/steward_api.h"
#include "core/play.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/shop.h"
#include <algorithm>
#include <limits>

namespace nztest {
using namespace nz;
void runTianjiStewardTests() {
    Game State = createGame(CreateOptions{});
    State.pending.reset(); State.job.reset(); State.player.arts.battle.reset();
    State.player.location = "wuling"; State.player.stage = 3; State.player.stones = 1000000000;
    const string Mineral = "fm-2-fire-yang-metal";
    const auto Market = play::marketList(State);
    check(std::any_of(Market.begin(), Market.end(), [&](const auto& Item) { return Item.id == Mineral; }), "二阶新矿物进入普通坊市");
    check(play::marketBuy(State, "material", Mineral, 10).empty(), "二阶新矿物实际采购");
    crafting::ForgeRequest Request{{Mineral, Mineral, Mineral, Mineral, Mineral, Mineral, Mineral, Mineral, Mineral, Mineral}, Mineral, "sword", "damage", ""};
    check(crafting::mfQuoteForge(State, Request).Valid, "普通购买材料进入真实炼器预览");
    for (const string Id : {"fm-1-fire-yang-skin", "fm-3-fire-yang-metal", "bottle-spirit-6", "fm-invalid"}) {
        const string Before = saveToJson(State);
        check(!play::marketBuy(State, "material", Id, 1).empty() && saveToJson(State) == Before, "普通坊市拒绝不开放/高阶/专属材料且原子");
    }
    State.player.stage = 0;
    check(!crafting::mfCanBuyOrdinaryForgeMaterial(State.player, Mineral), "炼气不能买二阶矿物");
    const auto& Steward = social::mfTianjiStewards().front();
    const string OfferId = "potion-5205";
    const string BeforeQuery = saveToJson(State);
    const auto Quote = crafting::mfQuoteTianjiPurchase(State, Steward.Id, OfferId, 1);
    check(Quote.Enabled && Quote.TotalCost == Quote.FixedUnitPrice, "提前购买突破丹不限制服用境界");
    check(saveToJson(State) == BeforeQuery, "执事报价无状态/时钟/RNG副作用");
    for (const double MarketValue : {0.01, 0.88, 1.16, 10.0}) {
        State.world.market = MarketValue; State.player.renown = 1000; State.player.talent = "merchant";
        checkEq(crafting::mfQuoteTianjiPurchase(State, Steward.Id, OfferId, 1).FixedUnitPrice, Quote.FixedUnitPrice, "执事固定价不受行情声望商贾影响");
        check(shop::salePrice(State, "potion", "5205") < Quote.FixedUnitPrice, "全部合法行情下突破丹购买再出售无套利");
    }
    for (const auto& Offer : crafting::mfTianjiOffers()) {
        if (Offer.ItemKind == "potion") check(shop::salePrice(State, "potion", Offer.ItemId) < Offer.FixedUnitPrice, "全部上架突破丹最高行情转售低于固定采购价");
    }
    const int BeforeStones = State.player.stones;
    check(crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, 1, Quote.TotalCost).empty(), "执事实际购买成功");
    checkEq(State.player.stones, BeforeStones - Quote.TotalCost, "执事按固定总价扣费");
    check(!artPillUseInfo(State.player, *findRecipe("5205")).reason.empty(), "提前购买不绕过突破丹实际服用资格");
    check(crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, 1, Quote.TotalCost).empty(), "无限货架可再次真实购买");
    for (const int Count : {0, -1, 1001, std::numeric_limits<int>::max()}) {
        const string Before = saveToJson(State);
        check(!crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, Count, 0).empty() && saveToJson(State) == Before, "非法数量整单拒绝");
    }
    for (const string Id : {"potion-5520", "material-fm-3-fire-yang-skin", "method-unknown"})
        check(!crafting::mfQuoteTianjiPurchase(State, Steward.Id, Id, 1).Enabled, "白名单拒绝六阶丹/兽材/空壳秘法");
    const string BeforeStale = saveToJson(State);
    check(!crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, 1, Quote.TotalCost + 1).empty() && saveToJson(State) == BeforeStale, "过期报价不扣款");
    State.player.location = "yunxi";
    check(!crafting::mfQuoteTianjiPurchase(State, Steward.Id, OfferId, 1).Enabled, "异地执事交易被拒绝");
    State.player.location = "wuling"; State.player.stones = 0;
    const string BeforePoor = saveToJson(State);
    check(!crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, 1, Quote.TotalCost).empty() && saveToJson(State) == BeforePoor, "余额不足无部分提交");
    checkEq(social::mfTianjiStewards().size(), 5, "五城固定执事目录");
    State.player.stones = 1000000000;
    State.player.arts.potions["5205"] = 1000000000;
    const string BeforeCapacity = saveToJson(State);
    check(!crafting::mfBuyTianjiOffer(State, Steward.Id, OfferId, 1, Quote.TotalCost).empty() && saveToJson(State) == BeforeCapacity, "丹药库存上限原子拒绝");
    json::Value Input(json::Object{});
    Input.set("npc", Steward.Id); Input.set("offer", OfferId); Input.set("count", 1); Input.set("expectedCost", Quote.TotalCost);
    Input.set("ignored", true);
    check(!api::mfExecuteTianjiStewardPurchase(State, Input).empty(), "执事接口拒绝未知键");
    Input.asObject().erase("ignored"); Input.set("count", 1.5);
    check(!api::mfExecuteTianjiStewardPurchase(State, Input).empty(), "执事接口拒绝小数数量");
    Input.set("count", std::numeric_limits<double>::quiet_NaN());
    check(!api::mfExecuteTianjiStewardPurchase(State, Input).empty(), "执事接口拒绝NaN数量");
    Input.set("count", 1); Input.set("expectedCost", -1);
    check(!api::mfExecuteTianjiStewardPurchase(State, Input).empty(), "执事接口拒绝负报价金额");
    Input.asObject().erase("expectedCost");
    json::Value QuoteOutput("unchanged");
    check(api::mfReadTianjiPurchaseQuote(State, Input, QuoteOutput).empty() && QuoteOutput.isObject(), "严格执事报价接口消费领域纯报价");
    Input.set("count", 0); QuoteOutput = json::Value("unchanged");
    check(!api::mfReadTianjiPurchaseQuote(State, Input, QuoteOutput).empty() && QuoteOutput.asString() == "unchanged", "非法报价输入不改输出对象");
}
}
