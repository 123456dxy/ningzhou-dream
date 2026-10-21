#pragma once

#include <span>
#include <string_view>

namespace nz::content {
struct ArtifactAppearanceDefinition {
    std::string_view Id;
    std::string_view ShapeId;
    std::string_view Path;
};
[[nodiscard]] std::span<const ArtifactAppearanceDefinition> mfArtifactAppearances();
[[nodiscard]] const ArtifactAppearanceDefinition* mfFindArtifactAppearance(std::string_view ShapeId);
} // namespace nz::content
