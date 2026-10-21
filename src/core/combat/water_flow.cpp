#include "core/combat/water_flow.h"
#include "core/battle.h"

namespace nz::WaterFlow {
namespace {
// 用途：同时匹配稳定ID与攻击类型；流程：水流配置/报价；返回：原生白名单行，法器及五行水剑为空。
const AttackScaling* mfFindSource(const SpellDef& Spell) {
    for (const auto& Source : SourceScaling)
        if (Spell.id == Source.SpellId && Spell.kind == Source.Kind) return &Source;
    return nullptr;
}
}

bool mfConfigureSpell(SpellDef& Spell) {
    const auto* Source = mfFindSource(Spell);
    if (!Source) return false;
    Spell.scale = Source->Scale;
    Spell.cooldown = 0;
    return true;
}

AttackQuote mfQuoteAttack(const SpellDef& Spell, int BeforeWater, int AfterWater, int UsedDraws) {
    AttackQuote Quote{0, UsedDraws};
    if (BeforeWater < 0 || AfterWater < 0 || UsedDraws < 0 || UsedDraws >= DrawLimit) return Quote;
    const auto* Source = mfFindSource(Spell);
    if (!Source) return Quote;
    const bool CanDraw = Source->Kind == "water" ? AfterWater > BeforeWater : BeforeWater >= ReleaseThreshold;
    if (CanDraw) {
        Quote.Draws = 1;
        ++Quote.AfterUsed;
    }
    return Quote;
}
std::string mfRulesDescription(const SpellDef& Spell) {
    const auto* Source = mfFindSource(Spell);
    if (!Source) return "";
    const std::string Trigger = Source->Kind == "water" ? "本次实际增加水势时补1张牌。" :
        "释放前水势至少" + std::to_string(ReleaseThreshold) + "层时补1张牌。";
    return Trigger + "原生水攻击共享每自身回合最多补" + std::to_string(DrawLimit) +
        "张，受手牌容量限制；回澜等独立抽牌不占此预算。输出无冷却。";
}
} // namespace nz::WaterFlow
