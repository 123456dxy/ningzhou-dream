// 宁州一梦 · C++ 版 — 营生与坊市（对应 JS 版 economy.mjs）
#pragma once

#include "core/model.h"

namespace nz {

// 五座城池
extern const char* const ECO_CITIES[5];

// 每月的资源与任务刷新（对应 ecoRefresh）
// 用途：刷新当月城市需求与任务上限。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoRefresh(Game& g);

// 已得功法抄本同步（对应 ecoSyncBooks）
// 用途：把已得功法抄本同步到学习书单。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoSyncBooks(Game& g);

// 取得的是可修习的功法抄本，不等于已学会；同步旧版两套书单。
// 返回是否新增抄本；未知传承不会写入书单，重复持有不重复发放。
// 用途：校验功法ID并仅发放一次抄本。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& id, const string& source，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool ecoAcquireBook(Game& g, const string& id, const string& source);

// 药材/法器价格
// 用途：计算既有行情下药材或法器售价。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& type, const string& id，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ecoSalePrice(const Game& g, const string& type, const string& id);

// 初始化
// 用途：补齐营生库存与委托初始化状态。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ensureEconomy(Game& g);

// 制作天数（炼丹/炼器按品阶与能力折算）
// 用途：计算既有能力影响后的炼制天数。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Player& p, const string& type, int grade，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int ecoCraftDays(const Player& p, const string& type, int grade);

// 是否在五城之一
// 用途：判断地点是否为五座城池。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const string& location，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool ecoIsCity(const string& location);

// ---- 行动查询（对应 ecoActionInfo / ecoActionError）----
// 行动描述：供界面与结算使用
struct EcoActionInfo {
    bool valid = false;
    string name;
    int days = 0;
    int cost = 0;
    double chance = 1.0;
    int batch = 1;
    int output = 1;
    int fee = 0;
    // 炼器时记录器型与配料
    string mainId, auxId, coreId, shapeId;
    string orderId;    // 委托的丹方 id
    string mode;       // 委托类型 brew / forge
};
// 用途：计算营生行动的时间材料成本与成功率。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& type, const string& id, int batch = 1，按本模块入口调用。
// 返回值：EcoActionInfo结果，包含说明所述状态或计算数据。
EcoActionInfo ecoActionInfo(const Game& g, const string& type,
                            const string& id, int batch = 1);
// 返回空字符串表示可以执行
// 用途：检查营生行动资源地点和状态门槛。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& type, const string& id, int batch = 1，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string ecoActionError(const Game& g, const string& type, const string& id, int batch = 1);

// 开始与完成
// 用途：扣除已验证成本并建立进行中的工作。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& type, const string& id, int batch = 1，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoBegin(Game& g, const string& type, const string& id, int batch = 1);
// 用途：结算完成的制作学习任务与采集工作。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const Job& job，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoComplete(Game& g, const Job& job);

// 猎妖结算
// 用途：结算猎妖胜负和对应悬赏。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, bool win，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void ecoHuntEnd(Game& g, bool win);

// 任务与委托的查询（供界面显示）
// 用途：公开城市任务目录。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：匹配定义或实体指针；未找到时为空。
const vector<struct TaskDef>* ecoTaskList();

} // namespace nz
