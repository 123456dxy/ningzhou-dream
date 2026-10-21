#include "core/content/artifact_appearances.h"

#include <array>

namespace nz::content {
std::span<const ArtifactAppearanceDefinition> mfArtifactAppearances() {
    static constexpr std::array<ArtifactAppearanceDefinition, 11> Appearances{{
        {"artifact-sword-v1", "sword", "/assets/artifacts/sword-v1.png"},
        {"artifact-needle-v1", "needle", "/assets/artifacts/needle-v1.png"},
        {"artifact-seal-v1", "seal", "/assets/artifacts/seal-v1.png"},
        {"artifact-ruler-v1", "ruler", "/assets/artifacts/ruler-v1.png"},
        {"artifact-box-v1", "box", "/assets/artifacts/box-v1.png"},
        {"artifact-armor-v1", "armor", "/assets/artifacts/armor-v1.png"},
        {"artifact-robe-v1", "robe", "/assets/artifacts/robe-v1.png"},
        {"artifact-bell-v1", "bell", "/assets/artifacts/bell-v1.png"},
        {"artifact-pearl-v1", "pearl", "/assets/artifacts/pearl-v1.png"},
        {"artifact-ring-v1", "ring", "/assets/artifacts/ring-v1.png"},
        {"artifact-token-v1", "token", "/assets/artifacts/token-v1.png"}
    }};
    return Appearances;
}
const ArtifactAppearanceDefinition* mfFindArtifactAppearance(std::string_view ShapeId) {
    for (const auto& Appearance : mfArtifactAppearances()) if (Appearance.ShapeId == ShapeId) return &Appearance;
    return nullptr;
}
} // namespace nz::content
