#include "core/progression/pill_poison_rules.h"

#include <array>

namespace nz {
namespace {
// progression拥有的不可变用途配置；无外部资源、可复制，不依赖角色或存档，允许并行只读。
struct PillPoisonRule {
    std::string_view EffectKind;
    int PoisonPoints;
};

constexpr std::array<PillPoisonRule, 14> PillPoisonRules{{
    {"heal", 1}, {"xp", 1},
    {"dao", 2}, {"points", 2}, {"apt", 2}, {"insight", 2},
    {"body", 2}, {"heart", 2}, {"mind", 2}, {"speed", 2},
    {"combat", 4}, {"breakthrough", 4}, {"life", 4},
    {"detox", 0}
}};
} // namespace

// 用途：保持同用途各品阶丹药的丹毒尺度一致；流程：统一服丹计划；用法：仅传稳定用途键；返回：配置点数，未登记用途返回空，不使用原始参考表丹毒。
std::optional<int> mfPillPoisonPoints(std::string_view EffectKind) {
    for (const auto& Rule : PillPoisonRules)
        if (Rule.EffectKind == EffectKind) return Rule.PoisonPoints;
    return std::nullopt;
}
} // namespace nz
