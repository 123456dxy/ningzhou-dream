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

const vector<SpellDef>& allSpells();
// 查神通；'defend' 返回内置的守势卡
const SpellDef* battleSpell(const string& id);
string battleSpellDescription(const SpellDef& spell);
bool battleNeedsTarget(const SpellDef& spell);

// 旧接口保留；永久剑意不再参与战斗伤害。
double battleSwordBonus(const Player& p);
int battleBasicCopies();
int battleSpeed(const Player& p);
double battleEscapeChance(const Player& p, const Battle& b);
// 额外神通槽位；普通攻击、防御固定携带，不占用槽位。
int battleSpellSlots(const Player& p);
// 每大境界自然回复 +1，保留功法与悟道提供的额外回复。
int battleEnergy(const Player& p);
int battleManaCap(const Player& p);
int battleNextMana(const Player& p, const Battle& b);
double battleReduction(const Player& p, const Battle& b);
// 神通实际费用：守势与灵击固定 1，其余按旧费用折半向上取整
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
Intent battleIntent(const Battle& b);

// 旧存档意图兼容；新的修士斗法直接执行完整神通效果。
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
    int sword = -1;      // 对手剑意；<0 表示按境界推算
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
void battleStart(Game& g, const EnemySpec& enemy);
// Tianji fights own an isolated encounter and immutable opponent loadout.
// Completed encounters retain Battle/logs but battleActive becomes false.
void battleStartShadow(Game& encounter, const Player& challengerSnapshot,
                       const Player& opponentSnapshot, uint32_t seed);
const string& battleShadowOutcome(const Game& encounter);

// 行动：'end' 结束回合；'escape' 撤离；其余为手牌 uid 或神通 id
struct TurnResult {
    bool ok = true;
    string error;
};
TurnResult battleTurn(Game& g, const string& id, const string& target = "");

// 抽牌（内部使用，也供测试）
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
Preview battlePreview(const Player& p, const Battle& b, const string& id);
// 目录和炼器的参考环境与真实斗法使用相同的开局属性、结算函数。
// 不抽牌、不生成对手、不消耗 RNG；参考手牌固定五张，仅用于说明换念。
Battle battleReferenceState(const Player& p);
Preview battleReferencePreview(const Player& p, const SpellDef& spell);
string battlePreviewConditions(const Player& p, const SpellDef& spell);

// 修士按已抽到的神通和灵力决定本回合连招；预览不改变牌堆或随机数。
struct EnemyPreview {
    string description;
    vector<string> spells;
    int damage = 0;
    int shield = 0;
    int heal = 0;
};
EnemyPreview battleEnemyPreview(const Game& g);

// 结算
// 实际战败风险独立于恢复点；lethal 仍保留原有撤离掷骰语义。
bool battleDefeatEndsLife(const Battle& b);
void battleFinish(Game& g, bool win, bool escape = false);
bool battleActive(const Game& g);

// 手牌对应的神通 id 列表（供界面显示）
vector<string> handSpells(const Battle& b);

} // namespace nz
