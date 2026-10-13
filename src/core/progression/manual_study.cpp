#include "core/progression/manual_study.h"

#include <algorithm>
#include <cmath>

#include "core/arts.h"
#include "core/cultivate.h"

namespace nz {

ManualStudyInfo manualStudyInfo(const Player& player, const string& manualId) {
    ManualInfo manual;
    if (!findManualInfo(manualId, manual)) return {};
    const auto learned = player.arts.manuals.find(manualId);
    const int level = learned == player.arts.manuals.end() ? 0 : std::clamp(learned->second, 0, 5);
    ManualStudyInfo result;
    result.valid = true;
    result.maxed = level >= 5;
    result.tier = std::clamp(manual.stage / 3 + 1, 1, 5);
    result.targetLevel = std::min(5, level + 1);
    if (!result.maxed) {
        result.days = cultivate::learnDays(player, level, result.tier);
        result.stones = static_cast<int>(std::ceil(24.0 * std::pow(1.2, level) *
                                                  std::pow(2.0, result.tier - 1) - 1e-9));
    }
    return result;
}

} // namespace nz
