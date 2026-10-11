// 宁州一梦 · C++ 版 — 道友往来（好感度 / 交情 / 送礼 / NPC 背包）
//
// 为什么单独一块：原来的「关系」只有一个 relations 数字，
// 而且每条社交动作都给悟性、都耗时、都不看对方是谁 ——
// 结果就是「无视境界无限刷好感」，元婴修士被你一个炼气期闲聊两句就掏心掏肺。
//
// 现在拆成两条独立的轴：
//
//   好感度 affinity  −100 ~ 100   对方对你的态度。可负，会掉。
//                                决定「能不能做某些事」（拜师/道侣/论道…）。
//   交情   credit     0 ~ ∞       人情债。只增不减（花掉才减）。
//                                用来换对方背包里的东西。
//
// 两条轴独立，所以「他讨厌你，但欠你人情」是合法状态。
//
// 打招呼只有第一次有收益；之后要靠送礼与重交互（切磋/请教/游历/论道）。
// 送礼好感按价值、修行偏好、性格和月内次数计算；交情按价值计算。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::friendship {

// ------------------------------------------------------------
// 交情
// ------------------------------------------------------------
// 取（没有就是 0）
int creditOf(const Game& g, const std::string& npcId);
// 加交情（只增）。delta 为负时是「花掉」，内部会夹到 >= 0
void addCredit(Game& g, const std::string& npcId, int delta);
// 花交情。不够返回 false
bool spendCredit(Game& g, const std::string& npcId, int cost);
// 所有可变往来共用：在世、同地点，且无未完成安排、抉择或斗法。
std::string interactionError(const Game& g, const std::string& npcId);

// 交情档位名称（界面展示）
std::string creditLabel(int credit);

// ------------------------------------------------------------
// 好感度
// ------------------------------------------------------------
// 好感档位名称
std::string affinityLabel(int affinity);
// 好感是否到了某个门槛（用于解锁行动）
bool affinityAtLeast(const Game& g, const std::string& npcId, int need);

// ------------------------------------------------------------
// 打招呼
// ------------------------------------------------------------
struct GreetResult {
    bool ok = false;
    bool firstTime = false;      // 是不是第一次见
    int affinityDelta = 0;       // 好感变化（可能为负）
    int creditDelta = 0;         // 交情变化
    std::string text;
    std::string error;
};
// 打招呼：只有第一次有收益，之后只是寒暄
GreetResult greet(Game& g, const std::string& npcId);

// ------------------------------------------------------------
// 送礼
// ------------------------------------------------------------
// 送礼加多少好感 —— 这是整套系统的核心公式，单独暴露便于调试与展示。
//
// 看三件事：
//   你的声望     名声大的人送东西，对方更当回事
//   礼物用途     按宗门、大道和当前需求偏好丹药、药材、材料或法器
//   对方性格     影响谢意；低境界送礼不会自动被视为侮辱
//   月内次数     超过三次后好感递减，交情保持
struct GiftOutcome {
    int affinity = 0;       // 好感变化
    int credit = 0;         // 交情变化
    std::string reason;     // 为什么加/减（界面要如实说明）
};
// value 是这份礼的坊市价值（按便宜时期的价，即 sourcePrice 口径）
GiftOutcome giftOutcome(const Game& g, const Npc& npc, int value,
                        const std::string& kind = "", const std::string& id = "");
// 交情按坊市价折算：价值越高，人情越大（带递减，避免一件神器买断）
int creditFromValue(int value);

// 执行送礼（从玩家背包扣一件，加好感与交情）
struct GiftResult {
    bool ok = false;
    int affinityDelta = 0;
    int creditDelta = 0;
    std::string text;
    std::string error;
};
// kind: "potion" | "herb" | "material" | "gear"
GiftResult giftItem(Game& g, const std::string& npcId,
                    const std::string& kind, const std::string& id);
// 旧在途赠药安排：按真实分类库存送出指定份数，作为一次赠礼结算。
GiftResult giftHerbs(Game& g, const std::string& npcId, int quantity);

// ------------------------------------------------------------
// NPC 背包
// ------------------------------------------------------------
struct BagItem {
    std::string kind;       // potion / herb / material / gear
    std::string id;
    std::string name;
    std::string desc;
    int count = 1;
    int value = 0;          // 坊市价（便宜时期口径）
    int creditCost = 0;     // 换它要花多少交情
    bool affordable = false;
    std::string reason;     // 换不了的原因
};
// 列出某个 NPC 背包里能换的东西
std::vector<BagItem> npcBag(const Game& g, const std::string& npcId);
// 花交情换一件
struct TradeResult {
    bool ok = false;
    int cost = 0;
    std::string text;
    std::string error;
};
TradeResult takeFromNpc(Game& g, const std::string& npcId,
                        const std::string& kind, const std::string& id);

// NPC 自己会不会用玩家送的东西（比自身装备好就换上）
// 始终接收一份法器；返回被替换的旧装备名（空槽或未更换时为空）。
std::string npcTryEquip(Game& g, Npc& npc, const Gear& gear);

// ------------------------------------------------------------
// 初始化
// ------------------------------------------------------------
// 给新生成的 NPC 备一份背包（按其境界给对应及更低境界的物品）
void ensureNpcBag(Game& g, Npc& npc);
// 给 NPC 定流派与神通（人类修士走「神通+平A」，妖兽走「力大砖飞」）
void ensureNpcCultivation(Game& g, Npc& npc);

// 读旧档时补上 NPC 的背包/流派（旧档没有这些字段）
void ensureAllNpcData(Game& g);

} // namespace nz::friendship
