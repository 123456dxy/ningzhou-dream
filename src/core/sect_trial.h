#pragma once

#include "core/model.h"

namespace nz {

struct SectTrialDef {
    string sect, name, desc, style, manual, spell;
    int minStage = 0, fee = 10, days = 3;
};

const vector<SectTrialDef>& sectTrialDefs();
const SectTrialDef* findSectTrial(const string& sect);
// 返回空字符串表示可在本宗山门报名；失败请求完全不改变游戏。
string sectTrialError(const Game& g, const string& sect);
string sectTrialBegin(Game& g, const string& sect);
// 必须使用 battleFinish 保留的完整快照，重复结算不发奖励。
void sectTrialBattleComplete(Game& g, const Battle& battle, bool won, bool escaped = false);
// 旧档 join 工作完成后转为实战考核，不能自动入宗或传送。
void sectTrialLegacyComplete(Game& g, const Job& job);
string sectTrialStateError(const Game& g);

} // namespace nz
