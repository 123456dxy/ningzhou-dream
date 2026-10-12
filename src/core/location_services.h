#pragma once
#include "core/model.h"
namespace nz {
struct LocationService {
    string id, name, desc, reason, targetTab;
    int days = 0, cost = 0, stage = 0;
    bool enabled = false;
};
// 用途：列出指定地点的服务和真实资格。
// 参与流程：地图及地点详情展示。
// 使用方式：传游戏与地点 ID，只读查询费用、耗时和阻塞。
// 返回值：服务列表；地点无效时为空。
vector<LocationService> locationServices(const Game& g, const string& location);
// 用途：执行服务导航或收费建立地点工作。
// 参与流程：地点服务动作入口。
// 使用方式：传游戏、服务 ID 和可写 targetTab，按当前位置服务校验。
// 返回值：成功为空；填写导航页或更新灵石、月标记和 Job。
string locationServiceBegin(Game& g, const string& id, string& targetTab);
// 用途：兑现地点感悟或真实采集奖励。
// 参与流程：地点 Job 完成回调。
// 使用方式：传游戏与 Job，按 origin/kind 使用当地与世界库存规则。
// 返回值：无；更新成长、行囊、世界资源、RNG 和完成日志。
void locationServiceComplete(Game& g, const Job& job);
}
