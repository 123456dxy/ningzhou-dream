#include "harness.h"

#include "core/content/forge_content.h"
#include "core/crafting/forge_rules.h"
#include "core/crafting/material_supply.h"
#include "core/crafting/craft_prices.h"
#include "core/economy.h"
#include "core/shop.h"

#include <algorithm>
#include <set>

namespace nztest {
namespace {

nz::Game mfForgeTestGame() {
    nz::Game GameState;
    GameState.player.stage = 12;
    GameState.player.health = 100;
    GameState.player.stones = 100000;
    GameState.player.arts.exp["器"] = 100000;
    return GameState;
}

nz::crafting::ForgeRequest mfForgeTestRequest(nz::Game& GameState) {
    nz::crafting::ForgeRequest Request;
    Request.MaterialIds = {"iron", "iron", "iron", "iron", "jade", "jade", "wood", "wood", "wood", "wood"};
    Request.MainMaterialId = "iron";
    Request.ShapeId = "sword";
    Request.EffectId = "redraw";
    for (const auto& MaterialId : Request.MaterialIds) GameState.player.arts.materials[MaterialId]++;
    return Request;
}

}

void mfRunForgeRedesignTests() {
    std::printf("\n-- 自由投料炼器与冻结合同 --\n");
    {
        const auto Materials = nz::content::mfForgeMaterials();
        check(Materials.size() >= 575, "材料目录包含旧15种与完整八属性阴阳五阶素材");
        std::set<std::string> UniqueIds;
        bool AllValid = true;
        for (const auto& Definition : Materials) {
            AllValid &= UniqueIds.insert(Definition.Material.id).second;
            AllValid &= Definition.Dimensions.size() == 5 && Definition.Energy > 0;
        }
        check(AllValid, "目录材料稳定ID唯一且五维器能有效");
        check(nz::content::mfFindForgeMaterial("iron") != nullptr, "旧锻剑铁仍可用于新炼器");
        check(nz::content::mfFindForgeMaterial("fm-5-spirit-yin-heart") != nullptr, "五阶神识地阴妖心已登记");
        check(nz::content::mfFindForgeMaterial("fm-5-mixed-yang-stone") != nullptr, "五阶混元天阳石材已登记");
        check(nz::content::mfForgeEffectAllowed("redraw", "weapon", "气"), "混元五行剑允许主动等量换牌");
        check(!nz::content::mfForgeEffectAllowed("fire_apply", "weapon", "水"), "水主材不能选择火道叠焰");
    }
    {
        auto GameState = mfForgeTestGame();
        const auto Request = mfForgeTestRequest(GameState);
        const auto RandomBefore = GameState.rng;
        const auto ClockBefore = GameState.clock.days;
        const auto StonesBefore = GameState.player.stones;
        const auto Quote = nz::crafting::mfQuoteForge(GameState, Request);
        check(Quote.Valid && Quote.Enabled, "十份已拥有材料允许开工");
        checkEq(Quote.MaterialCount, 10, "逐格投入总量为十份");
        checkEq(Quote.Materials.size(), 3, "重复材料合并为三种物品需求");
        checkEq(Quote.DimensionScore, 2250, "四金属两石四灵物五维封顶得2250分");
        checkNear(Quote.Utilization, 1, "三材类配比同样能满利用率");
        checkEq(GameState.rng, RandomBefore, "只读报价不推进随机数");
        checkEq(GameState.clock.days, ClockBefore, "只读报价不推进时间");
        checkEq(GameState.player.stones, StonesBefore, "只读报价不扣灵石");
    }
    {
        auto GameState = mfForgeTestGame();
        auto Request = mfForgeTestRequest(GameState);
        Request.MaterialIds.resize(6);
        Request.MaterialIds.push_back("");
        const auto Quote = nz::crafting::mfQuoteForge(GameState, Request);
        check(Quote.Valid && !Quote.Enabled, "不足十份仍可预览但不能开工");
        checkEq(Quote.MaterialCount, 6, "空格不计入投入份数");
        Request.MaterialIds.push_back("missing-material");
        check(!nz::crafting::mfQuoteForge(GameState, Request).Valid, "未知非空材料ID明确拒绝");
    }
    {
        auto GameState = mfForgeTestGame();
        const auto Request = mfForgeTestRequest(GameState);
        const auto Quote = nz::crafting::mfQuoteForge(GameState, Request);
        const auto InventoryBefore = GameState.player.arts.materials;
        const auto StonesBefore = GameState.player.stones;
        check(!nz::crafting::mfBeginForge(GameState, Request, Quote.CostSpiritStoneCount + 1).empty(), "过期费用拒绝开工");
        check(GameState.player.arts.materials == InventoryBefore && GameState.player.stones == StonesBefore && !GameState.job,
              "拒绝的费用确认不扣材料或灵石");
        check(nz::crafting::mfBeginForge(GameState, Request, Quote.CostSpiritStoneCount).empty(), "核验当前报价后成功开工");
        check(GameState.job && GameState.job->ForgeProduct.has_value(), "开工冻结完整成品合同");
        if (GameState.job && GameState.job->ForgeProduct) {
            const auto Contract = *GameState.job;
            checkStr(Contract.ForgeProduct->EffectId, "redraw", "合同冻结等量换牌效果");
            checkEq(Contract.ForgeProduct->SourceMaterials.at("iron"), 4, "合同冻结实际四份同名材料");
            checkEq(GameState.player.arts.materials["iron"], 0, "开工实际扣完四份锻剑铁");
            GameState.player.stage = 0;
            GameState.player.arts.exp["器"] = 0;
            auto GuaranteedContract = Contract;
            GuaranteedContract.chance = 1;
            check(nz::crafting::mfCompleteForge(GameState, GuaranteedContract), "新版合同由新版完成入口处理");
            checkEq(GameState.player.arts.gear.size(), 1, "成功合同实际产生一件法器");
            if (!GameState.player.arts.gear.empty()) {
                const auto& Product = GameState.player.arts.gear.back();
                checkEq(Product.grade, Quote.Product.grade, "后续属性变化不重算冻结品阶");
                checkNear(Product.EffectPower, Quote.Product.EffectPower, "后续属性变化不重算冻结效果强度");
                check(Product.SourceMaterials == Quote.Product.SourceMaterials, "成品保留精确投料用于拆解");
                GameState.job.reset();
                const auto Salvage = nz::crafting::salvageQuote(GameState, Product.id);
                check(Salvage.enabled && !Salvage.estimated, "新法器按冻结投料真实报价拆解");
                checkEq(Salvage.materials.size(), 3, "拆解保留三种原材料的返还明细");
                GameState.player.arts.nodes.push_back("11-1");
                check(!nz::shop::gearActionQuote(GameState, "inscribe", Product.id).enabled, "新版法器不能再附加旧式铭刻强度");
            }
        }
        check(!nz::crafting::mfCompleteForge(GameState, nz::Job{}), "旧工作没有新版合同标记则保留旧规则");
    }
    {
        auto GameState = mfForgeTestGame();
        nz::crafting::ForgeRequest Request;
        Request.ShapeId = "box";
        Request.EffectId = "draw";
        for (const auto& Material : nz::content::mfForgeMaterials()) {
            if (Material.Source != nz::content::ForgeMaterialSource::BeastDrop || Material.Material.grade != 2 || Material.Material.element != "水") continue;
            Request.MaterialIds.push_back(Material.Material.id);
            GameState.player.arts.materials[Material.Material.id] = 1;
            if (Request.MaterialIds.size() == 10) break;
        }
        if (!Request.MaterialIds.empty()) Request.MainMaterialId = Request.MaterialIds.front();
        const auto Quote = nz::crafting::mfQuoteForge(GameState, Request);
        check(Quote.Enabled && Quote.Materials.size() == 10, "允许十种不同ID逐份投入，不限制五个物品ID");
        checkEq(Quote.Grade, 2, "十份同阶新素材产生对应阶法器");
        checkEq(Quote.Product.ForgeVersion, 2, "成品记录新版规则标记");
        checkNear(Quote.Product.bonus, 0, "新版主效果不重复领取旧通用完整加成");
        if (!Request.MaterialIds.empty()) {
            GameState.player.arts.materials[Request.MaterialIds.front()] = 0;
            const auto Before = GameState.player.stones;
            check(!nz::crafting::mfBeginForge(GameState, Request).empty(), "缺任意材料整炉拒绝开工");
            checkEq(GameState.player.stones, Before, "缺料失败不会部分扣灵石");
        }
        Request.MaterialIds.push_back("");
        check(!nz::crafting::mfQuoteForge(GameState, Request).Valid, "超过十个投料格包括空格也拒绝");
    }
    {
        auto GameState = mfForgeTestGame();
        auto Request = mfForgeTestRequest(GameState);
        Request.MaterialIds[0] = "bottle-spirit-6";
        GameState.player.arts.materials["bottle-spirit-6"] = 1;
        GameState.player.stage = 0;
        check(!nz::crafting::mfQuoteForge(GameState, Request).Enabled, "低阶材料混六阶专属材料也要求化神");
        GameState.player.stage = 12;
        check(nz::crafting::mfQuoteForge(GameState, Request).Enabled, "化神可将六阶材料用于五阶以下成器");
        Request.ShapeId = "pearl";
        Request.EffectId = "turn_shield";
        Request.InscriptionId = "white";
        check(!nz::crafting::mfQuoteForge(GameState, Request).Valid, "被动辅助不能无代价附加主动灵纹收益");
    }
    {
        auto GameState = mfForgeTestGame();
        auto Request = mfForgeTestRequest(GameState);
        Request.MaterialIds.assign(10, "iron");
        GameState.player.arts.materials["iron"] = 10;
        Request.InscriptionId = "azure";
        const auto Quote = nz::crafting::mfQuoteForge(GameState, Request);
        check(Quote.Valid && !Quote.Enabled, "单材类器能不足以同时承载主效果和灵纹");
        Request.InscriptionId.clear();
        check(nz::crafting::mfQuoteForge(GameState, Request).Enabled, "相同单材类十份材料仍可无灵纹成器");
    }
    {
        auto GameState = mfForgeTestGame();
        nz::Job LegacyContract;
        LegacyContract.actionType = "forge";
        LegacyContract.destination = "iron|jade|wood|sword";
        LegacyContract.name = "铸炼锻剑铁剑";
        LegacyContract.chance = 1;
        LegacyContract.cost = 33;
        LegacyContract.total = 30;
        nz::ecoComplete(GameState, LegacyContract);
        checkEq(GameState.player.arts.gear.size(), 1, "真实完成入口继续支持旧三料在途合同");
        if (!GameState.player.arts.gear.empty()) {
            const auto& Product = GameState.player.arts.gear.back();
            checkEq(Product.ForgeVersion, 0, "旧在途合同不会自动变成新词条法器");
            check(Product.bonus > 0, "旧在途合同保留旧实际通用属性");
        }
    }
    {
        auto GameState = mfForgeTestGame();
        nz::crafting::ForgeRequest Request;
        Request.MaterialIds = {
            "fm-2-water-yang-skin", "fm-2-water-yang-skin",
            "fm-2-water-yang-bone", "fm-2-water-yang-bone",
            "fm-2-water-yang-eye", "fm-2-water-yang-eye",
            "fm-2-water-yang-metal", "fm-2-water-yang-metal",
            "fm-2-water-yang-stone", "fm-2-water-yang-stone"
        };
        Request.MainMaterialId = Request.MaterialIds.front();
        Request.ShapeId = "box";
        Request.EffectId = "redraw";
        for (const auto& MaterialId : Request.MaterialIds) ++GameState.player.arts.materials[MaterialId];
        check(nz::crafting::mfBeginForge(GameState, Request).empty(), "已拥有的妖兽部位素材可以真实成器");
        if (GameState.job) {
            auto Contract = *GameState.job;
            Contract.chance = 1;
            GameState.job.reset();
            nz::ecoComplete(GameState, Contract);
            checkEq(GameState.player.arts.gear.size(), 1, "新版完成合同通过真实ecoComplete发放成品");
            if (!GameState.player.arts.gear.empty()) {
                const auto ProductId = GameState.player.arts.gear.back().id;
                const auto Salvage = nz::crafting::salvageQuote(GameState, ProductId);
                check(Salvage.enabled && Salvage.materials.size() == 5, "新增ID也按五种原料各返还一份");
                check(nz::crafting::salvageMaterials(GameState, ProductId).empty(), "新增目录素材的真实分解可执行");
                checkEq(GameState.player.arts.gear.size(), 0, "分解移除原成品");
                checkEq(GameState.player.arts.materials[Request.MainMaterialId], 1, "真实分解按准确ID返还材料");
            }
        }
    }
    {
        auto GameState = mfForgeTestGame();
        nz::crafting::ForgeRequest Request;
        Request.MaterialIds.assign(4, "iron");
        Request.MaterialIds.insert(Request.MaterialIds.end(), 6, "fm-5-spirit-yin-heart");
        Request.MainMaterialId = "iron";
        Request.ShapeId = "box";
        Request.EffectId = "redraw";
        for (const auto& MaterialId : Request.MaterialIds) ++GameState.player.arts.materials[MaterialId];
        const auto Forge = nz::crafting::mfQuoteForge(GameState, Request);
        const auto ExpectedMaterialCost = 4 * nz::crafting::mfNormalBuyPrice(GameState, 8) +
            6 * nz::crafting::mfNormalBuyPrice(GameState, 200);
        const auto PreviewPrice = nz::crafting::mfQuoteGearPrice(GameState, Forge.Product);
        check(PreviewPrice.Valid && !PreviewPrice.UsesEstimatedMaterials, "未产生装备ID的新版成品预览仍采用真实投料估值");
        checkEq(PreviewPrice.MaterialCost, ExpectedMaterialCost, "高低阶混料按实际四份低阶六份高阶市值计价");
        auto Product = Forge.Product;
        Product.id = "forge-price-test";
        const auto ActualPrice = nz::crafting::mfQuoteGearPrice(GameState, Product);
        checkEq(ActualPrice.SellPrice, PreviewPrice.SellPrice, "成器前后材料明细一致则回收售价一致");
        Product.SourceMaterials.erase("iron");
        check(!nz::crafting::mfQuoteGearPrice(GameState, Product).Valid, "新版残缺投料不会退回同阶标准价");
    }
}

} // namespace nztest
