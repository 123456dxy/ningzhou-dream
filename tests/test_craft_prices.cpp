#include "harness.h"
#include "core/arts.h"
#include "core/api.h"
#include "core/crafting/material_supply.h"
#include "core/crafting/craft_prices.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include <algorithm>
#include <limits>
#include <cmath>

namespace nztest {
namespace {
using namespace nz;

// 用途：创建无价格加成的隔离角色；流程：市场报价与真实买卖回归；返回：自有副本，无文件和玩家存档依赖。
Game mfPriceGame() {
    CreateOptions Options;
    Options.seed = 1543;
    Options.traitsExplicit = true;
    Game GameState = createGame(Options);
    GameState.pending.reset();
    GameState.job.reset();
    GameState.npcs.clear();
    GameState.world.market = 1.0;
    GameState.player.location = "wuling";
    GameState.player.stage = 3;
    GameState.player.stones = 1000000;
    GameState.player.renown = 0;
    GameState.player.traits.clear();
    GameState.player.arts.main.clear();
    GameState.player.arts.aux.clear();
    GameState.player.arts.manuals.clear();
    GameState.player.arts.nodes.clear();
    GameState.player.arts.gear.clear();
    GameState.player.arts.equipped.clear();
    GameState.player.arts.potions.clear();
    GameState.economy.sold.clear();
    GameState.economy.marketCash = 1000000;
    return GameState;
}
}

// 用途：验证材料市价驱动的成品报价；流程：报价、购买、出售、旧法器与拒绝路径；返回：无，断言真实资源变化。
void mfRunCraftPriceTests() {
    std::printf("\n--- 成品统一材料市价回归 ---\n");
    // 用途：锁定本地药材价格口径及高品成品收购；流程：数值修正定向回归；仅用独立角色。
    {
        struct PriceSample { const char* Id; int Grade; int MaterialCost; int Sale; int Purchase; };
        const PriceSample Samples[] = {{"5001", 1, 21, 32, 63}, {"5101", 2, 112, 168, 336},
            {"5202", 3, 616, 924, 1848}, {"5302", 4, 672, 1008, 2016},
            {"5402", 5, 812, 1218, 2436}, {"5502", 6, 1183, 1775, 3549},
            {"5504", 6, 490, 735, 1470}, {"5505", 6, 1512, 2268, 4536},
            {"5512", 6, 2520, 3780, 7560}};
        for (const auto& Sample : Samples) {
            Game State = mfPriceGame();
            State.player.stage = std::max(0, (Sample.Grade - 2) * 3);
            const auto Quote = crafting::mfQuotePotionPrice(State, Sample.Id);
            check(Quote.Valid && Quote.MaterialCost == Sample.MaterialCost &&
                  Quote.BaseSellPrice == Sample.Sale && Quote.BuyPrice == Sample.Purchase,
                  "逐品丹方采用本地price完整材料价值且保留一点五/三倍：" + string(Sample.Id));
            State.economy.month = -1;
            ensureEconomy(State);
            State.player.arts.potions[Sample.Id] = 1;
            check(shop::saleQuote(State, "potion", Sample.Id).enabled,
                  "实际可炼品阶的代表丹药在新月城中可足额出售：" + string(Sample.Id));
            const int BeforeCash = State.economy.marketCash;
            const auto Sold = shop::sell(State, "potion", Sample.Id);
            check(Sold.ok && Sold.gain == Sample.Sale && State.economy.marketCash == BeforeCash - Sample.Sale,
                  "逐品丹药真实出售按完整价格扣收购资金：" + string(Sample.Id));
        }
        Game State = mfPriceGame();
        for (const int Stage : {0, 2, 3, 5, 6, 8, 9, 11, 12}) {
            State.player.stage = Stage;
            checkEq(ecoMarketTier(State.player), artTier(State.player), "月资金保障覆盖实际炼制品阶及突破准备阶段");
            check(ecoMarketBudget(State) >= crafting::mfQuoteStandardGearPrice(State, artTier(State.player)).SellPrice,
                  "月资金同样保障本层级标准法器完整售值");
        }
        State.player.stage = 12;
        State.economy.month = State.clock.days / 30;
        State.economy.marketCash = 0;
        State.player.arts.potions["5505"] = 2;
        const auto Before = saveToJson(State);
        check(!shop::sell(State, "potion", "5505").ok, "零资金不能销售高品丹药");
        check(saveToJson(State) == Before, "零资金拒售不改变库存余额及需求账");
        ecoRefresh(State);
        checkEq(State.economy.marketCash, 0, "更新价格规则后同月零余额不得充值");
        Game Restored; string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "已耗尽预算的旧档可读回：" + Error);
        checkEq(Restored.economy.marketCash, 0, "旧档读取保留当月已经用尽的预算");
        Restored.clock.days += 30; ecoRefresh(Restored);
        checkEq(Restored.economy.marketCash, 0, "旧档进入下月仍不补充当前三年账期资金");
        check(!shop::saleQuote(Restored, "potion", "5505").enabled, "换月不能恢复已耗尽的收购资格");
        Restored.clock.days = 1080; Restored.year = 3; Restored.player.age = Restored.clock.ageBase + 3;
        ecoRefresh(Restored);
        check(shop::saleQuote(Restored, "potion", "5505").enabled, "旧档跨三年边界后可按当前价格足额收购");
        Restored.player.location = "yishan";
        const auto Outside = saveToJson(Restored);
        check(!shop::sell(Restored, "potion", "5505").ok, "高品收购不能绕过五城地点限制");
        check(saveToJson(Restored) == Outside, "非城地点拒售保持全部交易状态");
    }
    {
        struct SessionScope {
            api::Session Session;
            SessionScope() { api::bindSession(&Session); }
            ~SessionScope() { api::bindSession(nullptr); }
        } Scope;
        Scope.Session.g = mfPriceGame();
        Scope.Session.started = true;
        auto& State = Scope.Session.g;
        State.economy.marketCash = 199;
        State.economy.sold["potion:5001"] = 3;
        const auto Before = saveToJson(State);
        const auto Response = api::handle({"POST", "/api/state", "{}", ""});
        const auto View = json::Value::parse(Response.body)["state"];
        bool Found = false;
        for (const auto& Entry : View["brewOptions"].asArray()) if (Entry["id"].asString() == "5001") {
            Found = true;
            const auto Market = Entry["pricing"]["market"];
            checkEq(Market["remainingCash"].asInt(), 199, "真实状态报价显示本月已花费后的剩余资金");
            checkEq(Market["affordableCount"].asInt(), 6, "可售量按199资金除以32单价向下取整，旧销量与现有库存不限制资金报价");
            check(Market["locations"].asArray().size() == 5 && Market["inCity"].asBool(),
                  "价格报价公布实际五城收购渠道");
        }
        check(Found, "真实工坊报价包含待炼丹方");
        check(saveToJson(State) == Before, "定价与产销渠道报价不修改余额时间随机数或库存");
    }
    {
        Game GameState = mfPriceGame();
        const auto Before = saveToJson(GameState);
        checkEq(shop::salePrice(GameState, "potion", "5001"), 32, "化淤丹完整本地原料21的基础收购价为32");
        const auto Market = play::marketList(GameState);
        const auto Pill = std::find_if(Market.begin(), Market.end(), [](const auto& Item) { return Item.kind == "potion" && Item.id == "5001"; });
        check(Pill != Market.end() && Pill->price == 63 && Pill->sellPrice == 32, "化淤丹买价为原料价值三倍且列表收购价一致");
        const auto GearMarket = play::gearList(GameState);
        check(GearMarket.empty(), "普通坊市不列没有真实能力的标准法器商品");
        const auto StandardValue = crafting::mfQuoteStandardGearPrice(GameState, 1);
        check(StandardValue.Valid && StandardValue.BuyPrice == 240 && StandardValue.SellPrice == 120, "标准法器预算估值保留但不代表上架商品");
        check(saveToJson(GameState) == Before, "成品报价不改变库存、随机数、日历和存档状态");
    }
    for (const double MarketRate : {0.88, 1.0, 1.16}) {
        for (const int Renown : {0, 100000}) {
            for (const bool HasMerchant : {false, true}) {
                Game GameState = mfPriceGame();
                GameState.world.market = MarketRate;
                GameState.player.renown = Renown;
                if (HasMerchant) GameState.player.traits = {"merchant"};
                const auto Before = saveToJson(GameState);
                check(play::gearList(GameState).empty(), "所有行情声望商贾组合下普通坊市都无标准法器商品");
                const auto Error = play::marketBuy(GameState, "gear", "sword-1", 1);
                check(Error.find("成品法器未上架") != string::npos, "旧法器购买请求明确拒绝并指引自行炼制");
                check(saveToJson(GameState) == Before, "所有行情声望商贾组合下拒绝旧法器购买保持整档原子不变");
            }
        }
    }
    {
        Game GameState = mfPriceGame();
        Gear Forged;
        Forged.id = "price-ledger-forge";
        Forged.name = "锻剑铁剑";
        Forged.slot = "weapon";
        Forged.grade = 1;
        Forged.craftCost = 33;
        Forged.crafted = true;
        GameState.player.arts.gear.push_back(Forged);
        crafting::recordForgeMaterials(GameState, Forged.id, "iron", "copper", "gold");
        // 原料为4×8 + 3×32 + 3×72 = 344，不能用一阶估值80或铸炼费33替代。
        checkEq(shop::salePrice(GameState, "gear", Forged.id), 516, "混阶真实配方按全部344灵石材料价值收购");
        checkEq(ecoSalePrice(GameState, "gear", Forged.id), 516, "兼容法器报价委托同一个真实收购公式");
        const auto PreviewBefore = saveToJson(GameState);
        const auto Preview = crafting::mfQuoteForgePrice(GameState, "iron", "copper", "gold");
        check(Preview.Valid && Preview.MaterialCost == 344 && Preview.BaseSellPrice == 516 && Preview.SellPrice == 516,
              "工坊三料预览直接使用真实配方价值且与成品一致");
        const auto Repeated = crafting::mfQuoteForgePrice(GameState, "iron", "iron", "iron");
        check(Repeated.Valid && Repeated.MaterialCost == 80 && Repeated.BuyPrice == 240, "同一原料占三角色仍精确合计十份");
        check(!crafting::mfQuoteForgePrice(GameState, "iron", "missing", "gold").Valid, "三料报价拒绝任何未知材料");
        check(saveToJson(GameState) == PreviewBefore, "三料报价不写配料账或活动状态");
        GameState.player.arts.materials = {{"iron", 100}, {"copper", 100}, {"gold", 100}};
        GameState.player.arts.gear.front().craftCost = 999999;
        checkEq(shop::salePrice(GameState, "gear", Forged.id), 516, "已有库存与历史开工费不改变法器材料市场价值");
        GameState.flags["forge-materials-v1:" + Forged.id + ":iron"] = 3;
        const auto Before = saveToJson(GameState);
        check(!shop::sell(GameState, "gear", Forged.id).ok, "损坏的不完整法器配料账不允许当正常估价出售");
        check(saveToJson(GameState) == Before, "损坏配料拒售不改变交易次数库存和余额");
    }
    {
        Game GameState = mfPriceGame();
        Gear Forged;
        Forged.id = "price-extra-sale";
        Forged.slot = "weapon";
        Forged.grade = 1;
        Forged.crafted = true;
        Forged.inscribed = true;
        GameState.player.arts.gear.push_back(Forged);
        crafting::recordForgeMaterials(GameState, Forged.id, "iron", "copper", "gold");
        GameState.player.renown = 100;
        GameState.player.traits = {"artisan"};
        const auto Quote = crafting::mfQuoteGearPrice(GameState, Forged);
        // 当前材料单价7/27/61，4/3/3合计292；438×1.12×1.2×1.15=676.9728，成交677。
        check(Quote.Valid && Quote.MaterialCost == 292 && Quote.BaseSellPrice == 438 && Quote.SellPrice == 677,
              "铭刻声望匠心附加售值在材料基价之外保留且不重复乘行情");
        checkNear(Quote.InscriptionMultiplier, 1.12, "报价单独明示铭刻乘区");
        checkNear(Quote.RenownSaleMultiplier, 1.2, "报价单独明示声望出售乘区");
        checkNear(Quote.CraftedSaleMultiplier, 1.15, "报价单独明示自炼能力乘区");
        checkEq(shop::salePrice(GameState, "gear", Forged.id), 677, "额外加成后的最终价格进入真实收购入口");
    }
    {
        Game GameState = mfPriceGame();
        GameState.player.stones = std::numeric_limits<int>::max() - 10;
        GameState.player.arts.potions["5001"] = 1;
        const auto Before = saveToJson(GameState);
        check(!shop::sell(GameState, "potion", "5001").ok, "灵石余额溢出前拒绝出售");
        check(saveToJson(GameState) == Before, "溢出拒绝不触碰需求次数、丹药和资金池");
        check(!play::marketBuy(GameState, "potion", "missing", 1).empty(), "未知丹方购买明确拒绝");
        check(saveToJson(GameState) == Before, "未知丹方购买不发生部分交易");
        check(!play::marketBuy(GameState, "gear", "sword-1", 2).empty(), "法器单件交易拒绝数量2而不悄悄买一件");
        check(saveToJson(GameState) == Before, "非法法器数量不扣费不入库");
    }
    {
        Game GameState = mfPriceGame();
        GameState.world.market = std::numeric_limits<double>::quiet_NaN();
        check(!crafting::mfQuotePotionPrice(GameState, "5001").Valid, "非有限行情不能生成可成交丹药报价");
        check(!play::marketBuy(GameState, "potion", "5001", 1).empty(), "非有限行情下实际丹药购买被拒绝");
        checkEq(GameState.player.stones, 1000000, "非法行情不会免费发放或误扣灵石");
        check(GameState.player.arts.potions.empty(), "非法行情不会创建丹药库存项");
        GameState.world.market = 1.0;
        const auto SixthGrade = crafting::mfQuoteStandardGearPrice(GameState, 6);
        check(SixthGrade.Valid && SixthGrade.MaterialCost == 2880 && SixthGrade.BuyPrice == 8640,
              "无六阶材料目录的旧法器采用明示标准估值而非开工费用");
    }
}
}
