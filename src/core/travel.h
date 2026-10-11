#pragma once

#include "core/model.h"

namespace nz {

struct TravelOption {
    string id;
    string name;
    int days = 0;
    int cost = 0;
    bool enabled = false;
    string reason;
    string desc;
    double speed = 0;         // 每天地图行进距离
    double ambushChance = 0;  // 每个途中日的截杀概率
};

struct TravelPlan {
    string from;
    string to;
    int distance = 0;
    int escapeSpeed = 0;
    vector<TravelOption> options;
};

bool travelModeKnown(const string& mode);
string travelModeName(const string& mode);
TravelPlan travelPlan(const Game& g, const string& to);
const TravelOption* travelOption(const TravelPlan& plan, const string& mode);
// 成功时返回空字符串，只建立任务和支付费用，不推进日历。
string travelBegin(Game& g, const string& to, const string& mode = "boat");
// 实际日历推进后检查一次；遭遇截杀时保留行程并暂停为斗法。
bool travelAmbushTick(Game& g);

} // namespace nz
