#include "core/source_limits.h"

#include <algorithm>

#include "core/arts.h"

namespace nz {

// 用途：计算普通坊市出售的药材最高品阶。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ordinaryHerbGradeLimit(const Player& player) {
    return std::min(2, artTier(player));
}

// 用途：计算普通坊市出售的器材最高品阶。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& player，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ordinaryMaterialGradeLimit(const Player& player) {
    return std::min(2, 1 + player.stage / 3);
}

// 用途：说明药材受品阶与境界限制的购买原因。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& player, int grade，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ordinaryHerbError(const Player& player, int grade) {
    if (grade < 1) return "药材品阶无效。";
    if (grade > 2) return "三品及以上药材不在普通坊市出售，可向天机阁购买定向材料秘境情报，通关保底获得十份。";
    if (grade > ordinaryHerbGradeLimit(player)) return "境界不足以购入此品阶药材。";
    return "";
}

// 用途：说明器材受品阶与境界限制的购买原因。
// 参与流程：工坊、坊市、藏经阁和洞府操作。
// 使用方式：传入const Player& player, int grade，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ordinaryMaterialError(const Player& player, int grade) {
    if (grade < 1) return "炼器材料品阶无效。";
    if (grade > 2) return "三阶及以上炼器材料不在普通坊市出售，可向天机阁购买定向材料秘境情报，通关保底获得十份。";
    if (grade > ordinaryMaterialGradeLimit(player)) return "二阶炼器材料需达到筑基期。";
    return "";
}

} // namespace nz
