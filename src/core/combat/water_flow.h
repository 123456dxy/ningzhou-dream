#pragma once
#include <array>
#include <string>
#include <string_view>

namespace nz { struct SpellDef; }

namespace nz::WaterFlow {
struct AttackScaling {
    std::string_view SpellId;
    std::string_view Kind;
    double Scale;
};
inline constexpr std::array<AttackScaling, 4> SourceScaling{{
    {"waterSeal", "water", .5}, {"stormSword", "water", .75},
    {"wavehit", "wave", .9}, {"waterfall", "wave", 1.2}
}};
inline constexpr int DrawLimit = 2;
inline constexpr int ReleaseThreshold = 4;

struct AttackQuote {
    int Draws = 0;
    int AfterUsed = 0;
};

// 用途：配置四张原生水攻击；流程：神通目录构建；返回：白名单及类型匹配时更新倍率/CD并返回真。
bool mfConfigureSpell(SpellDef& Spell);
// 用途：报价原生蓄水/浪击补牌；流程：双方真实动作与预览；返回：抽牌数及兑现后本轮预算，不改输入或RNG。
// 层数由调用者现有水势容量校验；负层数或超出0..2的已用预算不补牌，计数保持原值。
AttackQuote mfQuoteAttack(const SpellDef& Spell, int BeforeWater, int AfterWater, int UsedDraws);
// 用途：从同一白名单和预算参数生成原生水攻击说明；返回：无关神通为空。
std::string mfRulesDescription(const SpellDef& Spell);
} // namespace nz::WaterFlow
