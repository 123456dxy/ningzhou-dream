#pragma once

#include "core/model.h"

#include <optional>
#include <string_view>

namespace nz::crafting {

// 所属制作模块；封闭动作分类，无外部资源、可复制。
// 只表达耗时，不负责原料、费用、资格、随机数或工作持久化。
enum class CraftKind { Brewing, Forging };

// 用途：解析旧动作协议键；流程：ecoCraftDays 兼容门面；用法：传 brew/forge；返回：分类或 nullopt，未知键不回退炼器；纯查询。
[[nodiscard]] std::optional<CraftKind> mfParseCraftKind(std::string_view WireKind);

// 用途：提供唯一逐阶基础天数；流程：工坊报价及实际开工；用法：炼丹品阶1~6、炼器1~5；返回：每炉/件天数，分类或品阶非法为0；纯查询。
[[nodiscard]] int mfBaseCraftDurationDays(CraftKind Kind, int Grade);

// 用途：在基础天数上保留丹术、器道与生活减时；流程：预览、自炼、代炼代铸；用法：传只读角色与合法分类/品阶；返回：向上取整且至少1天，非法为0。
// 无状态写入、RNG、IO；仅调用期间借用角色，独立状态可并行查询。开工由 economy 将结果冻结进 Job，不在读档时重算。
[[nodiscard]] int mfCraftDurationDays(const Player& PlayerState, CraftKind Kind, int Grade);

} // namespace nz::crafting
