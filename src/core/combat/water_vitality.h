#pragma once
#include <string>

namespace nz { struct Arts; struct Battle; }

namespace nz::WaterVitality {
inline constexpr int RequiredManualLevel = 5;
inline constexpr double HealRate = 0.5;

struct HitFacts {
    bool Qualified = false;
    int DirectHealthLoss = 0;
    int HealingPotential = 0;
    int Healing = 0;
};

// 用途：识别浪击诀主修满层；流程：双方主动直伤/预览；返回：主修wave且修习至少五层，辅修不生效。
[[nodiscard]] bool mfQualifies(const Arts& ActorArts);
// 用途：报价原命中后的实际损血回春；流程：普通神通、V2法器及预览共用；返回：不写状态的治疗事实。
// AfterDirectHit已结算主段及其反震/固有吸血；TargetHealthBefore只取该主段命中前HP，过量击杀不计入吸血。
// 仅存活者按实际HP损失向下取整回复50%，每次最多回复缺失气血，不修改目标HP或护盾。
[[nodiscard]] HitFacts mfQuote(const Arts& ActorArts, const Battle& AfterDirectHit,
    int TargetHealthBefore);
// 用途：按同一报价兑现回春；流程：真实动作或隔离副本；返回：与报价相同的治疗事实。
// 只改施术者HP；调用者只在主动直伤主段后调用一次，持续伤害与反震不调用。
[[nodiscard]] HitFacts mfApply(const Arts& ActorArts, Battle& AfterDirectHit,
    int TargetHealthBefore);
// 用途：从资格与倍率常量生成满层功法说明；流程：功法目录/预览；返回：无状态依赖的规则文案。
[[nodiscard]] std::string mfRulesDescription();
// 用途：统一普通神通与V2法器的回春战报；流程：真实命中/预览；返回：无资格或无实际损血时为空串。
[[nodiscard]] std::string mfHitDescription(const HitFacts& Facts);
} // namespace nz::WaterVitality
