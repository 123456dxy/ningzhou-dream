#pragma once

#include "core/model.h"

#include <array>
#include <optional>
#include <string_view>

namespace nz::crafting {

struct ForgeRequest;

// 无外部资源的纯配方结果；不读取人物、库存、时钟或随机数。
struct ForgeRecipeResult {
    bool Valid = false;
    string Reason;
    std::array<int, 5> Dimensions{};
    int MaterialCount = 0;
    int Grade = 0;
    int DimensionScore = 0;
    int Energy = 0;
    int RequiredEnergy = 0;
    double Utilization = 0;
    bool ContainsSixthGradeMaterial = false;
    Gear Product;
};

[[nodiscard]] ForgeRecipeResult mfEvaluateForgeRecipe(const ForgeRequest& Request);
// 使用白名单的完整十份配方；非法模板、品阶或来源返回空，不产生编号或随机结果。
[[nodiscard]] std::optional<Gear> mfCreateGearFromTemplate(
    std::string_view TemplateId, int Grade, std::string_view Origin = "event");

} // namespace nz::crafting
