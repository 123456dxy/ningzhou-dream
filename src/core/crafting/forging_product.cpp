#include "core/crafting/forging_product.h"

#include "core/content/forge_content.h"
#include "core/content/gear_templates.h"
#include "core/crafting/forge_rules.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nz::crafting {
namespace {
constexpr int ForgePortionCount = 10;
constexpr int MaximumDimension = 500;
constexpr int FullUtilizationScore = 2200;

ForgeRecipeResult mfInvalidRecipe(ForgeRecipeResult Result, const string& Reason) {
    Result.Valid = false;
    Result.Reason = Reason;
    return Result;
}
}

ForgeRecipeResult mfEvaluateForgeRecipe(const ForgeRequest& Request) {
    ForgeRecipeResult Result;
    if (Request.MaterialIds.size() > ForgePortionCount)
        return mfInvalidRecipe(std::move(Result), "炼器最多投入十份材料。");
    const auto* Shape = content::mfFindForgeShape(Request.ShapeId);
    if (!Shape) return mfInvalidRecipe(std::move(Result), "请选择有效器型。");
    if (!content::mfFindForgeInscription(Request.InscriptionId))
        return mfInvalidRecipe(std::move(Result), "没有这种灵纹。");

    int RawEnergy = 0;
    int MinimumGrade = 6;
    map<string, int> MaterialCounts;
    for (const auto& MaterialId : Request.MaterialIds) {
        if (MaterialId.empty()) continue;
        const auto* Material = content::mfFindForgeMaterial(MaterialId);
        if (!Material) return mfInvalidRecipe(std::move(Result), "未知炼器材料：" + MaterialId + "。");
        ++Result.MaterialCount;
        ++MaterialCounts[MaterialId];
        MinimumGrade = std::min(MinimumGrade, Material->Material.grade);
        Result.ContainsSixthGradeMaterial |= Material->Material.grade == 6;
        RawEnergy += Material->Energy;
        for (std::size_t DimensionIndex = 0; DimensionIndex < Result.Dimensions.size(); ++DimensionIndex)
            Result.Dimensions[DimensionIndex] += Material->Dimensions[DimensionIndex];
    }
    for (auto& Dimension : Result.Dimensions) Dimension = std::min(MaximumDimension, Dimension);
    Result.DimensionScore = std::accumulate(Result.Dimensions.begin(), Result.Dimensions.end(), 0);
    Result.Utilization = std::min(1.0, static_cast<double>(Result.DimensionScore) / FullUtilizationScore);
    Result.Energy = static_cast<int>(std::floor(RawEnergy * Result.Utilization));
    Result.Valid = true;
    Result.Product.ForgeVersion = 2;
    Result.Product.Origin = "player";
    Result.Product.shape = Shape->Id;
    Result.Product.slot = Shape->Slot;
    Result.Product.SourceMaterials = MaterialCounts;
    Result.Product.dimensions.assign(Result.Dimensions.begin(), Result.Dimensions.end());
    if (Result.MaterialCount == 0) {
        Result.Reason = "请投入十份炼器材料。";
        return Result;
    }
    Result.Grade = std::min(5, MinimumGrade);
    Result.RequiredEnergy = (7 + (Request.InscriptionId.empty() ? 0 : 3)) * content::mfForgeMaterialEnergy(Result.Grade);
    Result.Product.grade = Result.Grade;
    Result.Product.bonus = 0;
    Result.Product.EffectPower = (1 + 0.2 * (Result.Grade - 1)) * (0.5 + 0.5 * Result.Utilization);
    Result.Product.ForgeEnergy = Result.Energy;
    Result.Product.crafted = true;
    Result.Product.InscriptionId = Request.InscriptionId;
    if (Request.MainMaterialId.empty()) {
        Result.Reason = "请从已投入材料中指定引灵主材。";
        return Result;
    }
    if (!MaterialCounts.contains(Request.MainMaterialId))
        return mfInvalidRecipe(std::move(Result), "引灵主材必须是已投入的材料。");
    const auto* MainMaterial = content::mfFindForgeMaterial(Request.MainMaterialId);
    Result.Product.element = MainMaterial->Material.element;
    Result.Product.polarity = MainMaterial->Material.polarity;
    if (Request.EffectId.empty()) {
        Result.Reason = "请选择主效果。";
        return Result;
    }
    const auto* Effect = content::mfFindForgeEffect(Request.EffectId);
    if (!Effect || !content::mfForgeEffectAllowed(Request.EffectId, Shape->Slot, MainMaterial->Material.element))
        return mfInvalidRecipe(std::move(Result), "该主效果与器型或主材属性不相容。");
    if (Effect->Passive && !Request.InscriptionId.empty())
        return mfInvalidRecipe(std::move(Result), "辅助被动首版不附加主动代价灵纹，请选择无灵纹。");
    Result.Product.EffectId = Effect->Id;
    Result.Product.name = MainMaterial->Material.name + Shape->Name + "·" + Effect->Name;
    if (Result.MaterialCount != ForgePortionCount)
        Result.Reason = "还需投入" + std::to_string(ForgePortionCount - Result.MaterialCount) + "份材料。";
    else if (Result.Energy < Result.RequiredEnergy)
        Result.Reason = "器能不足：当前" + std::to_string(Result.Energy) + "，主效果与灵纹需要" +
            std::to_string(Result.RequiredEnergy) + "。请调整材类配比、补入较高阶材料或去掉灵纹。";
    return Result;
}

std::optional<Gear> mfCreateGearFromTemplate(std::string_view TemplateId, int Grade, std::string_view Origin) {
    const auto* Definition = content::mfFindGearTemplate(TemplateId);
    if (!Definition || Grade < 1 || Grade > 5 || (Origin != "event" && Origin != "talent" && Origin != "player"))
        return std::nullopt;
    ForgeRequest Request;
    const string MaterialPrefix = "fm-" + std::to_string(Grade) + "-mixed-yang-";
    Request.MaterialIds.assign(6, MaterialPrefix + "metal");
    Request.MaterialIds.insert(Request.MaterialIds.end(), 4, MaterialPrefix + "stone");
    Request.MainMaterialId = MaterialPrefix + "metal";
    Request.ShapeId = string(Definition->ShapeId);
    Request.EffectId = string(Definition->EffectId);
    const auto Recipe = mfEvaluateForgeRecipe(Request);
    if (!Recipe.Valid || !Recipe.Reason.empty()) return std::nullopt;
    Gear Product = Recipe.Product;
    Product.name = string(Definition->Name);
    Product.Origin = string(Origin);
    Product.craftCost = 15 + 18 * Grade;
    return Product;
}

} // namespace nz::crafting
