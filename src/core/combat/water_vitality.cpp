#include "core/combat/water_vitality.h"
#include "core/model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace nz::WaterVitality {
static_assert(HealRate >= 0 && HealRate <= 1);

bool mfQualifies(const Arts& ActorArts) {
    if (ActorArts.main != "wave") return false;
    const auto Manual = ActorArts.manuals.find("wave");
    return Manual != ActorArts.manuals.end() && Manual->second >= RequiredManualLevel;
}

HitFacts mfQuote(const Arts& ActorArts, const Battle& AfterDirectHit,
        int TargetHealthBefore) {
    HitFacts Facts;
    Facts.Qualified = mfQualifies(ActorArts);
    if (TargetHealthBefore > 0) {
        const auto Loss = static_cast<std::int64_t>(TargetHealthBefore) - AfterDirectHit.enemyHp;
        Facts.DirectHealthLoss = static_cast<int>(std::clamp<std::int64_t>(Loss, 0, TargetHealthBefore));
    }
    if (!Facts.Qualified || AfterDirectHit.hp <= 0 || AfterDirectHit.max <= 0 ||
        AfterDirectHit.hp > AfterDirectHit.max) return Facts;
    Facts.HealingPotential = static_cast<int>(std::floor(Facts.DirectHealthLoss * HealRate));
    Facts.Healing = std::min(Facts.HealingPotential, AfterDirectHit.max - AfterDirectHit.hp);
    return Facts;
}

HitFacts mfApply(const Arts& ActorArts, Battle& AfterDirectHit,
        int TargetHealthBefore) {
    const auto Facts = mfQuote(ActorArts, AfterDirectHit, TargetHealthBefore);
    AfterDirectHit.hp += Facts.Healing;
    return Facts;
}

std::string mfRulesDescription() {
    return "主修浪击诀达到" + std::to_string(RequiredManualLevel) + "层时，存活者按主动直伤造成的实际气血损失回复" +
        std::to_string(static_cast<int>(HealRate * 100)) +
        "%气血（向下取整，不含盾挡和超杀），每次最多回复缺失气血；不受治疗加成影响。"
        "持续伤害与反震不触发本效果；辅修不生效。";
}

std::string mfHitDescription(const HitFacts& Facts) {
    if (!Facts.Qualified || Facts.DirectHealthLoss <= 0) return "";
    return "浪击诀回春：实际损血" + std::to_string(Facts.DirectHealthLoss) +
        "，治疗潜力" + std::to_string(Facts.HealingPotential) + "，回复" + std::to_string(Facts.Healing) + "。";
}
} // namespace nz::WaterVitality
