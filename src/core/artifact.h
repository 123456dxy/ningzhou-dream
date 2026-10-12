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
// 用途：识别自炼法器器型，兼容仅有器名的旧装备。
// 参与流程：法宝主动能力生成前置。
// 使用方式：先检查显式器型，再按旧器名和槽位补齐。
// 返回值：合法器型 ID；显式器型无效时为空。
string artifactShape(const Gear& gear);
// 用途：把法器器型、品阶、元素和铭刻转为主动神通。
// 参与流程：法宝目录与实战主动施术。
// 使用方式：传单件装备；非自炼或非法装备返回无效能力。
// 返回值：含神通、说明与原法器的能力值。
ArtifactAbility artifactAbility(const Gear& gear);
// 用途：列出已装备且品阶允许的自炼法宝能力。
// 参与流程：战斗侧栏、敌方施术与装备预览。
// 使用方式：传角色，按槽位和法器 ID 对照真实装备。
// 返回值：可用法宝主动能力列表，不改装备。
vector<ArtifactAbility> equippedArtifactAbilities(const Player& player);
// 用途：按主动神通 ID 查找已装备法宝能力。
// 参与流程：出牌 ID 解析和法宝预览。
// 使用方式：传角色及 artifact: 开头的能力 ID。
// 返回值：匹配能力；无可用装备时为 nullopt。
optional<ArtifactAbility> equippedArtifactAbility(const Player& player, const string& id);
} // namespace nz
