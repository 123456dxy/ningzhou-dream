#pragma once

#include "core/model.h"

namespace nz {

struct ManualStudyInfo {
    bool valid = false;
    bool maxed = false;
    int tier = 1;
    int targetLevel = 1;
    int days = 0;
    int stones = 0;
};

// 以功法要求的大境界定阶：每阶 ×2、每层 ×1.2；抄本售价另计。
// 只读报价供目录、开工扣费及兼容年度结算共同使用。
ManualStudyInfo manualStudyInfo(const Player& player, const string& manualId);

} // namespace nz
