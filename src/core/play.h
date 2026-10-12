// 宁州一梦 · C++ 版 — 玩家可直接操作的行动
//
// 这个模块的存在理由：原来的玩法是「选个计划 → 反复点推进行动」，
// 炼丹要过月、炼器要过月、买东西没接口、NPC 只能看不能理。
// 站在玩家角度这些都是坏体验。
//
// 所以这里补上「立刻就有结果」的一批操作：
//   - 坊市买卖（当场成交，价格随行情浮动）
//   - NPC 往来（切磋 / 请教 / 赠礼 / 结伴，好感度真正影响结果）
//   - 炼丹炼器（当场出结果，消耗材料与天数，不占用月度计划）
//
// 设计原则：每个动作都要「一次点击 -> 一个明确结果」，
// 并且把为什么失败说清楚（缺什么、差多少），不要只说「不可以」。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::play {

// ============================================================
// 坊市
// ============================================================
struct MarketItem {
    std::string kind;        // "potion" | "herb" | "material" | "gear"
    std::string id;
    std::string name;
    std::string desc;
    int price = 0;           // 买价（已含行情）
    int sellPrice = 0;       // 卖价
    int owned = 0;           // 已有数量
    std::string dao;         // 丹药对应大道
};

// 当前所在地能买到的东西（只在城池有效）
// 用途：列出当前城池实际可售的丹药草药器材。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<MarketItem> marketList(const Game& g);

// 在坊市可买到的法器（按境界解锁）
// 用途：列出当前境界可购买的成品法器。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<MarketItem> gearList(const Game& g);

// 买入。count 为数量。返回空字符串表示成功，否则是原因。
// 用途：验证地点品阶库存与灵石后购入指定数量。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& kind, const std::string& id, int count，按本模块入口调用。
// 返回值：成功空串；失败中文原因且不购入资源。
std::string marketBuy(Game& g, const std::string& kind, const std::string& id,
                      int count);

// ============================================================
// NPC 往来
// ============================================================
// 附近可交互的人（同地点、在世）
// 用途：筛选所在地仍在世可交互NPC。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
std::vector<const Npc*> nearbyPeople(const Game& g);

// 找一个人（按 id）
// 用途：按NPC ID查询现有人物。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const Npc* findPerson(const Game& g, const std::string& id);

struct SocialAction {
    std::string id;
    std::string name;
    std::string desc;
    int days = 0;
    int cost = 0;            // 灵石
    bool enabled = false;
    std::string reason;      // 不可用的原因（要具体）
};

// 某个人当前能做的往来
// 用途：预览指定道友当前可做往来及条件。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& npcId，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<SocialAction> socialActions(const Game& g, const std::string& npcId);

struct SocialResult {
    bool ok = false;
    std::string error;
    std::string text;        // 给玩家看的一句话结果
    int relationDelta = 0;
};

// 执行往来
// 用途：执行已验证的请教赠礼交流与切磋。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& npcId, const std::string& actionId，按本模块入口调用。
// 返回值：SocialResult结果，包含说明所述状态或计算数据。
SocialResult socialDo(Game& g, const std::string& npcId,
                      const std::string& actionId);

// 好感度档位名称（用于界面显示）
// 用途：把好感数值转换为中文关系等级。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入int value，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string relationLabel(int value);

// ============================================================
// 炼丹 / 炼器（即时结算，不占月度计划）
// ============================================================
struct CraftOption {
    std::string kind;        // "brew" | "forge"
    std::string id;
    std::string name;
    std::string desc;
    int days = 0;
    int stoneCost = 0;
    bool enabled = false;
    std::string reason;
    double chance = 0;       // 成丹率 / 成功率
    int output = 1;          // 每炉成功成丹数
};

// 可以炼的丹方 / 可以打的器型
// 用途：汇总即时制作候选及既有数值门槛。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const std::string& kind，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<CraftOption> craftOptions(const Game& g, const std::string& kind);

struct CraftResult {
    bool ok = false;
    std::string error;
    std::string text;
    bool success = false;    // 是否成功产出
};

// 用途：执行即时制作并推进相应天数。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const std::string& kind, const std::string& id，按本模块入口调用。
// 返回值：CraftResult结果，包含说明所述状态或计算数据。
CraftResult craftDo(Game& g, const std::string& kind, const std::string& id);

} // namespace nz::play
