// 宁州一梦 · C++ 版 — 洞府道法（对应 JS 版 arts.mjs）
//
// 包含：十二大道与道法节点、功法与神通、炼丹与炼器、限时机缘、成就。
#pragma once

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz {

// 持续状态的安全上限；普通施法即可累积，不以功法或悟道解锁叠加资格。
constexpr int BATTLE_POISON_CAP = 99;
constexpr int BATTLE_BURN_CAP = 1000000;

// 十二大道
extern const char* const ART_DAOS[12];
// 道法境界名
extern const char* const ART_LEVELS[6];
// 各境界所需经验
extern const int ART_THRESHOLDS[6];

// 数据表查找（实现在 arts.cpp，供其它模块复用）
// 十二大道的节点定义（12 道 × 5 层 = 60 个）。
// 放在头文件里是为了让界面层能显示节点名称与效果说明。
struct NodeDef {
    int dao = 0;
    int level = 1;
    int cost = 1;
    string name;
    string desc;
};
const vector<NodeDef>& artNodes();
const NodeDef* findNode(const string& id);

// 功法的公开信息（界面上要显示名称/大道/效果，不能只给 id）
struct ManualInfo {
    bool found = false;
    string id, name, dao, effect, coefficients;
    int stage = 0, price = 0;
};
// 按 id 查一部功法（含拓展功法表）
bool findManualInfo(const string& id, ManualInfo& out);

// 可组合的流派提示；引用本地真实功法、神通和悟道节点 ID。
struct BuildInfo {
    string id, name, dao, desc;
    vector<string> manuals, spells, nodes;
};
const vector<BuildInfo>& artBuilds();

const data::RecipeDef* findRecipe(const string& id);

const data::FurnaceDef* findFurnace(const string& id);
const data::HerbDef* findHerbById(const string& id);
// 按名称查（丹方 ingredients 里写的是名称）
const data::HerbDef* findHerbByName(const string& name);
// 把丹方的 ingredients 字符串解析成 (药材id, 数量) 列表
vector<std::pair<string, int>> parseIngredients(const string& s);

int artLevel(int exp);
bool artHas(const Arts& a, const string& dao, int level);
// 大道序号（金0 木1 水2 火3 土4 神5 体6 剑7 气8 阵9 丹10 器11），未知返回 -1
int daoIndex(const string& dao);

// 主修按层数全额累计，辅修按层数的一半累计。
// 整数收益在触发时向下取整，比例收益由 battle 按对应上限结算。
struct CombatHooks {
    double discardShield = 0;   // 每张未打出而弃置的牌：气血上限比例护盾
    double discardPower = 0;    // 每张弃牌：下一直接攻击的增伤
    double waterMana = 0;       // 浪击每消耗一层水势：恢复灵力
    double waterKeep = 0;       // 浪击保留的水势比例
    double burnStack = 0;       // 每次新施加的灼烧额外增加比例，旧灼烧天然保留
    double burnExplosion = 0;   // 引爆灼烧的额外伤害比例
    double poisonHeal = 0;      // 每新增一层毒：气血上限比例治疗
    double poisonGrowth = 0;    // 回合结束：已有毒增加层数
    double shieldCharge = 0;    // 技能实际获得护盾时：增加蓄势
    double shieldStrike = 0;    // 重击附加当前护盾比例伤害
    double swordCombo = 0;      // 每多连一剑增加的追加段比例，总上限50%
    double swordMana = 0;       // 本回合每连续三张剑招：恢复灵力
    double drawCost = 0;        // 抽牌、预谋、重抽及移除牌的费用减免
    double drawBonus = 0;       // 抽牌神通的额外抽牌数
    double carryBonus = 0;      // 蓄灵神通的封存上限加成
    double quietMana = 0;       // 一回合出牌不超过二张：次回合额外灵力
    double comboMana = 0;       // 五行相生连击：恢复灵力
    double comboDraw = 0;       // 五行相生连击：抽牌（每回合最多两张）
    double bodyCounter = 0;     // 护盾格挡伤害：反震比例
    double poisonCleanse = 0;   // 治疗神通：降低自身每回合毒伤
};

// 洞府加成汇总（对应 JS 的 artMods）
struct LifeHooks {
    double brewYield = 0;     // 每炉成功时增加的丹药数，汇总后向下取整
    double forgeSpeed = 0;    // 炼器耗时缩短比例，最高60%
    double craftedSale = 0;   // 自炼法器售值增加比例，最高50%
    double escapeSpeed = 0;   // 额外遁速，战斗撤离与步行/御器共用
};

struct ArtMods : CombatHooks, LifeHooks {
    double cult = 1.0;
    double attack = 1.0;
    double sword = 1.0;
    double heal = 1.0;
    double hp = 1.0;
    double shield = 1.0;
    int mana = 8;
    double brew = 0.0;
    double forge = 0.0;
    double reduction = 0.0;
    double charge = 0.0;
    double water = 0.0;
    double poison = 0.0;
    double soul = 0.0;
    double drain = 1.0;
    double fire = 1.0;
    double steal = 0.0;
    double roundShield = 0.0;
    double openingShield = 0.0;
    double openingMana = 0.0;
    double openingQi = 0.0;
    double quietHeal = 0.0;
    double combo = 0.0;
    double burnBonus = 0.0;
    double thirdHeal = 0.0;
    double qiGain = 0.0;
    // 十二系元素加成（按 ART_DAOS 下标）
    double elements[12] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
};

const ArtMods& artModsOf(const Arts& a);
int artEscapeSpeed(const Player& p);
// 按宗门、大道与境界生成合法被动构筑；散修早期功法与神通较少。
Arts artNpcLoadout(const Npc& npc);

// 初始化洞府（新档或读旧档时调用）
void ensureArts(Game& g);

// 获得大道经验并处理升级
void artGain(Player& p, const string& dao, double amount);

// 同步药材分类与总量
void artSyncHerbs(Player& p);

double cultivationRateOf(const Player& p);

// 品阶上限（对应 JS 的 artTier）
int artTier(const Player& p);

// ---- 炼丹 ----
struct BrewInfo {
    bool valid = false;
    string recipeId;
    string recipeName;
    int grade = 1;
    int furnaceGrade = 1;
    int cost = 0;          // 开炉灵石
    double chance = 0.5;   // 成丹率
    int count = 0;         // 需要的药材总数
    int output = 1;        // 每炉成功产出，开工时冻结
};
BrewInfo artBrewInfo(const Player& p, const string& recipeId);

// ---- 炼器 ----
struct ForgeInfo {
    bool valid = false;
    int grade = 1;
    double util = 0.0;
    int cost = 0;
    double bonus = 0.0;
    double chance = 0.5;
    string shapeId;
    string shapeName;
    string shapeSlot;
    string mainName;
    string mainElement;
    string mainPolarity;
};
ForgeInfo artForgeInfo(const Player& p, const string& mainId,
                       const string& auxId, const string& coreId,
                       const string& shapeId);
struct ForgeMaterialCost {
    string id;
    string name;
    vector<string> roles;
    int required = 0;
    int owned = 0;
    int missing = 0;
};
// 相同材料兼任主料、辅料或灵核时，按材料合并开炉所需的总份数。
vector<ForgeMaterialCost> artForgeMaterials(const Player& p,
                                           const string& mainId,
                                           const string& auxId,
                                           const string& coreId);

// ---- 丹药效果 ----
struct PillEffect {
    int value = 0;
    string text;
};
PillEffect artPillEffect(const Player& p, const data::RecipeDef& r);

// 丹药战斗准备（对应 JS 的 artCombatPill）
struct CombatPill {
    string key;
    double value = 0;
    string text;
};
CombatPill artCombatPill(const data::RecipeDef& r);

// ---- 限时机缘 ----
// 返回空字符串表示可以参与；否则是拒绝原因（与 JS 的 return 文案一致）
string artQuestError(const Game& g, const data::QuestDef* q);
void artRewardQuest(Game& g, const data::QuestDef& q);

// ---- 年度洞府行动 ----
// 返回空字符串表示成功；否则是错误原因
string artAnnualError(const Game& g, const string& type, const string& id);
void artAnnual(Game& g, const string& type, const string& id);

// ---- 即时洞府操作 ----
struct ActionError {
    bool ok = true;
    string error;
};
ActionError artInstant(Game& g, const string& type, const string& id);

// 进度查询（对应 artProgress）
int artProgress(const Player& p, const string& key);

// 成就表
struct AchievementDef {
    string id;
    string key;
    int target = 0;
    string name;
    int points = 0;
};
const vector<AchievementDef>& artAchievements();

} // namespace nz
