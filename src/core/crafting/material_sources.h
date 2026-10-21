#pragma once
#include "core/model.h"
#include <string_view>

namespace nz::crafting {
// 用途：普通坊市材料渠道与境界的唯一资格；纯查询，无RNG/时间/存档副作用。
[[nodiscard]] bool mfCanBuyOrdinaryForgeMaterial(const Player& PlayerState, std::string_view MaterialId);
[[nodiscard]] string mfOrdinaryForgeMaterialError(const Player& PlayerState, std::string_view MaterialId);
}
