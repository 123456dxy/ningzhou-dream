#pragma once

#include <algorithm>
#include <cstddef>

namespace nz::BattleCards {

// 双方抽牌、换手、只读预演和存档校验共用的容量；不改变起手五张与神通携带槽位。
inline constexpr int MaximumHandSize = 12;

// 用途：报价补至指定手牌数的实际抽牌量；流程：双方施放与只读预览共用。
// 使用：传本牌离手后的手牌、待抽数、目标与抽弃牌堆总数；返回：不超过目标及可抽牌数的增量。
inline int mfDrawToCount(std::size_t HandCount, int PendingDraws, int TargetCount, std::size_t SourceCount) {
    const int Held = static_cast<int>(std::min(HandCount, static_cast<std::size_t>(MaximumHandSize)));
    const int Pending = std::clamp(PendingDraws, 0, MaximumHandSize);
    const int Target = std::clamp(TargetCount, 0, MaximumHandSize);
    const int Available = static_cast<int>(std::min(SourceCount, static_cast<std::size_t>(MaximumHandSize)));
    return std::max(0, std::min(Target - Held - Pending, Available - Pending));
}

} // namespace nz::BattleCards
