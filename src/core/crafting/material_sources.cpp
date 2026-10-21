#include "core/crafting/material_sources.h"
#include "core/content/forge_content.h"
#include "core/source_limits.h"

namespace nz::crafting {
string mfOrdinaryForgeMaterialError(const Player& PlayerState, std::string_view MaterialId) {
    const auto* Definition = content::mfFindForgeMaterial(MaterialId);
    if (!Definition) return "坊市里没有这份炼器材料。";
    if (const string Error = ordinaryMaterialError(PlayerState, Definition->Material.grade); !Error.empty()) return Error;
    if (Definition->Source != content::ForgeMaterialSource::OrdinaryMarket || Definition->AcquisitionPending)
        return "此材料不在普通坊市出售，请查看材料的实际来源。";
    return {};
}
bool mfCanBuyOrdinaryForgeMaterial(const Player& PlayerState, std::string_view MaterialId) {
    return mfOrdinaryForgeMaterialError(PlayerState, MaterialId).empty();
}
}
