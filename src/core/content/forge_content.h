#pragma once

#include "data/reference_tables.h"

#include <array>
#include <span>
#include <string_view>

namespace nz::content {

enum class ForgeMaterialSource { OrdinaryMarket, TianjiSteward, BeastDrop, GreenBottle, Pending };

// 制作内容目录的不可变值；不拥有游戏状态，不提供掉落或购买动作。
struct ForgeMaterialDefinition {
    data::MaterialDef Material;
    string FamilyId;
    string FamilyName;
    string PartId;
    string PartName;
    std::array<int, 5> Dimensions{};
    int Energy = 0;
    bool AcquisitionPending = false;
    string SourceNote;
    ForgeMaterialSource Source = ForgeMaterialSource::Pending;
};

struct ForgeShapeDefinition {
    string Id;
    string Name;
    string Slot;
};

struct ForgeEffectDefinition {
    string Id;
    string Name;
    string Slot;
    string Description;
    vector<string> Elements;
    bool Passive = false;
};

struct ForgeInscriptionDefinition {
    string Id;
    string Name;
    string Description;
};

// 用途：读取自由投料所用的稳定材料白名单，保留旧材料；流程：目录、报价和存档校验；纯查询，不开放新来源。
[[nodiscard]] std::span<const ForgeMaterialDefinition> mfForgeMaterials();
[[nodiscard]] const ForgeMaterialDefinition* mfFindForgeMaterial(std::string_view MaterialId);
[[nodiscard]] int mfForgeMaterialEnergy(int Grade);
[[nodiscard]] std::span<const ForgeShapeDefinition> mfForgeShapes();
[[nodiscard]] const ForgeShapeDefinition* mfFindForgeShape(std::string_view ShapeId);
[[nodiscard]] std::span<const ForgeEffectDefinition> mfForgeEffects();
[[nodiscard]] const ForgeEffectDefinition* mfFindForgeEffect(std::string_view EffectId);
[[nodiscard]] bool mfForgeEffectAllowed(std::string_view EffectId, std::string_view Slot, std::string_view Element);
[[nodiscard]] std::span<const ForgeInscriptionDefinition> mfForgeInscriptions();
[[nodiscard]] const ForgeInscriptionDefinition* mfFindForgeInscription(std::string_view InscriptionId);

} // namespace nz::content
