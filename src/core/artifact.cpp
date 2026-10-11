#include "core/artifact.h"
#include "core/arts.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz {
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

ArtifactAbility artifactAbility(const Gear& gear) {
    ArtifactAbility a;
    if (!gear.crafted || gear.id.empty() || gear.grade < 1 || gear.grade > 6) return a;
    a.shape = artifactShape(gear);
    if (a.shape.empty()) return a;
    for (const auto& form : data::ShapeDef_TABLE()) if (form.id == a.shape) a.shapeName = form.name;
    a.valid = true; a.gearId = gear.id;
    auto& sp = a.spell;
    sp.id = "artifact:" + gear.id;
    sp.name = gear.name + "·催动";
    sp.dao = gear.element == "神念" ? "神" : gear.element == "混元" ? "气" : gear.element;
    if (daoIndex(sp.dao) < 0) sp.dao = "气";
    sp.stage = gear.grade <= 2 ? 0 : (gear.grade - 2) * 3 - 1;
    sp.cooldown = 1;
    const bool yin = gear.polarity == "地阴";
    const double potency = (1 + (gear.grade - 1) * 0.10 + std::min(0.20, std::max(0.0, gear.bonus))) *
                           (gear.inscribed ? 1.12 : 1.0);
    const auto elemental = [&]() {
        if (sp.dao == "火") return string("fire");
        if (sp.dao == "木") return string("poison");
        if (sp.dao == "水") return string("water");
        if (sp.dao == "神") return string("soul");
        if (sp.dao == "土") return string("charge");
        if (sp.dao == "剑") return string("sword");
        return string("attack");
    };
    if (a.shape == "sword") {
        sp.cost = 4; sp.kind = elemental();
        if (yin && sp.dao == "金") sp.kind = "charge";
        if (yin && sp.dao == "气") sp.kind = "steal";
        sp.scale = (yin ? 0.40 : 0.70) * potency;
        sp.statusScale = (yin ? 1.5 : 1.0) * potency;
        a.desc = yin ? (sp.dao == "气" ? "地阴混元剑以较低直伤夺灵，命中后恢复灵力。" :
                       sp.dao == "金" ? "地阴金剑以较低直伤积攒蓄势，可配合重击爆发。" : "地阴剑侧重叠加属性状态，直伤较低。") :
                      sp.dao == "金" || sp.dao == "气" ? "天阳剑释放直接属性伤害，可配合相生与增伤。" : "天阳剑造成属性伤害，并触发对应大道的状态。";
    } else if (a.shape == "seal") {
        sp.cost = 6; sp.cooldown = 2;
        if (yin) {
            sp.kind = elemental(); sp.scale = 0.30 * potency; sp.statusScale = 2.0 * potency;
            if (sp.dao == "金") sp.kind = "charge";
            if (sp.dao == "气") sp.kind = "steal";
            a.desc = sp.dao == "气" ? "地阴混元印以较低直伤夺灵，命中后恢复灵力。" :
                     sp.dao == "金" ? "地阴金印以较低直伤集中积攒蓄势。" : "地阴印以较低直伤集中施加属性状态。";
        } else {
            sp.scale = 0.90 * potency;
            sp.kind = sp.dao == "火" ? "fireBurst" : sp.dao == "木" ? "poisonBurst" :
                      sp.dao == "水" ? "wave" : sp.dao == "神" ? "soulBurst" :
                      sp.dao == "土" ? "heavy" : sp.dao == "剑" ? "sword" : "attack";
            a.desc = "天阳印释放属性爆发；火、木、水、神系会消耗累积状态，土系消耗蓄势。";
        }
    } else if (a.shape == "armor") {
        sp.cost = 4; sp.kind = yin ? "guard" : "shield"; sp.scale = 0.13 * potency;
        a.desc = yin ? "获得本回合护盾与守势减伤。" : "获得本回合护盾，可配合蓄势、反震与盾击。";
    } else if (a.shape == "bell") {
        sp.cost = 0; sp.cooldown = 2; sp.kind = "mana";
        sp.scale = 2 * (1 + (gear.grade >= 3 ? 1 : 0) + (gear.grade >= 5 ? 1 : 0));
        a.desc = "吸纳灵气，恢复" + std::to_string(static_cast<int>(sp.scale / 2)) + "灵力（不超过战斗上限）。";
    } else {
        sp.cost = 2; sp.cooldown = 2;
        if (sp.dao == "木" || sp.dao == "水") {
            sp.kind = "heal"; sp.scale = 0.08 * potency;
            a.desc = "恢复气血，可触发治疗与清毒类功法。";
        } else if (yin) {
            sp.kind = "ward"; sp.scale = 0.04 * potency;
            a.desc = "提高本场战斗减伤，法宝与神通的额外减伤共用20%上限。";
        } else {
            sp.kind = "empower"; sp.scale = 0.06 * potency;
            a.desc = "提高本场伤害，法宝与神通的伤害加成共用30%上限。";
        }
    }
    a.desc += " 品阶与铭刻强化威力；冷却" + std::to_string(sp.cooldown) +
              "个回合。装备后常驻斗法侧栏，无需抽牌、不占额外神通槽。";
    return a;
}

vector<ArtifactAbility> equippedArtifactAbilities(const Player& p) {
    vector<ArtifactAbility> result;
    for (const auto& [slot, id] : p.arts.equipped)
        for (const auto& gear : p.arts.gear) {
            if (gear.id != id || gear.slot != slot || gear.grade > artTier(p)) continue;
            auto a = artifactAbility(gear);
            if (a.valid) result.push_back(std::move(a));
            break;
        }
    return result;
}
optional<ArtifactAbility> equippedArtifactAbility(const Player& p, const string& id) {
    for (auto& a : equippedArtifactAbilities(p)) if (a.spell.id == id) return a;
    return std::nullopt;
}
} // namespace nz
