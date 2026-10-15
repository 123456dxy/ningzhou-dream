#pragma once
#include "core/model.h"

namespace nz {
enum class DungeonEventKind { Spring, Altar, Reflection, Tablet, Remains, Caravan };
struct DungeonEventInfo {
    DungeonEventKind Kind = DungeonEventKind::Spring;
    string Name, Hint;
};
struct DungeonEventQuote {
    bool Enabled = false;
    string Reason;
    int HealthCost = 0, StoneCost = 0;
};
// 用途：确定事件主题；流程：路线预告和事件生成；用法：传探索与节点；返回：稳定名称/风险提示，不推进RNG。
DungeonEventInfo mfDescribeDungeonEvent(const DungeonRun& Run, const DungeonNode& Node);
// 用途：冻结本节点事件选项；流程：进入新版奇遇；用法：传只读游戏/探索/节点；返回：可持久化选择，不发奖励。
vector<DungeonReward> mfDungeonEventRewards(const Game& State, const DungeonRun& Run, const DungeonNode& Node);
// 用途：核对当前事件选择代价；流程：展示和执行共享；用法：传当前奖励ID；返回：真实资格与费用，无副作用。
DungeonEventQuote mfQuoteDungeonEvent(const Game& State, const string& RewardId);
// 用途：校验事件冻结选项；流程：读档领域校验；用法：传当前游戏；返回：非法选项的原因，合法为空。
string mfDungeonEventStateError(const Game& State);
// 用途：兑现一项事件选择；流程：dungeonClaim；用法：传冻结奖励ID；返回：错误或空，失败不扣费、不改RNG。
string mfClaimDungeonEvent(Game& State, const string& RewardId);
}
