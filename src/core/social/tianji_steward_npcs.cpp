#include "core/social/tianji_steward.h"
#include <array>

namespace nz::social {
std::span<const TianjiStewardDefinition> mfTianjiStewards() {
    static const std::array<TianjiStewardDefinition, 5> Stewards{{
        {"tianji-steward-wuling", "沈知微", "wuling", 3},
        {"tianji-steward-yifeng", "陆明远", "yifeng", 4},
        {"tianji-steward-yunxi", "江听澜", "yunxi", 6},
        {"tianji-steward-tianxing", "顾星衡", "tianxing", 7},
        {"tianji-steward-guangling", "温照海", "guangling", 9}
    }};
    return Stewards;
}
const TianjiStewardDefinition* mfFindTianjiSteward(std::string_view NpcId) {
    for (const auto& Steward : mfTianjiStewards()) if (Steward.Id == NpcId) return &Steward;
    return nullptr;
}
}
