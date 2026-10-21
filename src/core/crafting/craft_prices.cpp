#include "core/crafting/craft_prices.h"

#include "core/arts.h"
#include "core/content/forge_content.h"
#include "core/renown.h"
#include "core/talents.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace nz::crafting {
namespace {
constexpr int StandardForgeMaterialCount = 10;
constexpr int StandardPillCount = 1;
constexpr int MinimumGearGrade = 1;
constexpr int MaximumGearGrade = 6;
constexpr int LegacySixthGradeMaterialBasePrice = 288;
constexpr double InscribedSaleMultiplier = 1.12;
constexpr CraftEconomyRules EconomyRules{};
static_assert(EconomyRules.BaseSellNumerator > 0 && EconomyRules.BaseSellDenominator > 0 &&
    EconomyRules.BaseBuyMultiplier > 0 && EconomyRules.MonthlyBudgetMultiplier > 0 &&
    EconomyRules.MaximumMonthlyBudget > 0 && EconomyRules.MinorStagesPerMajorRealm > 0 &&
    EconomyRules.MaximumMajorRealmIndex >= 0 && EconomyRules.MaximumMajorRealmIndex < 30);
constexpr std::int64_t MaximumQuotedMaterialCost = std::numeric_limits<int>::max() /
    std::max(EconomyRules.BaseBuyMultiplier, EconomyRules.BaseSellNumerator);

// 用途：构造明确失败的报价；流程：所有只读价格校验；返回：Valid为假且持有原因的值。
CraftPriceQuote mfPriceFailure(const std::string& Reason) {
    CraftPriceQuote Quote;
    Quote.Reason = Reason;
    return Quote;
}

// 用途：在整数乘法前限制正值原料总额；流程：丹药及法器材料累加；返回：总额是否仍可生成安全三倍买价。
bool mfAddMaterialValue(std::int64_t& MaterialCost, int UnitPrice, int MaterialCount) {
    if (UnitPrice <= 0 || MaterialCount <= 0) return false;
    const std::int64_t RowCost = static_cast<std::int64_t>(UnitPrice) * MaterialCount;
    if (RowCost > MaximumQuotedMaterialCost - MaterialCost) return false;
    MaterialCost += RowCost;
    return true;
}

// 用途：由完整原料价值生成基础1.5/3报价；流程：所有成品共用，不包括炉费、库存抵扣或重复行情。
// 返回：整数报价，正值半枚向上取整；无法在int表示时返回失败，不饱和伪造可成交价。
CraftPriceQuote mfBaseCraftQuote(std::int64_t MaterialCost, int StandardOutput, bool UsesEstimatedMaterials) {
    if (MaterialCost <= 0 || MaterialCost > MaximumQuotedMaterialCost || StandardOutput <= 0)
        return mfPriceFailure("材料市价或成品报价超过支持范围。");
    CraftPriceQuote Quote;
    Quote.MaterialCost = static_cast<int>(MaterialCost);
    Quote.StandardOutput = StandardOutput;
    // 分母固定是标准产量；增产功法或天赋影响收益数量，不降低同一枚丹的市场价值。
    const std::int64_t BuyNumerator = MaterialCost * EconomyRules.BaseBuyMultiplier;
    Quote.BuyPrice = static_cast<int>((BuyNumerator + StandardOutput / 2) / StandardOutput);
    const std::int64_t SaleDenominator = static_cast<std::int64_t>(EconomyRules.BaseSellDenominator) * StandardOutput;
    Quote.BaseSellPrice = static_cast<int>((MaterialCost * EconomyRules.BaseSellNumerator + SaleDenominator / 2) / SaleDenominator);
    Quote.SellPrice = Quote.BaseSellPrice;
    Quote.UsesEstimatedMaterials = UsesEstimatedMaterials;
    Quote.Valid = Quote.BuyPrice > 0 && Quote.BaseSellPrice > 0;
    return Quote;
}

// 用途：对法器基础售值应用独立的既有加成；流程：现货回收和标准法器列表。
// 返回：含各乘区和最终整数售值的报价；非法倍率或金额越界失败，无游戏状态变化。
CraftPriceQuote mfApplyGearSaleBonuses(const Game& GameState, const Gear& GearState, CraftPriceQuote Quote) {
    if (!Quote.Valid) return Quote;
    Quote.InscriptionMultiplier = GearState.inscribed ? InscribedSaleMultiplier : 1.0;
    Quote.RenownSaleMultiplier = 1.0 + renown::sellBonus(GameState.player.renown);
    const bool IsPlayerForged = GearState.crafted && GearState.Origin != "event" && GearState.Origin != "talent";
    Quote.CraftedSaleMultiplier = IsPlayerForged ? 1.0 + talentMods(GameState.player).craftedSale : 1.0;
    Quote.SaleMultiplier = Quote.InscriptionMultiplier * Quote.RenownSaleMultiplier * Quote.CraftedSaleMultiplier;
    const double FinalSaleValue = Quote.BaseSellPrice * Quote.SaleMultiplier;
    if (!std::isfinite(FinalSaleValue) || FinalSaleValue <= 0.0 ||
        FinalSaleValue > std::numeric_limits<int>::max() - 0.5)
        return mfPriceFailure("法器额外售值或最终报价超过支持范围。");
    Quote.SellPrice = static_cast<int>(std::floor(FinalSaleValue + 0.5));
    return Quote;
}

// 用途：查询无真实配料账法器的同阶标准原料价值；流程：普通铺、掉落、NPC及旧档法器。
// 返回：十份同阶材料完整价值；第六阶无目录材料，显式使用旧物估价基值288/份，不读取craftCost。
CraftPriceQuote mfStandardGearMaterialQuote(const Game& GameState, int Grade) {
    if (Grade < MinimumGearGrade || Grade > MaximumGearGrade)
        return mfPriceFailure("法器品阶必须为一至六阶。");
    int UnitPrice = 0;
    for (const auto& Material : data::MaterialDef_TABLE()) {
        if (Material.grade == Grade) {
            UnitPrice = mfNormalMaterialBuyPrice(GameState, MaterialPriceKind::ForgeMaterial, Material.id);
            break;
        }
    }
    if (Grade == MaximumGearGrade)
        UnitPrice = mfNormalBuyPrice(GameState, LegacySixthGradeMaterialBasePrice);
    std::int64_t MaterialCost = 0;
    if (!mfAddMaterialValue(MaterialCost, UnitPrice, StandardForgeMaterialCount))
        return mfPriceFailure("同阶法器标准材料市场价无效。");
    return mfBaseCraftQuote(MaterialCost, 1, true);
}
} // namespace

const CraftEconomyRules& mfCraftEconomyRules() { return EconomyRules; }

// 用途：折算正常采购价格；流程：普通材料报价的唯一倍率实现；返回：安全整数单价，非法数值为0。
int mfNormalBuyPrice(const Game& GameState, int BasePrice) {
    if (BasePrice <= 0 || !std::isfinite(GameState.world.market) || GameState.world.market <= 0.0) return 0;
    const double MarketMultiplier = 1.0 + (GameState.world.market - 1.0) * 0.5;
    const double ReputationMultiplier = 1.0 - renown::buyDiscount(GameState.player.renown);
    const double MerchantMultiplier = hasTalent(GameState.player, "merchant") ? 0.9 : 1.0;
    const double PurchaseValue = BasePrice * MarketMultiplier * ReputationMultiplier * MerchantMultiplier;
    if (!std::isfinite(PurchaseValue) || PurchaseValue <= 0.0 ||
        PurchaseValue > std::numeric_limits<int>::max() - 0.5) return 0;
    return std::max(1, static_cast<int>(std::floor(PurchaseValue + 0.5)));
}

// 用途：读取材料目录并生成当前正常单价；流程：兼容普通铺及新制作经济；返回：纯价格，未知材料为0。
int mfNormalMaterialBuyPrice(const Game& GameState, MaterialPriceKind Kind, const std::string& MaterialId) {
    switch (Kind) {
    case MaterialPriceKind::Herb: {
        const auto* Herb = findHerbById(MaterialId);
        if (!Herb) return 0;
        // price是本地经济目录值；sourcePrice是导入参考值，六阶两者相差六百余倍，不能混用。
        const int BasePrice = Herb->price;
        const int LocalBasePrice = Herb->sourceLocation == GameState.player.location ? jsRound(BasePrice * 0.8) : BasePrice;
        return mfNormalBuyPrice(GameState, LocalBasePrice);
    }
    case MaterialPriceKind::ForgeMaterial:
        if (const auto* Material = content::mfFindForgeMaterial(MaterialId))
            return mfNormalBuyPrice(GameState, Material->Material.price);
        return 0;
    }
    return 0;
}

// 用途：查询丹方完整原料市场价值；流程：成品买卖和收益报价；返回：纯报价，不提交任何状态。
CraftPriceQuote mfQuotePotionPrice(const Game& GameState, const std::string& RecipeId) {
    const auto* Recipe = findRecipe(RecipeId);
    if (!Recipe) return mfPriceFailure("没有这个丹方。");
    std::int64_t MaterialCost = 0;
    for (const auto& [HerbName, MaterialCount] : parseIngredients(Recipe->ingredients)) {
        const auto* Herb = findHerbByName(HerbName);
        if (!Herb) return mfPriceFailure("丹方包含未知药材。");
        const int UnitPrice = mfNormalMaterialBuyPrice(GameState, MaterialPriceKind::Herb, Herb->id);
        if (!mfAddMaterialValue(MaterialCost, UnitPrice, MaterialCount))
            return mfPriceFailure("丹方材料市场价无效或超过报价范围。");
    }
    return mfBaseCraftQuote(MaterialCost, StandardPillCount, false);
}

// 用途：优先读取法器实际配料账再确定售值；流程：装备和坊市预览；返回：纯报价，损坏账明确失败。
CraftPriceQuote mfQuoteGearPrice(const Game& GameState, const Gear& GearState) {
    if (GearState.grade < MinimumGearGrade || GearState.grade > MaximumGearGrade)
        return mfPriceFailure("法器品阶必须为一至六阶。");
    if (GearState.ForgeVersion == 2) {
        int MaterialCount = 0;
        std::int64_t MaterialCost = 0;
        for (const auto& [MaterialId, RowCount] : GearState.SourceMaterials) {
            const int UnitPrice = mfNormalMaterialBuyPrice(GameState, MaterialPriceKind::ForgeMaterial, MaterialId);
            if (RowCount < 1 || RowCount > StandardForgeMaterialCount ||
                !mfAddMaterialValue(MaterialCost, UnitPrice, RowCount))
                return mfPriceFailure("法器原始配料记录无效。");
            MaterialCount += RowCount;
            if (MaterialCount > StandardForgeMaterialCount)
                return mfPriceFailure("法器原始配料总数无效。");
        }
        if (MaterialCount != StandardForgeMaterialCount)
            return mfPriceFailure("法器原始配料记录不完整。");
        return mfApplyGearSaleBonuses(GameState, GearState, mfBaseCraftQuote(MaterialCost, 1, false));
    }
    const std::string LedgerPrefix = "forge-materials-v1:" + GearState.id + ":";
    bool HasMaterialLedger = false;
    int MaterialCount = 0;
    std::int64_t MaterialCost = 0;
    for (const auto& [LedgerKey, RowCount] : GameState.flags) {
        if (!LedgerKey.starts_with(LedgerPrefix)) continue;
        HasMaterialLedger = true;
        const std::string MaterialId = LedgerKey.substr(LedgerPrefix.size());
        const int UnitPrice = mfNormalMaterialBuyPrice(GameState, MaterialPriceKind::ForgeMaterial, MaterialId);
        if (RowCount < 1 || RowCount > StandardForgeMaterialCount ||
            !mfAddMaterialValue(MaterialCost, UnitPrice, RowCount))
            return mfPriceFailure("法器原始配料记录无效。");
        MaterialCount += RowCount;
        if (MaterialCount > StandardForgeMaterialCount) return mfPriceFailure("法器原始配料总数无效。");
    }
    if (HasMaterialLedger && MaterialCount != StandardForgeMaterialCount)
        return mfPriceFailure("法器原始配料记录不完整。");
    auto Quote = HasMaterialLedger ? mfBaseCraftQuote(MaterialCost, 1, false) :
                                    mfStandardGearMaterialQuote(GameState, GearState.grade);
    return mfApplyGearSaleBonuses(GameState, GearState, std::move(Quote));
}

// 用途：按三料4/3/3需求预览真实自炼价格；流程：工坊开工前报价；返回：纯报价，重复原料正确合并。
CraftPriceQuote mfQuoteForgePrice(const Game& GameState, const std::string& MainMaterialId,
                                  const std::string& AuxiliaryMaterialId, const std::string& CoreMaterialId) {
    const auto Materials = artForgeMaterials(GameState.player, MainMaterialId, AuxiliaryMaterialId, CoreMaterialId);
    if (Materials.empty()) return mfPriceFailure("请选择有效的三种炼器材料。");
    std::int64_t MaterialCost = 0;
    for (const auto& Material : Materials) {
        const int UnitPrice = mfNormalMaterialBuyPrice(GameState, MaterialPriceKind::ForgeMaterial, Material.id);
        if (!mfAddMaterialValue(MaterialCost, UnitPrice, Material.required))
            return mfPriceFailure("炼器材料市场价无效或超过报价范围。");
    }
    Gear PreviewGear;
    PreviewGear.crafted = true;
    return mfApplyGearSaleBonuses(GameState, PreviewGear, mfBaseCraftQuote(MaterialCost, 1, false));
}

// 用途：为普通铺标准法器给出买价及可实际成交的回收价；流程：目录和购买共用；返回：纯估价快照。
CraftPriceQuote mfQuoteStandardGearPrice(const Game& GameState, int Grade) {
    Gear StandardGear;
    StandardGear.grade = Grade;
    return mfApplyGearSaleBonuses(GameState, StandardGear, mfStandardGearMaterialQuote(GameState, Grade));
}
} // namespace nz::crafting
