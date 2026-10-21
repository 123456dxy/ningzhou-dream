#pragma once

#include "core/model.h"

#include <array>
#include <optional>

namespace nz::exploration {

// 探索模块的不可变掉落报价；只拥有材料ID与显示属性，无外部资源。
// 五个ID分别对应skin/bone/claw/eye/heart，同一妖兽的属性、品阶、阴阳一致。
struct BeastMaterialLoot {
    std::array<string, 5> MaterialIds;
    int Grade = 1;
    string Element;
    string Polarity;
};

// 用途：按已完成妖兽战斗的实际境界/大道报价五部位；仅允许真实击杀场景的胜利快照。
// 纯查询；不推进活动RNG、日历、存档或库存。阴阳从现有随机快照和战斗上下文稳定派生。
// 未获胜、撤离、修士、切磋/留影、非法境界或无材料属性返回nullopt，不降级为其他属性。
[[nodiscard]] std::optional<BeastMaterialLoot> mfQuoteBeastMaterialLoot(const Game& State, const Battle& Context);

// 用途：在真实动作边界选择八种已有材料属性之一；返回旧大道键并推进一次活动RNG。
// 仅供新建猎妖/兽潮对手调用；查询与读档不得调用。
[[nodiscard]] string mfRollBeastMaterialElement(Game& State);

// 用途：战斗所属模块的唯一收尾边界入袋；非秘境进永久背包，秘境进已核对上下文的探索行囊。
// 五项在候选map计算后一次交换；任一项达容量上限时整组不部分发放，返回0，否则返回5。
// 本函数不保存领取标记，调用方必须由battleFinish的battle清除或秘境phase转换保证仅调用一次。
// 不推进RNG/日历；仅串行领域结算调用，不接收HTTP请求，不写文件。
[[nodiscard]] int mfAwardBeastMaterialLoot(Game& State, const Battle& Context);

} // namespace nz::exploration
