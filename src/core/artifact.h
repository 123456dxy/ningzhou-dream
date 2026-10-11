#pragma once
#include "core/battle.h"

namespace nz {
// 自炼法宝的主动能力独立于牌堆，沿用战斗灵力、悟道与冷却结算。
struct ArtifactAbility {
    bool valid = false;
    string gearId, shape, shapeName, desc;
    SpellDef spell;
    Gear gear; // 原法宝：目录和炼器预览按真实装备、铭刻规则计算
};
string artifactShape(const Gear& gear);
ArtifactAbility artifactAbility(const Gear& gear);
vector<ArtifactAbility> equippedArtifactAbilities(const Player& player);
optional<ArtifactAbility> equippedArtifactAbility(const Player& player, const string& id);
} // namespace nz
