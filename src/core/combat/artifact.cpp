#include "core/artifact.h"
#include "core/content/game_ids.h"
#include "core/content/text_table.h"
#include "core/arts.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz {
using content::DaoId;
using content::SpellKind;
using content::TextId;
// 用途：识别自炼法器器型，兼容仅有器名的旧装备。
// 参与流程：法宝主动能力生成前置。
// 使用方式：先检查显式器型，再按旧器名和槽位补齐。
// 返回值：合法器型 ID；显式器型无效时为空。
string artifactShape(const Gear& gear) {
    for (const auto& form : data::ShapeDef_TABLE())
        if (form.id == gear.shape && form.slot == gear.slot) return form.id;
    if (!gear.shape.empty()) return "";
    // 老存档只保存器名和槽位；先识别实际器型，再按槽位补齐。
    for (const auto& form : data::ShapeDef_TABLE())
        if (form.slot == gear.slot && gear.name.size() >= form.name.size() &&
            gear.name.compare(gear.name.size() - form.name.size(), form.name.size(), form.name) == 0)
            return form.id;
    if (gear.slot == "weapon") return "sword";
    if (gear.slot == "armor") return "armor";
    if (gear.slot == "accessory") return "pearl";
    return "";
}

// 用途：把法器器型、品阶、元素和铭刻转为主动神通。
// 参与流程：法宝目录与实战主动施术。
// 使用方式：传单件装备；非自炼或非法装备返回无效能力。
// 返回值：含神通、说明与原法器的能力值。
ArtifactAbility artifactAbility(const Gear& gear) {
    ArtifactAbility ability;
    if (!gear.crafted || gear.id.empty() || gear.grade < 1 || gear.grade > 6) return ability;
    ability.shape = artifactShape(gear);
    if (ability.shape.empty()) return ability;
    for (const auto& form : data::ShapeDef_TABLE()) if (form.id == ability.shape) ability.shapeName = form.name;
    ability.valid = true; ability.gearId = gear.id; ability.gear = gear;
    auto& spell = ability.spell;
    spell.id = "artifact:" + gear.id;
    spell.name = content::formatText(TextId::ArtifactActivate, {{"name", gear.name}});
    // Preserve the historical neutral-element fallback explicitly at this boundary.
    const DaoId daoId = content::parseArtifactElement(gear.element).value_or(DaoId::Qi);
    spell.dao = string(content::legacyDaoKey(daoId));
    spell.stage = gear.grade <= 2 ? 0 : (gear.grade - 2) * 3 - 1;
    spell.cooldown = 1;
    const bool yin = gear.polarity == "地阴";
    const double potency = (1 + (gear.grade - 1) * 0.10 + std::min(0.20, std::max(0.0, gear.bonus))) *
                           (gear.inscribed ? 1.12 : 1.0);
    const auto elemental = [&]() {
        if (daoId == DaoId::Fire) return string(content::legacyValue(SpellKind::Fire));
        if (daoId == DaoId::Wood) return string(content::legacyValue(SpellKind::Poison));
        if (daoId == DaoId::Water) return string(content::legacyValue(SpellKind::Water));
        if (daoId == DaoId::Mind) return string(content::legacyValue(SpellKind::Soul));
        if (daoId == DaoId::Earth) return string(content::legacyValue(SpellKind::Charge));
        if (daoId == DaoId::Sword) return string(content::legacyValue(SpellKind::Sword));
        return string(content::legacyValue(SpellKind::Attack));
    };
    if (ability.shape == "sword") {
        spell.cost = 4; spell.kind = elemental();
        if (yin && daoId == DaoId::Metal) spell.kind = content::legacyValue(SpellKind::Charge);
        if (yin && daoId == DaoId::Qi) spell.kind = content::legacyValue(SpellKind::Steal);
        spell.scale = (yin ? 0.40 : 0.70) * potency;
        spell.statusScale = (yin ? 1.5 : 1.0) * potency;
        ability.desc = yin ? (daoId == DaoId::Qi ? string(content::text(TextId::ArtifactYinQiSword)) :
                       daoId == DaoId::Metal ? string(content::text(TextId::ArtifactYinMetalSword)) : string(content::text(TextId::ArtifactYinSword))) :
                      daoId == DaoId::Metal || daoId == DaoId::Qi ? string(content::text(TextId::ArtifactYangDirectSword)) : string(content::text(TextId::ArtifactYangSword));
    } else if (ability.shape == "seal") {
        spell.cost = 6; spell.cooldown = 2;
        if (yin) {
            spell.kind = elemental(); spell.scale = 0.30 * potency; spell.statusScale = 2.0 * potency;
            if (daoId == DaoId::Metal) spell.kind = content::legacyValue(SpellKind::Charge);
            if (daoId == DaoId::Qi) spell.kind = content::legacyValue(SpellKind::Steal);
            ability.desc = daoId == DaoId::Qi ? string(content::text(TextId::ArtifactYinQiSeal)) :
                     daoId == DaoId::Metal ? string(content::text(TextId::ArtifactYinMetalSeal)) : string(content::text(TextId::ArtifactYinSeal));
        } else {
            spell.scale = 0.90 * potency;
            spell.kind = daoId == DaoId::Fire ? string(content::legacyValue(SpellKind::FireBurst)) : daoId == DaoId::Wood ? string(content::legacyValue(SpellKind::PoisonBurst)) :
                      daoId == DaoId::Water ? string(content::legacyValue(SpellKind::Wave)) : daoId == DaoId::Mind ? string(content::legacyValue(SpellKind::SoulBurst)) :
                      daoId == DaoId::Earth ? string(content::legacyValue(SpellKind::Heavy)) : daoId == DaoId::Sword ? string(content::legacyValue(SpellKind::Sword)) : string(content::legacyValue(SpellKind::Attack));
            ability.desc = string(content::text(TextId::ArtifactYangSeal));
        }
    } else if (ability.shape == "armor") {
        spell.cost = 4; spell.kind = yin ? string(content::legacyValue(SpellKind::Guard)) : string(content::legacyValue(SpellKind::Shield)); spell.scale = 0.13 * potency;
        ability.desc = yin ? string(content::text(TextId::ArtifactYinArmor)) : string(content::text(TextId::ArtifactYangArmor));
    } else if (ability.shape == "bell") {
        spell.cost = 0; spell.cooldown = 2; spell.kind = content::legacyValue(SpellKind::Mana);
        spell.scale = 2 * (1 + (gear.grade >= 3 ? 1 : 0) + (gear.grade >= 5 ? 1 : 0));
        ability.desc = content::formatText(TextId::ArtifactBell, {{"count", std::to_string(static_cast<int>(spell.scale / 2))}});
    } else {
        spell.cost = 2; spell.cooldown = 2;
        if (daoId == DaoId::Wood || daoId == DaoId::Water) {
            spell.kind = content::legacyValue(SpellKind::Heal); spell.scale = 0.08 * potency;
            ability.desc = string(content::text(TextId::ArtifactHeal));
        } else if (yin) {
            spell.kind = content::legacyValue(SpellKind::Ward); spell.scale = 0.04 * potency;
            ability.desc = string(content::text(TextId::ArtifactWard));
        } else {
            spell.kind = content::legacyValue(SpellKind::Empower); spell.scale = 0.06 * potency;
            ability.desc = string(content::text(TextId::ArtifactEmpower));
        }
    }
    ability.desc += content::formatText(TextId::ArtifactSuffix, {{"count", std::to_string(spell.cooldown)}});
    return ability;
}

// 用途：列出已装备且品阶允许的自炼法宝能力。
// 参与流程：战斗侧栏、敌方施术与装备预览。
// 使用方式：传角色，按槽位和法器 ID 对照真实装备。
// 返回值：可用法宝主动能力列表，不改装备。
vector<ArtifactAbility> equippedArtifactAbilities(const Player& player) {
    vector<ArtifactAbility> result;
    for (const auto& [slot, id] : player.arts.equipped)
        for (const auto& gear : player.arts.gear) {
            if (gear.id != id || gear.slot != slot || gear.grade > artTier(player)) continue;
            auto ability = artifactAbility(gear);
            if (ability.valid) result.push_back(std::move(ability));
            break;
        }
    return result;
}
// 用途：按主动神通 ID 查找已装备法宝能力。
// 参与流程：出牌 ID 解析和法宝预览。
// 使用方式：传角色及 artifact: 开头的能力 ID。
// 返回值：匹配能力；无可用装备时为 nullopt。
optional<ArtifactAbility> equippedArtifactAbility(const Player& player, const string& id) {
    for (auto& ability : equippedArtifactAbilities(player)) if (ability.spell.id == id) return ability;
    return std::nullopt;
}
} // namespace nz
