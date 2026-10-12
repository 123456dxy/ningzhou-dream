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

// 用途：检查交通方式协议值。
// 参与流程：赶路动作前置。
// 使用方式：传 walk/boat/flight 值。
// 返回值：支持的方式为真，其他值为假。
bool travelModeKnown(const string& mode);
// 用途：生成交通方式中文名称。
// 参与流程：赶路选项与工作名称。
// 使用方式：传交通方式 ID。
// 返回值：对应名称；未知方式为赶路。
string travelModeName(const string& mode);
// 用途：计算路线距离、费用、时间和途中风险。
// 参与流程：地图交通报价与实际赶路开始。
// 使用方式：传只读游戏与目的地，计入遁速、海路和活动阻塞。
// 返回值：所有交通方式的 TravelPlan，不扣费或推进时间。
TravelPlan travelPlan(const Game& g, const string& to);
// 用途：从路线计划按方式查一个报价。
// 参与流程：赶路执行与途中风险检查。
// 使用方式：传 TravelPlan 和方式 ID。
// 返回值：对应选项常量指针；找不到时为空。
const TravelOption* travelOption(const TravelPlan& plan, const string& mode);
// 成功时返回空字符串，只建立任务和支付费用，不推进日历。
// 用途：支付报价费用并建立可恢复行程。
// 参与流程：地图赶路动作。
// 使用方式：传游戏、目的地和方式，内部重新校验真实计划。
// 返回值：成功为空并修改灵石、Job 和日志；失败为原因。
string travelBegin(Game& g, const string& to, const string& mode = "boat");
// 实际日历推进后检查一次；遭遇截杀时保留行程并暂停为斗法。
// 用途：检查刚推进的途中天数是否触发截杀。
// 参与流程：日历完成一个赶路日之后。
// 使用方式：传游戏，记录检查进度，每段行程最多触发一次。
// 返回值：开战为真；可能修改检查数、RNG、战斗，原行程保留。
bool travelAmbushTick(Game& g);

} // namespace nz
