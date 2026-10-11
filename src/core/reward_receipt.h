#pragma once

#include "core/model.h"

namespace nz {

// 在奖励发生前记录可见资源，结算后按真实变化生成文案。
// 包含天赋倍率、属性上限、重复传承和悟道升级的实际结果。
struct RewardReceipt {
    explicit RewardReceipt(const Game& game);
    string changes(const Game& game) const;

private:
    Player player;
    int contribution = 0;
    vector<string> books;
};

} // namespace nz
