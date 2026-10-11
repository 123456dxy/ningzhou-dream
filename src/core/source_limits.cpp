#include "core/source_limits.h"

#include <algorithm>

#include "core/arts.h"

namespace nz {

int ordinaryHerbGradeLimit(const Player& player) {
    return std::min(2, artTier(player));
}

int ordinaryMaterialGradeLimit(const Player& player) {
    return std::min(2, 1 + player.stage / 3);
}

string ordinaryHerbError(const Player& player, int grade) {
    if (grade < 1) return "药材品阶无效。";
    if (grade > 2) return "三品及以上药材不在普通坊市出售，可向天机阁购买定向材料秘境情报，通关保底获得十份。";
    if (grade > ordinaryHerbGradeLimit(player)) return "境界不足以购入此品阶药材。";
    return "";
}

string ordinaryMaterialError(const Player& player, int grade) {
    if (grade < 1) return "炼器材料品阶无效。";
    if (grade > 2) return "三阶及以上炼器材料不在普通坊市出售，可向天机阁购买定向材料秘境情报，通关保底获得十份。";
    if (grade > ordinaryMaterialGradeLimit(player)) return "二阶炼器材料需达到筑基期。";
    return "";
}

} // namespace nz
