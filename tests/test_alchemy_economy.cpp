#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/crafting/material_supply.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;
// 用途：创建隔离炼丹夹具；流程：收益和真实结算回归；返回：无天赋功法加成、常价、材料空仓的游戏。
Game alchemyGame() {
    CreateOptions options; options.seed=1543; options.traitsExplicit=true;
    Game game=createGame(options);
    game.pending.reset(); game.job.reset(); game.npcs.clear(); game.world.market=1.0;
    auto& player=game.player; player.stones=1000000; player.location="wuling"; player.alchemy=30;
    player.traits.clear(); player.sect.clear(); player.renown=0;
    player.arts.main.clear(); player.arts.aux.clear(); player.arts.manuals.clear(); player.arts.nodes.clear();
    player.arts.herbs.clear(); player.herbs=0; player.arts.potions.clear();
    player.arts.furnace="furnace-1"; player.arts.durability=40;
    game.economy.sold.clear(); game.economy.marketCash=400;
    return game;
}
// 用途：准备指定炉数配料；流程：各结算路径回归；返回：无，只按真实配方入库。
void stockBrew(Game& game, const string& id, int batch=1) {
    const auto* recipe=findRecipe(id);
    for (const auto& [name,count]:parseIngredients(recipe->ingredients)) {
        game.player.arts.herbs[name]=count*batch; game.player.herbs+=count*batch;
    }
}
// 用途：找到真实随机数可覆盖的成败种子；流程：即时和年度动作；返回：不篡改成丹率的确定性种子。
uint32_t brewSeed(double chance, bool success) {
    for (uint32_t seed=1; seed<100000; ++seed) { Rng rng(seed); if ((rng.next()<chance)==success) return seed; }
    throw std::runtime_error("Missing deterministic brewing seed");
}
}

void runAlchemyEconomyTests() {
    std::printf("\n--- 炼丹收益与产量端到端回归 ---\n");
    {
        Game game=alchemyGame(); game.player.alchemy=10;
        const auto before=saveToJson(game);
        // 固定案例独立锁定真实采购成本与经济目标，避免只复述实现公式。
        struct Sample { const char* id; int materials, fee, sale; };
        for (const auto& row: {Sample{"5001",27,14,90}, Sample{"5101",144,20,361}, Sample{"5201",180,26,453}}) {
            const auto quote=crafting::materialQuote(game,"brew",row.id,1);
            check(quote.enabled,"一至三品代表丹方的低阶配料确实能采购");
            checkEq(quote.totalCost,row.materials,"全成本使用坊市真实药材采购报价");
            const auto brew=artBrewInfo(game.player,row.id);
            checkEq(brew.cost,row.fee,"开炉费用保持原值");
            checkEq(shop::salePrice(game,"potion",row.id),row.sale,"售价覆盖真实采购基价及失败风险");
            check(brew.chance*brew.output*row.sale-row.materials-row.fee-0.5>0,
                  "丹术10无增产天赋的代表丹方含耐久摊销后期望盈利");
        }
        const auto high=crafting::materialQuote(game,"brew","5202",1);
        check(!high.enabled,"高阶配料不因炼丹利润调整而出现在普通坊市");
        check(saveToJson(game)==before,"报价和收益查询不消耗资源、时间、随机数");
        checkEq(shop::salePrice(game,"potion","missing"),0,"未知丹方不可出售");
        checkEq(ecoSalePrice(game,"potion","missing"),0,"兼容报价同样拒绝未知丹方");
    }
    {
        bool allRetailSafe=true, allSameQuote=true;
        for (const double market: {0.88,1.0,1.16}) for (const int fame: {0,100000}) for (const bool merchant: {false,true}) {
            Game game=alchemyGame(); game.world.market=market; game.player.renown=fame;
            if (merchant) game.player.traits={"merchant"};
            for (const auto& item:play::marketList(game)) if (item.kind=="potion") {
                allRetailSafe &= item.price>item.sellPrice && item.sellPrice==shop::salePrice(game,"potion",item.id);
                allSameQuote &= ecoSalePrice(game,"potion",item.id)==item.sellPrice;
            }
        }
        check(allRetailSafe,"所有零售丹药在最低/正常/最高行情及最高声望商贾折扣下买入价均高于回收价");
        check(allSameQuote,"旧经济报价、坊市展示和实际收购共用丹药价格");
        Game game=alchemyGame(); game.world.market=0.88; game.player.renown=100000; game.player.traits={"merchant"};
        game.economy.marketCash=2400; const int stones=game.player.stones;
        check(play::marketBuy(game,"potion","5001",1).empty(),"真实最优惠购买丹药成功");
        const auto sale=shop::sell(game,"potion","5001");
        check(sale.ok && game.player.stones<stones,"真实买入后立即出售仍净亏损，不能套利");
    }
    {
        Game game=alchemyGame(); game.player.arts.potions["5001"]=20;
        const auto quote=shop::saleQuote(game,"potion","5001");
        const int stones=game.player.stones, cash=game.economy.marketCash;
        const auto result=shop::sell(game,"potion","5001");
        check(result.ok,"炼丹成品实际可卖给坊市");
        checkEq(result.gain,quote.returnStones,"报价与实际出售收入相同");
        checkEq(game.player.stones,stones+90,"真实出售增加新版灵石金额");
        checkEq(game.economy.marketCash,cash-90,"收购仍消耗商人资金");
        game.economy.marketCash=89;
        check(!shop::sell(game,"potion","5001").ok,"商人现金不足仍不能透支收购");
        game.economy.marketCash=400; game.economy.sold["potion:5001"]=8;
        check(!shop::sell(game,"potion","5001").ok,"既有同类货品每月需求上限不变");
    }
    {
        auto plain=alchemyGame().player;
        checkEq(artBrewInfo(plain,"5001").output,1,"普通人物每炉成功产出一枚");
        auto child=plain; child.traits={"alchemy"};
        checkEq(artBrewInfo(child,"5001").output,2,"丹火童子增产实际为每炉加一枚");
        auto dao=plain; dao.arts.nodes={"10-5"};
        checkEq(artBrewInfo(dao,"5001").output,2,"丹道大成节点单独增加一枚");
        child.arts.nodes={"10-5"};
        checkEq(artBrewInfo(child,"5001").output,3,"丹火童子与丹道大成可叠加");
        child.arts.main="pill"; child.arts.manuals={{"pill",5}};
        checkEq(artBrewInfo(child,"5001").output,4,"丹诀五层叠加后最多四枚");
        child.arts.aux={"pill"};
        checkEq(artBrewInfo(child,"5001").output,4,"即使叠加超额系数仍封顶四枚");
        plain.arts.main="pill"; plain.arts.manuals={{"pill",4}};
        checkEq(artBrewInfo(plain,"5001").output,1,"不足一枚的功法成丹系数不提前兑现");
    }
    // 真实收购资金链：按境界分级、整枚出售、耗尽阻断、次月刷新。
    for (const int stage : {0,3,6,9,12}) for (const double market : {0.88,1.0,1.16}) {
        Game game=alchemyGame(); game.player.stage=stage; game.world.market=market;
        const int tier=ecoMarketTier(game.player);
        checkEq(tier,stage==12?6:1+stage/3,"大境界对应稳定收购市场层级，化神覆盖六品");
        string expensive; int price=0;
        for (const auto& recipe:data::RecipeDef_TABLE()) if (recipe.grade<=tier) {
            const int value=shop::salePrice(game,"potion",recipe.id);
            if (value>price) { price=value; expensive=recipe.id; }
        }
        game.economy.month=-1; ensureEconomy(game);
        const int budget=ecoMarketBudget(game);
        checkEq(game.economy.marketCash,budget,"初始化采用统一月度预算");
        check(budget>=price,"每个市场层级至少能收购一枚该层级最贵丹药");
        checkEq(budget,std::max(400,price),"预算有明确有限上限，不无限注资");
        game.player.arts.potions[expensive]=2;
        const int stones=game.player.stones;
        const auto sale=shop::sell(game,"potion",expensive);
        check(sale.ok && sale.gain==price,"对应境界真实卖出完整高价值丹药");
        checkEq(game.player.stones,stones+price,"高阶丹实际获得全额灵石");
        checkEq(game.economy.marketCash,budget-price,"高阶收购足额扣减本月资金");
        const int remaining=game.economy.marketCash;
        check(!shop::sell(game,"potion",expensive).ok,"月资金耗尽后不能再收同价丹药");
        ecoRefresh(game);
        checkEq(game.economy.marketCash,remaining,"同月刷新不能重置已支出的预算");
        game.clock.days+=30; ecoRefresh(game);
        checkEq(game.economy.marketCash,budget,"进入下月才补充当期市场预算");
        check(game.economy.sold.empty(),"月度刷新同时清除旧需求计数");
        check(shop::sell(game,"potion",expensive).ok,"下月可以实际卖出下一枚高价值成品");
    }
    {
        Game game=alchemyGame(); game.economy.month=-1; ensureEconomy(game);
        checkEq(game.economy.marketCash,447,"炼气无声望预算以原400为基础，仅提升到一品最高单价");
        game.player.arts.potions["5512"]=1;
        const int stones=game.player.stones, cash=game.economy.marketCash;
        check(!shop::sell(game,"potion","5512").ok,"低阶角色不能凭高阶丹药透支当前层级资金");
        checkEq(game.player.stones,stones,"拒绝超预算交易不发放灵石");
        checkEq(game.economy.marketCash,cash,"拒绝超预算交易不修改商人现金");
        game.player.stage=12; ecoRefresh(game);
        checkEq(game.economy.marketCash,cash,"同月晋阶不会额外充值");
        Game restored; string error;
        check(loadFromJson(saveToJson(game),restored,&error),"保存读取晋阶后的市场状态");
        checkEq(restored.economy.marketCash,cash,"读取存档不按高境界重新发放本月资金");
        restored.clock.days+=30; ecoRefresh(restored);
        check(shop::sell(restored,"potion","5512").ok,"晋阶后次月六品成品可以变现");
        Game famous=alchemyGame(); famous.player.renown=100000;
        checkEq(ecoMarketBudget(famous),2400,"低阶高声望保留原2400额度为基础");
    }
    // 实际即时/年度动作分别覆盖成功和失败，验证加成没有只停留在预览层。
    for (const bool success: {false,true}) for (const bool annual: {false,true}) {
        Game game=alchemyGame(); game.player.traits={"alchemy"}; game.player.arts.nodes={"10-5"};
        stockBrew(game,"5001"); const auto brew=artBrewInfo(game.player,"5001");
        game.rng=brewSeed(brew.chance,success); const int stones=game.player.stones, durability=game.player.arts.durability;
        check(artAnnualError(game,"brew","5001").empty(),"真实炼丹夹具满足材料/丹炉资格");
        if (annual) artAnnual(game,"brew","5001");
        else { const auto result=play::craftDo(game,"brew","5001"); check(result.ok && result.success==success,"即时炼丹的成功状态由真实随机数决定"); }
        checkEq(game.player.arts.potions["5001"],success?3:0,"即时/年度实际成丹数量与天赋悟道预览一致，失败不发丹");
        checkEq(game.player.stones,stones-brew.cost,"即时/年度无论成败只扣一炉费");
        checkEq(game.player.arts.durability,durability-1,"即时/年度无论成败按炉损耗耐久");
        checkEq(game.player.herbs,0,"即时/年度只消耗该炉配料");
    }
    for (const int batch: {1,3,5,10}) {
        Game game=alchemyGame(); game.player.traits={"alchemy"}; game.player.arts.nodes={"10-5"};
        stockBrew(game,"5001",batch); const int stones=game.player.stones;
        const auto info=ecoActionInfo(game,"brew","5001",batch);
        ecoBegin(game,"brew","5001",batch);
        check(game.job.has_value(),"合法批次可以建立真实炼丹安排");
        if (!game.job) continue;
        checkEq(game.job->craftOutput,3,"批量开炉冻结天赋与悟道产量");
        checkEq(game.player.stones,stones-info.cost,"批量开炉只预扣整批炉费");
        checkEq(game.player.herbs,0,"批量开炉精确消耗整批原料");
        checkEq(game.player.arts.durability,40-batch,"批量耐久按炉数而非产丹数量扣除");
        // 开工后失去加成及存档往返也不能改写已经支付的产量合同。
        game.player.traits.clear(); game.player.arts.nodes.clear();
        Game restored; string error;
        check(loadFromJson(saveToJson(game),restored,&error),"炼丹安排可以保存并读取");
        if (!restored.job) continue;
        const Job job=*restored.job;
        checkEq(job.craftOutput,3,"读档保留已冻结每炉产量");
        checkEq(job.batch,batch,"读档保留真实批次数");
        restored.rng=1; Rng expected(restored.rng); int successes=0;
        for (int i=0;i<batch;++i) if (expected.next()<job.chance) ++successes;
        const int paidStones=restored.player.stones;
        restored.job.reset(); ecoComplete(restored,job);
        checkEq(restored.player.arts.potions["5001"],successes*3,"批量结算按真实成功炉数乘开工冻结产量");
        checkEq(restored.player.stones,paidStones,"批量结束不再次扣炉费");
    }
    {
        struct SessionScope { api::Session session; SessionScope(){api::bindSession(&session);} ~SessionScope(){api::bindSession(nullptr);} } scope;
        scope.session.g=alchemyGame(); scope.session.g.player.traits={"alchemy"}; scope.session.started=true;
        const auto before=saveToJson(scope.session.g);
        http::Request request; request.method="POST"; request.path="/api/state"; request.body="{}";
        const auto response=api::handle(request);
        const auto state=json::Value::parse(response.body)["state"];
        bool found=false;
        for (const auto& entry:state["brewOptions"].asArray()) if (entry["id"].asString()=="5001") {
            found=true; const auto brew=artBrewInfo(scope.session.g.player,"5001");
            checkEq(entry["output"].asInt(),2,"真实状态接口下发丹童成丹数");
            checkEq(entry["sellPrice"].asInt(),90,"真实状态接口下发统一成品收购价");
            checkNear(entry["expectedOutput"].asNumber(),brew.chance*2,"真实状态接口下发含失败风险的期望产量");
            checkNear(entry["expectedSaleIncome"].asNumber(),brew.chance*2*90,"收入预览由后端统一计算");
        }
        check(found,"状态接口保留化淤丹炼制选项");
        check(saveToJson(scope.session.g)==before,"经济展示接口不推进时间或修改存档");
    }
}
}