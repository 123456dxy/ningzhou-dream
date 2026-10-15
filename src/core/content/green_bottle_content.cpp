#include "core/content/green_bottle_content.h"
#include <array>

namespace nz::content {
// 用途：定义仅专属秘境稳定产出的三种炼器灵材；流程：目录只读；返回：不可变定义，参考价沿同阶既有材料基值。
std::span<const GreenBottleMaterialDefinition> mfGreenBottleMaterials() {
    static const std::array<GreenBottleMaterialDefinition, 3> Materials{{
        {{"bottle-spirit-4", "青玄灵髓", 4, "地阴", "木", 128, "440,500,780,0,720"}, 3, 6, "绿瓶·青玄灵境"},
        {{"bottle-spirit-5", "太乙青晶", 5, "天阳", "木", 200, "780,440,660,840,0"}, 4, 9, "绿瓶·太乙灵境"},
        {{"bottle-spirit-6", "造化碧玉", 6, "天阳", "木", 288, "900,600,820,960,0"}, 5, 12, "绿瓶·造化灵境"}
    }};
    return Materials;
}
// 用途：按稳定ID找专属材料；流程：配料/奖励校验；返回：借用静态指针或空。
const GreenBottleMaterialDefinition* mfFindGreenBottleMaterial(std::string_view MaterialId) {
    for (const auto& Definition : mfGreenBottleMaterials()) if (Definition.Material.id == MaterialId) return &Definition;
    return nullptr;
}
// 用途：按目标阶位找秘境材料；流程：升级/入口报价；返回：借用静态指针或空。
const GreenBottleMaterialDefinition* mfGreenBottleMaterialForGrade(int Grade) {
    for (const auto& Definition : mfGreenBottleMaterials()) if (Definition.Material.grade == Grade) return &Definition;
    return nullptr;
}
}
