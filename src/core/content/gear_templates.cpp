#include "core/content/gear_templates.h"

#include <array>

namespace nz::content {

std::span<const GearTemplateDefinition> mfGearTemplates() {
    static constexpr std::array<GearTemplateDefinition, 6> Templates{{
        {"damage_sword", "破阵灵剑", "sword", "damage"},
        {"draw_sword", "引念灵剑", "sword", "draw"},
        {"redraw_sword", "换念灵剑", "sword", "redraw"},
        {"shield_bell", "护体灵钟", "bell", "shield"},
        {"cleanse_bell", "清浊灵钟", "bell", "cleanse"},
        {"turn_shield_pearl", "绵护灵珠", "pearl", "turn_shield"}
    }};
    return Templates;
}

const GearTemplateDefinition* mfFindGearTemplate(std::string_view TemplateId) {
    for (const auto& Definition : mfGearTemplates())
        if (Definition.Id == TemplateId) return &Definition;
    return nullptr;
}

} // namespace nz::content
