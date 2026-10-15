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
// 用途：提供十二大道六十个节点的名称和实际机制说明。
// 参与流程：节点购买、汇总与界面显示。
// 使用方式：无参数，静态生成节点定义。
// 返回值：节点定义列表的常量引用。
const vector<NodeDef>& artNodes();
// 用途：按大道序号与层数组成的 ID 查节点。
// 参与流程：节点购买前置及节点展示。
// 使用方式：传如 7-4 的节点 ID。
// 返回值：匹配节点指针；格式或范围无效时为空。
const NodeDef* findNode(const string& id);

// 功法的公开信息（界面上要显示名称/大道/效果，不能只给 id）
struct ManualInfo {
    bool found = false;
    string id, name, dao, effect, coefficients;
    int stage = 0, price = 0;
};
// 按 id 查一部功法（含拓展功法表）
// 用途：公开功法元信息并补实际触发机制说明。
// 参与流程：功法目录、学习与配装展示。
// 使用方式：传 ID 和可写输出记录。
// 返回值：找到时为真并填写 out；无定义时为假。
bool findManualInfo(const string& id, ManualInfo& out);
// 用途：读取包含本地新功法的完整不可变目录；流程：展示、授业与来源筛选。
// 使用：按稳定ID迭代，动态资格另行报价；返回：静态内容目录常量引用，无角色状态/RNG/IO副作用。
const map<string, ManualInfo>& mfManualCatalog();

// 可组合的流派提示；引用本地真实功法、神通和悟道节点 ID。
struct BuildInfo {
    string id, name, dao, desc;
    vector<string> manuals, spells, nodes;
};
// 用途：提供由真实功法、神通和节点组成的流派建议。
// 参与流程：道法目录的构筑说明。
// 使用方式：无参数，仅读取固定示例。
// 返回值：BuildInfo 列表常量引用。
const vector<BuildInfo>& artBuilds();

// 用途：按丹方 ID 查静态配方。
// 参与流程：炼丹、服药与目录查询。
// 使用方式：传配方 ID，不消耗任何材料。
// 返回值：丹方定义指针；不存在时为空。
const data::RecipeDef* findRecipe(const string& id);

// 用途：按炉具 ID 查品阶、容量和耐久定义。
// 参与流程：炼丹预览及炉具门槛校验。
// 使用方式：传炉具 ID。
// 返回值：炉具定义指针；不存在时为空。
const data::FurnaceDef* findFurnace(const string& id);
// 用途：按药材 ID 查真实药材定义。
// 参与流程：材料目录和资源奖励。
// 使用方式：传药材 ID。
// 返回值：药材定义指针；无匹配时为空。
const data::HerbDef* findHerbById(const string& id);
// 按名称查（丹方 ingredients 里写的是名称）
// 用途：按丹方使用的药材名称查定义。
// 参与流程：配方校验、扣除与绿瓶催熟。
// 使用方式：传药材中文名称，泛用灵药不代替指定材料。
// 返回值：药材定义指针；未知名称为空。
const data::HerbDef* findHerbByName(const string& name);
// 把丹方的 ingredients 字符串解析成 (药材名称, 数量) 列表
// 用途：拆解丹方的名称与数量配料文本。
// 参与流程：炼丹预览、校验和材料扣除。
// 使用方式：传以分号分隔、冒号指定数量的 ingredients 字段。
// 返回值：药材名称及数量列表；数量解析沿用 stoi 规则。
vector<std::pair<string, int>> parseIngredients(const string& s);

// 用途：按累计经验计算统一悟道等级。
// 参与流程：各大道等级、节点解锁及升级奖励。
// 使用方式：传经验，与 ART_THRESHOLDS 阈值表比较。
// 返回值：零至五级的悟道等级。
int artLevel(int exp);
// 用途：查询角色是否已购买某大道的指定节点。
// 参与流程：战斗与生活机制触发判断。
// 使用方式：传 Arts、大道和节点层数，不只检查经验等级。
// 返回值：对应节点 ID 存在时为真。
bool artHas(const Arts& a, const string& dao, int level);
// 大道序号（金0 木1 水2 火3 土4 神5 体6 剑7 气8 阵9 丹10 器11），未知返回 -1
// 用途：解析大道为历史十二系数组索引。
// 参与流程：节点、经验和元素加成计算。
// 使用方式：传稳定 ID 或冻结中文大道键。
// 返回值：0..11 的序号；未知大道为 -1。
int daoIndex(const string& dao);

// 主修按层数全额累计，辅修按层数的一半累计。
// 整数收益在触发时向下取整，比例收益由 battle 按对应上限结算。
struct CombatHooks {
    double ElementalSwordMastery = 0; // 五行剑诀相生返灵系数，主满辅半；整数收益向下取整
    double discardShield = 0;   // 每张未打出而弃置的牌：气血上限比例护盾
    double discardPower = 0;    // 每张弃牌：下一直接攻击的增伤
    double waterMana = 0;       // 浪击每消耗一层水势：恢复灵力
    double waterKeep = 0;       // 浪击保留的水势比例
    double burnStack = 0;       // 每次新施加的灼烧额外增加比例，旧灼烧天然保留
    double burnExplosion = 0;   // 引爆灼烧的额外伤害比例
    double BurnGrowthStackPerRound = 0; // 每个已实际结算灼烧的完整回合，为之后新施加量提供附加比例。
    double BurnGrowthExplosionPerRound = 0; // 本场已兑现灼烧回合提供的引爆成长，轮数独立封顶。
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

// 用途：汇总主辅功法、悟道、法器与常驻属性收益。
// 参与流程：修炼、斗法、炼丹炼器和遁速规则。
// 使用方式：传只读 Arts，每次重算；需长期保留结果时复制该值。
// 返回值：受既有上限约束的线程局部引用；同线程下一次调用会覆盖它。
const ArtMods& artModsOf(const Arts& a);
// 玩家与 NPC 共用的丹毒修炼减速；超过20开始衰减，最低五折。
double artPoisonCultivationMultiplier(int poison);
// 用途：汇总境界、额外遁速、功法与天赋速度。
// 参与流程：赶路和斗法先手/撤离共享。
// 使用方式：传角色，使用真实属性与上限。
// 返回值：最终遁速整数。
int artEscapeSpeed(const Player& p);
// 按宗门、大道与境界生成合法被动构筑；散修早期功法与神通较少。
// 用途：为 NPC 生成宗门、大道和境界允许的被动构筑。
// 参与流程：真实修士斗法初始化。
// 使用方式：传 NPC，低阶散修沿用较少功法神通配置。
// 返回值：含主辅功法、节点和神通的 Arts 值。
Arts artNpcLoadout(const Npc& npc);

// 初始化洞府（新档或读旧档时调用）
// 用途：初始化新角色道法或补齐旧档缺项。
// 参与流程：建号、读档和动作入口兼容。
// 使用方式：传游戏，已初始化时仅补缺并去重固定牌。
// 返回值：无；修改玩家 Arts，保留已获经验、资源与玩家卸空选择。
void ensureArts(Game& g);

// 获得大道经验并处理升级
// 用途：增加指定大道经验并发放升级悟道点。
// 参与流程：修炼活动、战斗与机缘成长。
// 使用方式：传角色、大道键和经验量，先解析合法大道。
// 返回值：无；更新对应 Arts.exp 与 Arts.points。
void artGain(Player& p, const string& dao, double amount);

// 同步药材分类与总量
// 用途：将分类药材总量压回玩家药材总数。
// 参与流程：资源消耗后的药材兼容同步。
// 使用方式：传可写角色，仅移除分类总数超出的部分。
// 返回值：无；修改 Arts.herbs 各分类数量，不增加药材。
void artSyncHerbs(Player& p);

// 用途：保留历史修炼速度查询声明。
// 参与流程：旧公共接口兼容；当前没有实现或调用。
// 使用方式：新代码使用 cultivate::rate(p)。
// 返回值：历史声明的速度结果；本项目无可链接实现。
double cultivationRateOf(const Player& p);

// 品阶上限（对应 JS 的 artTier）
// 用途：按境界计算可用丹药和法器品阶上限。
// 参与流程：服药、装备与炼丹品阶校验。
// 使用方式：传角色，末小境提前解锁下一品阶。
// 返回值：最高六阶的品阶上限。
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
// 用途：计算既有炼丹费用、产量、成功率及总配料数。
// 参与流程：炼丹报价和实际开炉共同查询。
// 使用方式：传角色和丹方 ID，只读计算不扣资源。
// 返回值：BrewInfo；未知配方返回 valid 为假的值。
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
// 用途：计算既有三料器型的品阶、利用率、费用和成功率。
// 参与流程：炼器报价与实际铸炼。
// 使用方式：传角色、三种材料和器型 ID，只读预览。
// 返回值：ForgeInfo；无效材料或器型返回 valid 为假。
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
// 用途：合并三种炼器角色所需的同名材料份数。
// 参与流程：炼器预览、短缺校验和实际消耗。
// 使用方式：依次传角色、主料、辅料、灵核 ID。
// 返回值：各材料总需求、持有量和缺量；非法材料返回空列表。
vector<ForgeMaterialCost> artForgeMaterials(const Player& p,
                                           const string& mainId,
                                           const string& auxId,
                                           const string& coreId);

// ---- 丹药效果 ----
struct PillEffect {
    int value = 0;
    string text;
};
// 用途：计算丹药基础效果并生成说明。
// 参与流程：丹药目录与服药计划。
// 使用方式：传角色与丹方，套用原有属性上限和天赋条件。
// 返回值：基础效果值及说明，不消耗丹药。
PillEffect artPillEffect(const Player& p, const data::RecipeDef& r);
// 用途：查询本地丹药标称能力，不受当前角色小境、属性、天赋、库存或耐药截断。
// 参数：Recipe 为有效目录丹方；返回：本地效果值与说明，修为丹仍按真实规则受吸收上限限制。
// 副作用：无；不消耗 RNG、推进时间或写档，不保存参数引用；目录不变时可并行调用。
PillEffect mfCompletePillEffect(const data::RecipeDef& Recipe);
// 用途：以稳定丹方 ID 查询突破准备门槛；流程：服药资格与材料取得目标；用法：传目录 RecipeId；返回：小境序号，非突破丹或未登记时空值。
optional<int> mfBreakthroughPillStage(const string& RecipeId);

// 只读服药计划与实际服药共用结算；不复制角色、不消耗随机数。
// enabled 只表示场外服药自身条件，API 另叠加任务/抉择/斗法等全局阻塞。
struct PillUseInfo {
    PillEffect baseEffect;
    string effect, usage, reason;
    double effectValue = 0;
    int owned = 0, used = 0, toleranceLimit = 0, toleranceRemaining = 0;
    bool toleranceUnlimited = false, overTier = false;
    int poison = 0, poisonAfter = 0, poisonDelta = 0;
    int requiredStage = -1;
};
// 用途：公开服药计划的实际效果和可用条件。
// 参与流程：服药页面的数值与门槛预览。
// 使用方式：传角色和丹方，只读计算不复制整个角色或消耗 RNG。
// 返回值：PillUseInfo，含实际变化、耐药和拒绝原因。
PillUseInfo artPillUseInfo(const Player& p, const data::RecipeDef& r);
// 应用同一服丹计划，不写日志；NPC 使用临时角色视图复用耐药、丹毒与属性上限。
// 成功为空，失败不改变角色；调用方负责记录动作日志。
string artApplyPill(Player& player, const string& id);
// 空字符串表示成功；失败不修改任何资源，成功保留原有服药日志。
// 用途：验证并应用同一服药计划。
// 参与流程：场外服药动作入口。
// 使用方式：传游戏和丹方 ID，失败前不改任何资源。
// 返回值：成功为空；失败为原因，成功更新属性、库存、丹毒和日志。
string artUsePill(Game& g, const string& id);


// 丹药战斗准备（对应 JS 的 artCombatPill）
struct CombatPill {
    string key;
    double value = 0;
    string text;
};
// 用途：从稳定丹方 ID 获取下场战斗准备种类。
// 参与流程：战斗丹服药计划与准备提示。
// 使用方式：传丹方，读取本地已实现的准备规则，不匹配中文文案。
// 返回值：准备键、值和原有说明，不叠加同类准备。
CombatPill artCombatPill(const data::RecipeDef& r);

// ---- 限时机缘 ----
// 返回空字符串表示可以参与；否则是拒绝原因（与 JS 的 return 文案一致）
// 用途：检查机缘的结束年份、地点、前置和资源门槛。
// 参与流程：年度机缘行动执行前。
// 使用方式：传游戏与可为空的定义，不消费资源。
// 返回值：拒绝原因；满足参与条件时为空。
string artQuestError(const Game& g, const data::QuestDef* q);
// 用途：发放未完成机缘的真实一次性奖励。
// 参与流程：非战斗机缘或机缘战胜结算。
// 使用方式：传游戏与定义，已完成的机缘直接返回。
// 返回值：无；修改机缘记录、经验、资源、抄本和奖励日志。
void artRewardQuest(Game& g, const data::QuestDef& q);

// ---- 年度洞府行动 ----
// 返回空字符串表示成功；否则是错误原因
// 用途：验证年度洞府行动的资源及门槛。
// 参与流程：学习、参悟、炼制、演武和机缘执行前。
// 使用方式：传游戏、行动类型和对应 ID；炼器 ID 为四段配料编码。
// 返回值：拒绝原因；既有允许分支返回空。
string artAnnualError(const Game& g, const string& type, const string& id);
// 用途：结算已通过校验的年度洞府行动。
// 参与流程：经济行动完成后的成长、炼制与机缘执行。
// 使用方式：传游戏、行动类型和 ID，调用前先查 artAnnualError。
// 返回值：无；按原有规则修改资源、成长、制作结果、来源记录、战斗与 RNG。
void artAnnual(Game& g, const string& type, const string& id);

// ---- 即时洞府操作 ----
struct ActionError {
    bool ok = true;
    string error;
};
// 用途：执行不跨时间的道法、配装和斗法操作。
// 参与流程：API 即时洞府动作入口。
// 使用方式：传游戏、操作类型和 ID；先检查事件与战斗阻塞。
// 返回值：ActionError；成功按分支更新悟道点、配装、功法或战斗。
ActionError artInstant(Game& g, const string& type, const string& id);
// 用途：检查法器存在及当前境界的装备品阶。
// 参与流程：法器装备动作前置。
// 使用方式：传角色和法器 ID，只读查行囊。
// 返回值：拒绝原因；可装备时为空。
string artEquipError(const Player& p, const string& id);

// 进度查询（对应 artProgress）
// 用途：读取指定成就进度计数。
// 参与流程：成就展示与达标判断。
// 使用方式：传角色及 nodes/manuals/brew/forge/wins/quests/wealth 键。
// 返回值：对应进度整数；未知键为零。
int artProgress(const Player& p, const string& key);

// 成就表
struct AchievementDef {
    string id;
    string key;
    int target = 0;
    string name;
    int points = 0;
};
// 用途：读取成就目标与悟道点奖励表。
// 参与流程：进度展示与成就领取。
// 使用方式：无参数，从静态成就数据构建列表。
// 返回值：成就定义列表常量引用。
const vector<AchievementDef>& artAchievements();

} // namespace nz
