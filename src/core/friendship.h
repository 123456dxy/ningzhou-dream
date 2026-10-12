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
// 用途：读取玩家对目标的交情余额。
// 参与流程：NPC 背包报价与社交展示。
// 使用方式：传游戏与 NPC ID，不建立新账。
// 返回值：已有余额；没有往来时为零。
int creditOf(const Game& g, const std::string& npcId);
// 加交情（只增）。delta 为负时是「花掉」，内部会夹到 >= 0
// 用途：调整并限制交情余额。
// 参与流程：赠礼增加交情及其他往来记账。
// 使用方式：传游戏、NPC ID 和可正可负的变化量。
// 返回值：无；修改 npcBonds 对应余额，限制在零至十亿。
void addCredit(Game& g, const std::string& npcId, int delta);
// 花交情。不够返回 false
// 用途：检查并扣除足够的交情。
// 参与流程：向 NPC 讨取物品。
// 使用方式：传游戏、NPC ID 和非负成本。
// 返回值：足够时扣余额、增加累计花费并返回真，否则假。
bool spendCredit(Game& g, const std::string& npcId, int cost);
// 所有可变往来共用：在世、同地点，且无未完成安排、抉择或斗法。
// 用途：校验角色、目标、同地与活动阻塞。
// 参与流程：可修改 NPC 往来的统一前置。
// 使用方式：传只读游戏与 NPC ID。
// 返回值：拒绝原因；当前可往来时为空。
std::string interactionError(const Game& g, const std::string& npcId);

// 交情档位名称（界面展示）
// 用途：按余额给交情分档。
// 参与流程：NPC 往来展示。
// 使用方式：传交情整数，不修改余额。
// 返回值：中文交情档位名称。
std::string creditLabel(int credit);

// ------------------------------------------------------------
// 好感度
// ------------------------------------------------------------
// 好感档位名称
// 用途：按关系值描述目标态度。
// 参与流程：NPC 详情与往来展示。
// 使用方式：传 -100..100 口径的好感值。
// 返回值：从仇雠到生死之交的中文标签。
std::string affinityLabel(int affinity);
// 好感是否到了某个门槛（用于解锁行动）
// 用途：判断目标好感是否达到行动门槛。
// 参与流程：拜师、道侣等往来条件。
// 使用方式：传游戏、NPC ID 和要求值。
// 返回值：已有好感不低于要求时为真。
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
// 用途：见礼并只发放首次好感收益。
// 参与流程：当面打招呼动作。
// 使用方式：传目标 ID，先校验真实可往来状态。
// 返回值：结果和文案；首次修改见面账、关系并记日志。
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
// 用途：计算一次赠礼的好感、交情和原因。
// 参与流程：赠礼预览与实际结算。
// 使用方式：传游戏、NPC、价值及礼物种类/ID。
// 返回值：按偏好、性格、声望与月内递减计算的收益。
GiftOutcome giftOutcome(const Game& g, const Npc& npc, int value,
                        const std::string& kind = "", const std::string& id = "");
// 交情按坊市价折算：价值越高，人情越大（带递减，避免一件神器买断）
// 用途：将礼物价值按开方折算为交情。
// 参与流程：赠礼与背包报价。
// 使用方式：传基础价值，非正值仍返回最低一点。
// 返回值：至少一点的交情收益。
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
// 用途：把一件真实物品送给同地修士。
// 参与流程：即时赠礼动作。
// 使用方式：传 NPC ID、potion/herb/material/gear 和物品 ID。
// 返回值：赠礼结果；成功转移库存、更新好感交情及赠礼账。
GiftResult giftItem(Game& g, const std::string& npcId,
                    const std::string& kind, const std::string& id);
// 旧在途赠药安排：按真实分类库存送出指定份数，作为一次赠礼结算。
// 用途：结算旧在途安排的一次多份赠药。
// 参与流程：赠药工作完成后的兼容入口。
// 使用方式：传游戏、NPC ID 和真实分类药材份数。
// 返回值：赠礼结果；成功转移药材并更新关系、交情与次数。
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
// 用途：列出目标行囊物品和交情兑换条件。
// 参与流程：NPC 背包页面及兑换报价。
// 使用方式：传游戏与 NPC ID；空行囊会惰性初始化，即使参数为 const。
// 返回值：按成本排序的报价；惰性补全时会修改 NPC 和 RNG。
std::vector<BagItem> npcBag(const Game& g, const std::string& npcId);
// 花交情换一件
struct TradeResult {
    bool ok = false;
    int cost = 0;
    std::string text;
    std::string error;
};
// 用途：花交情从目标真实行囊取一件物品。
// 参与流程：NPC 物品兑换动作。
// 使用方式：传目标、物品种类与 ID，装备中的法器不能讨取。
// 返回值：兑换结果；成功扣交情并转移物品到玩家。
TradeResult takeFromNpc(Game& g, const std::string& npcId,
                        const std::string& kind, const std::string& id);

// NPC 自己会不会用玩家送的东西（比自身装备好就换上）
// 始终接收一份法器；返回被替换的旧装备名（空槽或未更换时为空）。
// 用途：接收法器并在更优或空槽时换装。
// 参与流程：玩家赠送法器后的 NPC 装备决策。
// 使用方式：传游戏、NPC 与法器，先复制输入避免容器失效。
// 返回值：被替换旧器名；无换装或原空槽为空，修改 NPC 装备。
std::string npcTryEquip(Game& g, Npc& npc, const Gear& gear);

// ------------------------------------------------------------
// 初始化
// ------------------------------------------------------------
// 给新生成的 NPC 备一份背包（按其境界给对应及更低境界的物品）
// 用途：为无背包 NPC 补消耗品和概率初始法器。
// 参与流程：新 NPC 和旧档数据补齐。
// 使用方式：已有 bag 时直接返回；法器仅在 gear 为空时补。
// 返回值：无；修改 NPC 背包、装备和 RNG。
void ensureNpcBag(Game& g, Npc& npc);
// 给 NPC 定流派与神通（人类修士走「神通+平A」，妖兽走「力大砖飞」）
// 用途：补齐 NPC 大道与神通。
// 参与流程：新 NPC、旧档的构筑初始化。
// 使用方式：传游戏与 NPC，已有道和神通时保留原配置。
// 返回值：无；修改 NPC 流派/神通，需随机选择时推进 RNG。
void ensureNpcCultivation(Game& g, Npc& npc);

// 读旧档时补上 NPC 的背包/流派（旧档没有这些字段）
// 用途：逐人补齐构筑和存活 NPC 的空行囊。
// 参与流程：旧档 NPC 数据升级。
// 使用方式：传游戏，保留已有构筑与非空背包。
// 返回值：无；更新 NPC 集合，必要时推进 RNG。
void ensureAllNpcData(Game& g);

} // namespace nz::friendship
