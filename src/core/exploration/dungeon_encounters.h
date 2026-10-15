#pragma once

#include "core/battle.h"
#include <string>

namespace nz {

// 所属探索模块；表示秘境节点的六类妖兽与宗门修士，不以显示名称参与规则分支。
enum class DungeonEncounterKind {
    StoneGuard, FlameBeast, VenomBeast, MindBeast, RendingBeast, ChargingBeast, SectDisciple
};

// 所属探索模块；无外部资源、可复制的只读遭遇报价。
// Enemy为预告与实际开战共用的规格；Enabled=false时调用方必须展示Reason并拒绝开战。
// 不借用实际NPC身份，不修改其背包或服丹；难度来自持久入口，不取当前人物临时牌组境界。
struct DungeonEncounter {
    bool Enabled = false;
    DungeonEncounterKind Kind = DungeonEncounterKind::StoneGuard;
    std::string Id, Name, Hint, Reason;
    EnemySpec Enemy;
};

// 用途：以入口与节点稳定选择秘境对手并给出真实机制预告。
// 参与流程：路线展示、节点确认与实际battleStart；用法：传活动游戏、run及其中节点。
// 返回值：可直接传给battleStart的Enemy及中文提示；入口缺失/非法/非战斗节点返回禁用原因。
// 只读取状态，不推进世界时钟、RNG、服丹或存档；普通战比冻结入口低一个小境界，精英/首领同入口。
[[nodiscard]] DungeonEncounter mfDescribeDungeonEncounter(
    const Game& State, const DungeonRun& Run, const DungeonNode& Node);

} // namespace nz
