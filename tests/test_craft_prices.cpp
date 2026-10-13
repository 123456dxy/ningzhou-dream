#include "harness.h"
#include "core/arts.h"
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
    {
        Game GameState = mfPriceGame();
        const auto Before = saveToJson(GameState);
        checkEq(shop::salePrice(GameState, "potion", "5001"), 41, "化淤丹完整原料27的基础收购价为41");
        const auto Market = play::marketList(GameState);
        const auto Pill = std::find_if(Market.begin(), Market.end(), [](const auto& Item) { return Item.kind == "potion" && Item.id == "5001"; });
        check(Pill != Market.end() && Pill->price == 81 && Pill->sellPrice == 41, "化淤丹买价为原料价值三倍且列表收购价一致");
        const auto GearMarket = play::gearList(GameState);
        const auto Sword = std::find_if(GearMarket.begin(), GearMarket.end(), [](const auto& Item) { return Item.id == "sword-1"; });
        check(Sword != GearMarket.end() && Sword->price == 240 && Sword->sellPrice == 120, "标准一阶法器十份8灵石原料给出240购入和120收购报价");
        check(saveToJson(GameState) == Before, "成品报价不改变库存、随机数、日历和存档状态");
    }
    for (const double MarketRate : {0.88, 1.0, 1.16}) {
        for (const int Renown : {0, 100000}) {
            for (const bool HasMerchant : {false, true}) {
                Game GameState = mfPriceGame();
                GameState.world.market = MarketRate;
                GameState.player.renown = Renown;
                if (HasMerchant) GameState.player.traits = {"merchant"};
                const int BeforeStones = GameState.player.stones;
                const auto List = play::gearList(GameState);
                const auto Offer = std::find_if(List.begin(), List.end(), [](const auto& Item) { return Item.id == "sword-1"; });
                check(Offer != List.end(), "标准一阶法器在普通坊市有报价");
                if (Offer == List.end()) continue;
                check(play::marketBuy(GameState, "gear", "sword-1", 1).empty(), "真实购买一阶法器成功");
                const auto& Purchased = GameState.player.arts.gear.back();
                const string GearId = Purchased.id;
                checkEq(shop::salePrice(GameState, "gear", GearId), Offer->sellPrice, "真实买入后法器收购价等于购前列表报价");
                const auto Result = shop::sell(GameState, "gear", GearId);
                check(Result.ok && GameState.player.stones < BeforeStones, "所有行情声望商贾组合下买入立即卖出不能套利");
                checkEq(Result.gain, Offer->sellPrice, "法器实际收购金额与统一预览一致");
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
