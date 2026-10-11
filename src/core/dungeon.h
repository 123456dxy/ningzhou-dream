#pragma once
#include "core/model.h"

namespace nz {

struct DungeonRelicDef {
    string id, name, desc;
    int mana = 0, draw = 0, heal = 0;
    double shield = 0;
};
struct DungeonIntelOption {
    string kind, id, name, desc;
    int grade = 1, stage = 0, cost = 0;
    bool enabled = false;
    string reason;
};

const vector<DungeonRelicDef>& dungeonRelics();
void ensureDungeons(Game& g);
// 旧版缺少永久奖励字段时，在存档状态校验前确定性补齐。
void migrateDungeonRewards(Game& g);
void dungeonYearTick(Game& g);
string dungeonEnterError(const Game& g, const DungeonSite& site);
bool dungeonNodeAvailable(const DungeonRun& run, const string& nodeId);
string dungeonEnter(Game& g, const string& siteId);
string dungeonChoose(Game& g, const string& nodeId);
string dungeonClaim(Game& g, const string& rewardId);
string dungeonLeave(Game& g);
// 身亡或外部中断时丢弃临时物资，不制造额外日历安排。
void dungeonAbandon(Game& g);
vector<DungeonIntelOption> dungeonIntelOptions(const Game& g);
string dungeonIntel(Game& g, const string& kind, const string& id);
void dungeonBattleComplete(Game& g, const Battle& battle, bool won);
string dungeonStateError(const Game& g);

} // namespace nz
