#pragma once

#include <span>
#include <string_view>

namespace nz::content {

struct GearTemplateDefinition {
    std::string_view Id;
    std::string_view Name;
    std::string_view ShapeId;
    std::string_view EffectId;
};

[[nodiscard]] std::span<const GearTemplateDefinition> mfGearTemplates();
[[nodiscard]] const GearTemplateDefinition* mfFindGearTemplate(std::string_view TemplateId);

} // namespace nz::content
