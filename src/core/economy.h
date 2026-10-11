// 宁州一梦 · C++ 版 — 营生与坊市（对应 JS 版 economy.mjs）
#pragma once

#include "core/model.h"

namespace nz {

// 五座城池
extern const char* const ECO_CITIES[5];

// 每月的资源与任务刷新（对应 ecoRefresh）
void ecoRefresh(Game& g);

// 已得功法抄本同步（对应 ecoSyncBooks）
void ecoSyncBooks(Game& g);

// 药材/法器价格
int ecoSalePrice(const Game& g, const string& type, const string& id);

// 初始化
void ensureEconomy(Game& g);

// 制作天数（炼丹/炼器按品阶与能力折算）
int ecoCraftDays(const Player& p, const string& type, int grade);

// 是否在五城之一
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
EcoActionInfo ecoActionInfo(const Game& g, const string& type,
                            const string& id, int batch = 1);
// 返回空字符串表示可以执行
string ecoActionError(const Game& g, const string& type, const string& id, int batch = 1);

// 开始与完成
void ecoBegin(Game& g, const string& type, const string& id, int batch = 1);
void ecoComplete(Game& g, const Job& job);

// 猎妖结算
void ecoHuntEnd(Game& g, bool win);

// 任务与委托的查询（供界面显示）
const vector<struct TaskDef>* ecoTaskList();

} // namespace nz
