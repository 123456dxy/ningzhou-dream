#include "core/crafting/craft_timing.h"

#include "core/content/game_ids.h"
#include "core/talents.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace nz::crafting {
namespace {
inline constexpr std::array BrewingBaseDurationDays{4, 5, 6, 7, 8, 9};
inline constexpr std::array ForgingBaseDurationDays{30, 60, 120, 240, 480};
inline constexpr double MaximumAbilityReduction = 0.30;
inline constexpr double AbilityReductionPerRootPoint = 0.016;
inline constexpr double MaximumLifeReduction = 0.60;
inline constexpr int MinimumCraftDurationDays = 1;
}

// 用途：把兼容协议分类转为封闭枚举；流程：旧接口适配；用法：传动作键；返回：分类或空，不改状态。
std::optional<CraftKind> mfParseCraftKind(std::string_view WireKind) {
    if (WireKind == "brew") return CraftKind::Brewing;
    if (WireKind == "forge") return CraftKind::Forging;
    return std::nullopt;
}

// 用途：按目录品阶查询基础时长；流程：所有制作报价；用法：传已解析分类和一基品阶；返回：合法天数或0，无越界访问。
int mfBaseCraftDurationDays(CraftKind Kind, int Grade) {
    if (Grade < 1) return 0;
    switch (Kind) {
    case CraftKind::Brewing:
        if (Grade > static_cast<int>(BrewingBaseDurationDays.size())) return 0;
        return BrewingBaseDurationDays[static_cast<std::size_t>(Grade - 1)];
    case CraftKind::Forging:
        if (Grade > static_cast<int>(ForgingBaseDurationDays.size())) return 0;
        return ForgingBaseDurationDays[static_cast<std::size_t>(Grade - 1)];
    }
    return 0;
}

// 用途：统一结算单炉或单件制作天数；流程：工坊预览、开工与委托；用法：传角色快照及品阶；返回：最终整数天数，失败分类/品阶为0。
int mfCraftDurationDays(const Player& PlayerState, CraftKind Kind, int Grade) {
    const int BaseDurationDays = mfBaseCraftDurationDays(Kind, Grade);
    if (BaseDurationDays == 0) return 0;

    const auto ArtifactExperience = PlayerState.arts.exp.find(
        std::string(content::legacyDaoKey(content::DaoId::Artifact)));
    const double AbilityPoints = Kind == CraftKind::Brewing
        ? static_cast<double>(std::max(0, PlayerState.alchemy))
        : (ArtifactExperience == PlayerState.arts.exp.end()
            ? 0.0 : std::max(0, ArtifactExperience->second) / 10.0);
    const double AbilityReduction = std::min(
        MaximumAbilityReduction, std::sqrt(AbilityPoints) * AbilityReductionPerRootPoint);
    const double LifeReduction = Kind == CraftKind::Forging
        ? std::clamp(talentMods(PlayerState).forgeSpeed, 0.0, MaximumLifeReduction) : 0.0;
    return std::max(MinimumCraftDurationDays, static_cast<int>(std::ceil(
        BaseDurationDays * (1.0 - AbilityReduction) * (1.0 - LifeReduction))));
}

} // namespace nz::crafting
