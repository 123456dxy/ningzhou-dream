#pragma once
#include "data/reference_tables.h"
#include <span>
#include <string_view>

namespace nz::content {
// 专属灵材静态白名单；真实炼器维度/参考价格与物品ID归此目录，升级不可使用普通材料替代。
struct GreenBottleMaterialDefinition {
    data::MaterialDef Material;
    int MinimumBottleGrade = 0;
    std::string DungeonName;
};
// 用途：读取三种真实专属灵材；流程：升瓶/秘境/炼器/背包；返回：不可变静态目录，无交易或随机副作用。
std::span<const GreenBottleMaterialDefinition> mfGreenBottleMaterials();
// 用途：按稳定ID查专属灵材；流程：目标校验/配料；返回：静态定义或空，不将普通材料当作专属材料。
const GreenBottleMaterialDefinition* mfFindGreenBottleMaterial(std::string_view MaterialId);
// 用途：按目标品阶查专属灵材；流程：4/5/6阶秘境及升级；返回：静态定义或空。
const GreenBottleMaterialDefinition* mfGreenBottleMaterialForGrade(int Grade);
}
