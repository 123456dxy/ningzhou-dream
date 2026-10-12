// 宁州一梦 · C++ 版 — 神通卡牌斗法（对应 Demo 07 的 battle.mjs）
//
// 战斗循环：
//   每回合抽 5 张 -> 看敌方意图 -> 用灵力连续出牌（不触发反击）
//   -> 点「结束回合」：弃手牌 → 毒/灼烧结算 → 敌方按意图行动
//      → 护盾清零 → 冷却递减 → 回满灵力 → 再抽 5 张
// 最多 24 回合。
#pragma once

#include "core/model.h"

namespace nz {

// 各境界战力基数（对应 BATTLE_POWER）
extern const int BATTLE_POWER[13];

// 神通定义（战斗视图）
struct SpellDef {
    string id;
    string name;
    string dao;
    int cost = 0;
    string kind = "attack";
    double scale = 0.5;
    int stage = 0;
    int cooldown = 0;
    double statusScale = 1.0; // 法宝属性状态倍率；普通神通保留原始施加量
};

// 用途：汇总基础及扩展神通定义。
// 参与流程：斗法牌组和目录查询的数据入口。
// 使用方式：读取一次初始化的静态列表。
// 返回值：神通定义常量引用。
const vector<SpellDef>& allSpells();
// 查神通；'defend' 返回内置的守势卡
// 用途：按 ID 查神通，守势使用内置定义。
// 参与流程：出牌解析、构筑与界面展示。
// 使用方式：传普通神通 ID，法宝由 battleResolveSpell 解析。
// 返回值：匹配定义指针；未知 ID 返回空。
const SpellDef* battleSpell(const string& id);
// 用途：按真实神通类型拼接规则说明。
// 参与流程：神通目录与手牌说明。
// 使用方式：传定义，参数代入集中内容文案。
// 返回值：含类型效果、限制和冷却的说明。
string battleSpellDescription(const SpellDef& spell);
// 用途：识别需要指定移除目标的神通。
// 参与流程：忘机咒出牌校验及界面选牌。
// 使用方式：传神通定义，检查其种类是否为 Exhaust。
// 返回值：需目标时为真。
bool battleNeedsTarget(const SpellDef& spell);

// 旧接口保留；永久剑意不再参与战斗伤害。
// 用途：保留旧永久剑意加成入口并关闭其收益。
// 参与流程：旧接口兼容，战斗公式不再读取永久剑意。
// 使用方式：传角色只读状态。
// 返回值：恒为零，表示没有永久剑意额外加成。
double battleSwordBonus(const Player& p);
// 用途：提供普通攻击和守势的固定副本数。
// 参与流程：玩家和影身牌组构建。
// 使用方式：无参数，每种基础牌使用同一数量。
// 返回值：每种基础牌五张。
int battleBasicCopies();
// 用途：获取装备、功法与天赋汇总后的遁速。
// 参与流程：先手与撤离判定。
// 使用方式：传角色，委托统一遁速规则。
// 返回值：实际战斗遁速整数。
int battleSpeed(const Player& p);
// 用途：按双方遁速、战力和临时准备计算撤离概率。
// 参与流程：撤离操作与战况预览。
// 使用方式：传当前角色和战斗状态。
// 返回值：非生死对局为一；生死对局限制在 0.10..0.95。
double battleEscapeChance(const Player& p, const Battle& b);
// 额外神通槽位；普通攻击、防御固定携带，不占用槽位。
// 用途：按大境界计算额外神通携带槽数。
// 参与流程：配装与牌组合法性过滤。
// 使用方式：传角色，固定基础攻防不计入此数。
// 返回值：4 至 12 个额外槽位。
int battleSpellSlots(const Player& p);
// 每大境界自然回复 +1，保留功法与悟道提供的额外回复。
// 用途：计算当前角色每轮的基础灵力总量。
// 参与流程：开局及参考战斗初始化。
// 使用方式：传角色，计入大境界、容量收益和气道节点。
// 返回值：受真实容量限制的基础回灵总量。
int battleEnergy(const Player& p);
// 用途：汇总斗法灵力容量。
// 参与流程：即时回灵和回合恢复的容量限制。
// 使用方式：传角色，容量至少十二并叠加有效天赋收益。
// 返回值：本场可持有的最大灵力。
int battleManaCap(const Player& p);
// 用途：预测下一轮会恢复到的灵力总量。
// 参与流程：结束回合赋值及 nextMana 界面预览。
// 使用方式：传回合结束前状态，计入锁定、封存、静息和容量。
// 返回值：下一轮最终灵力值；不是在当前灵力上追加的增量。
int battleNextMana(const Player& p, const Battle& b);
// 用途：汇总被动、场内与守势的来伤减免。
// 参与流程：真实受伤与敌方伤害预览。
// 使用方式：传角色和对应视角的战斗状态。
// 返回值：夹在 -10% 至 35% 的总减伤比例。
double battleReduction(const Player& p, const Battle& b);
// 神通实际费用：守势与灵击固定 1，其余按旧费用折半向上取整
// 用途：计算含悟道与抽牌减费的实际施术费用。
// 参与流程：双方出牌校验与预览。
// 使用方式：传角色和神通，普通攻防固定一费。
// 返回值：不低于零的实际灵力消耗。
int battleCost(const Player& p, const SpellDef& x);

// 敌方本回合意图
//
// 人类修士通过独立牌堆、费用、冷却和功法触发决定连招。
// 妖兽及旧档的首轮行为保留 attack / heavy / guard 等意图。
struct Intent {
    string kind;    // attack / guard / pierce / heavy / heal / drain / poison / weaken
    string name;
    double scale = 0;
    string spell;   // 用的是哪门神通（妖兽为空，表示平A）
};
// 用途：按敌方类型与回合选择简化行动。
// 参与流程：妖兽与旧档首轮意图规划。
// 使用方式：传当前战斗，使用已有风格和冷却信息。
// 返回值：本轮意图定义。
Intent battleIntent(const Battle& b);

// 旧存档意图兼容；新的修士斗法直接执行完整神通效果。
// 用途：把旧存档神通映射为简化敌方意图。
// 参与流程：旧敌方行动兼容。
// 使用方式：传定义；新修士战斗执行完整卡牌效果。
// 返回值：简化种类、名称、倍率和神通 ID。
Intent spellToIntent(const SpellDef& sp);

// 开始斗法（含初始化牌组）
struct EnemySpec {
    string name;
    string id;
    string quest;
    string kind = "spar";
    bool lethal = false;
    int stage = 0;
    int health = 100;
    string style;
    string sect;
    int sword = -1;      // 兼容旧敌人剑意字段；不影响攻防，<0 时保留原推算
    int speed = -1;      // <0 时按实际 NPC 构筑或境界计算
    string gender;
    bool beast = false;
    // 奖励必须在先手行动前登记；对手可能在首轮被护盾反震击败。
    optional<AdvContext> adventure;
    string adventureChoice;
    int huntFee = 0;
    string wildTask;
    int wildFee = 0;
    string dungeonSite;
    string dungeonNode;
};
// 用途：登记敌人、奖励上下文并开始普通斗法。
// 参与流程：游历、悬赏、切磋及野外冲突入口。
// 使用方式：传游戏与完整敌人规格，先手可在入口内结算。
// 返回值：无；写入玩家战斗、消费准备，更新 RNG 与日志。
void battleStart(Game& g, const EnemySpec& enemy);
// Tianji fights own an isolated encounter and immutable opponent loadout.
// Completed encounters retain Battle/logs but battleActive becomes false.
// 用途：由双方不可变快照创建独立影身对局。
// 参与流程：天机影身遭遇初始化。
// 使用方式：传独立游戏、双方快照和种子，不触碰原账号状态。
// 返回值：无；初始化 encounter 的角色、敌方牌组和战报。
void battleStartShadow(Game& encounter, const Player& challengerSnapshot,
                       const Player& opponentSnapshot, uint32_t seed);
// 用途：读取保留战报中的影身结局。
// 参与流程：影身对局结束状态查询。
// 使用方式：传独立遭遇游戏。
// 返回值：结局字符串引用；没有战斗时为空引用。
const string& battleShadowOutcome(const Game& encounter);

// 行动：'end' 结束回合；'escape' 撤离；其余为手牌 uid 或神通 id
struct TurnResult {
    bool ok = true;
    string error;
};
// 用途：校验并执行出牌、结束回合或撤离。
// 参与流程：斗法动作公共入口。
// 使用方式：传游戏、手牌 UID/神通 ID/end/escape 与可选移除目标。
// 返回值：操作成功或拒绝原因；成功更新战斗并可能触发结算。
TurnResult battleTurn(Game& g, const string& id, const string& target = "");

// 抽牌（内部使用，也供测试）
// 用途：为玩家抽牌并在必要时重洗弃牌。
// 参与流程：开局、回合开始与抽牌神通。
// 使用方式：传游戏与数量；过滤已移除牌，手牌最多八张。
// 返回值：无；修改玩家战斗牌堆、手牌、洗牌次数与 RNG。
void battleDraw(Game& g, int n);

// 出牌预览（不改动真实状态）
struct Preview {
    bool valid = false;
    string spellName;
    int cost = 0;
    int damage = 0;
    int shield = 0;
    int heal = 0;
    int energy = 0;
    int qi = 0;
    int healPotential = 0;  // 不受当前缺血限制的单次治疗量
    int manaGain = 0;       // 扣费后恢复的灵力；energy 是最终净变化
    int draw = 0, drawNext = 0;
    int hpMaxChange = 0, manaRegenDelta = 0, storedMana = 0;
    double damageBonusPct = 0, reductionPct = 0, guardPct = 0;
    struct Status {
        string id, name, target;
        int amount = 0, total = 0, turns = 0, cap = 0, perTurn = 0;
    };
    vector<Status> statuses;
    string text;
};
// 用途：解析已满足境界的神通或法宝并预览。
// 参与流程：实战手牌与法宝侧栏。
// 使用方式：传角色、战斗和施术 ID，不写真实状态。
// 返回值：实际效果预览；未知或境界不足时无效。
Preview battlePreview(const Player& p, const Battle& b, const string& id);
// 目录和炼器的参考环境与真实斗法使用相同的开局属性、结算函数。
// 不抽牌、不生成对手、不消耗 RNG；参考手牌固定五张，仅用于说明换念。
// 用途：生成角色当前准备下的参考开局状态。
// 参与流程：场外神通和法宝预览。
// 使用方式：只读传角色，保留现有准备值。
// 返回值：与真实开局属性规则一致的 Battle 值。
Battle battleReferenceState(const Player& p);
// Pure next-battle preview with one preparation replaced; no Player copy or RNG.
// 用途：生成替换一项丹药准备后的只读开局环境。
// 参与流程：服药计划与目录数值预览。
// 使用方式：传角色、准备键和新值，不抽牌或生成敌人。
// 返回值：开局属性 Battle 值，不改变玩家或 RNG。
Battle battleReferenceWithPreparation(const Player& p, const string& key, double value);
// 用途：在统一开局环境中给出目录参考效果。
// 参与流程：神通目录、法宝目录与炼器预览。
// 使用方式：传角色和定义；示例使用其余四张手牌，不抽牌。
// 返回值：参考效果预览，附治疗潜力等场外说明。
Preview battleReferencePreview(const Player& p, const SpellDef& spell);
// 用途：解释蓄积、相生和抽牌等影响预览的条件。
// 参与流程：场外参考数值的条件说明。
// 使用方式：传角色和定义，从当前功法系数产生说明。
// 返回值：对应条件文案；无特定条件时为空。
string battlePreviewConditions(const Player& p, const SpellDef& spell);

// 修士按已抽到的神通和灵力决定本回合连招；预览不改变牌堆或随机数。
struct EnemyPreview {
    string description;
    vector<string> spells;
    int damage = 0;
    int shield = 0;
    int heal = 0;
};
// 用途：只读试算回合末持续状态及敌方完整连招。
// 参与流程：战况意图展示。
// 使用方式：传游戏，在副本上执行已计划行动，不重洗未知牌。
// 返回值：预计伤害、护盾、治疗、招式与说明，不消费 RNG。
EnemyPreview battleEnemyPreview(const Game& g);

// 结算
// 实际战败风险独立于恢复点；lethal 仍保留原有撤离掷骰语义。
// 用途：判断这场战斗失败是否结束角色一世。
// 参与流程：战败结算和界面风险提示。
// 使用方式：传战斗种类；恢复点不改变该风险。
// 返回值：该种类失败会结束一世时为真。
bool battleDefeatEndsLife(const Battle& b);
// 用途：按胜负或撤离结算本场战斗。
// 参与流程：先手、出牌斩杀、回合末及撤离收尾。
// 使用方式：传游戏、胜负和撤离标记；影身只记独立结局。
// 返回值：无；更新战斗/健康、成长、奖励、任务和日志。
void battleFinish(Game& g, bool win, bool escape = false);
// 用途：判断是否有仍可行动的战斗。
// 参与流程：API 动作阻塞及斗法生命周期。
// 使用方式：传游戏；影身已结算但保留战报时返回假。
// 返回值：战斗存在且没有影身结局时为真。
bool battleActive(const Game& g);

// 手牌对应的神通 id 列表（供界面显示）
// 用途：把当前手牌 UID 映射为神通 ID 列表。
// 参与流程：界面手牌序列化。
// 使用方式：传战斗，保留手牌位置顺序。
// 返回值：与手牌逐项对应的神通 ID，找不到牌时为空项。
vector<string> handSpells(const Battle& b);

} // namespace nz
