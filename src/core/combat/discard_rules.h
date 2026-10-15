#pragma once

namespace nz::DiscardRules {
// 主动换手支付了费用和卡位，保留完整凝盾；自动余牌只兑现一半，锐势不受影响。
enum class Timing { Active, RoundEnd };
inline constexpr int RoundEndShieldPercent = 50;
}
