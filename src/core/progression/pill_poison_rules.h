#pragma once

#include <optional>
#include <string_view>

namespace nz {
// 用途：按稳定用途键查询统一丹毒；流程：玩家/NPC服丹及目录预览；用法：传丹方kind，不传品阶或境界；返回：0/1/2/4点，未知用途返回空值供动作拒绝，无副作用。
std::optional<int> mfPillPoisonPoints(std::string_view EffectKind);
} // namespace nz
