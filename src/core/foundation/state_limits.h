#pragma once

namespace nz::StateLimits {
// 用途：统一一般持久数值的绝对上限；参与存档校验、交易余额及纪事编号预检。
// 使用：RNG/seed有独立无符号32位规则，其余相关整数计算在此范围内预演后再提交。
inline constexpr int MaxPersistedInteger = 1000000000;
}
