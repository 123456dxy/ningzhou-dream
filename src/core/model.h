// 宁州一梦 · C++ 版 — 核心数据模型
//
// 这一层完全对应 JS 版 sim.mjs 里的游戏状态对象 s。
// 字段名尽量保持和 JS 一致，方便两版对照阅读与调试。
#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/base.h"

namespace nz {

// ============================================================
// 静态规则表（对应 JS 版 sim.mjs 顶部的常量）
// ============================================================

// 境界。need = 升到下一境所需修为；life = 该境寿元上限；power = 战力基数。
struct RealmDef {
    const char* name;
    int need;
    int life;
    int power;
};

inline const std::vector<RealmDef>& REALMS() {
    static const std::vector<RealmDef> data = {
        {"炼气初期", 110, 100, 14},    {"炼气中期", 190, 100, 24},
        {"炼气后期", 320, 100, 39},    {"筑基初期", 900, 200, 65},
        {"筑基中期", 1600, 200, 95},   {"筑基后期", 2800, 200, 140},
        {"金丹初期", 3600, 500, 210},  {"金丹中期", 5400, 500, 310},
        {"金丹后期", 8000, 500, 440},  {"元婴初期", 18000, 1000, 660},
        {"元婴中期", 30000, 1000, 900},{"元婴后期", 46000, 1000, 1250},
        {"化神", 0, 2000, 1800},
    };
    return data;
}

// 宗门
struct SectDef {
    const char* id;
    const char* name;
    const char* element;
    const char* motto;
    const char* description;
    const char* color;
};

inline const std::vector<SectDef>& SECTS() {
    static const std::vector<SectDef> data = {
        {"jinhong", "金虹剑派", "金", "一剑照长空",
         "以剑入道，锋芒藏于静气。修炼额外获得剑道经验。", "#b99a59"},
        {"xinghe", "星河剑派", "水", "星河入梦来",
         "观水悟剑，绵延不绝。论道收益提高。", "#779dab"},
        {"zhushan", "竹山宗", "木", "万木皆有生",
         "草木成荫，灵药生息。采药产出提高。", "#70947a"},
        {"lihuo", "离火门", "火", "一念燃山海",
         "以火炼心，以丹养道。炼丹成功率提高。", "#b77459"},
        {"huachen", "化尘教", "土", "尘中见天地",
         "厚积凝元，守静笃行。闭关消耗减少。", "#aa916b"},
    };
    return data;
}

// 地点
struct LocationDef {
    const char* id;
    const char* name;
    const char* type;   // city / sect / wild
    int x;
    int y;
    const char* description;
    int danger;
};

inline const std::vector<LocationDef>& LOCATIONS() {
    static const std::vector<LocationDef> data = {
        {"wuling", "武陵城", "city", 70, 48, "东部河湾的修士都会，白帝楼、升仙台与灵药堂聚于此地。", 0},
        {"yifeng", "逸风城", "city", 79, 67, "武陵东南的行旅之城，斗法台与宝器轩迎来四方修士。", 0},
        {"yunxi", "云汐城", "city", 43, 25, "北部水巷烟雨之城，中草阁汇集宁州药材。", 0},
        {"tianxing", "天星城", "city", 63, 28, "北部腹地的书市之城，问道楼收藏功法、神通与修行入门书籍。", 0},
        {"guangling", "广陵城", "city", 16, 87, "西南海岸的商埠，百宝行经营炼器材料，码头通往海外。", 0},
        {"jinhong", "金虹剑派", "sect", 88, 17, "永安镇以北的金系山门，剑修传承与连石山相接。", 0},
        {"xinghe", "星河剑派", "sect", 24, 12, "云汐西北的水系剑派，临水山门与风雷谷遥相呼应。", 0},
        {"zhushan", "竹山宗", "sect", 48, 86, "宁州南部竹海中的木系宗门，药田与方壶山相接。", 0},
        {"lihuo", "离火门", "sect", 59, 10, "天星以北的火系山门，赤岩丹炉长明，后山通往沃焦。", 0},
        {"huachen", "化尘教", "sect", 22, 55, "西部山川间的土系山门，层台石殿俯瞰正阳山。", 0},
        {"fanghu", "方壶山", "wild", 60, 94, "竹山宗东南的灵药险峰，碧幽潭藏于山间。", 22},
        {"qingshi", "青石灵脉", "wild", 7, 30, "宁州西部与衡州接壤的灵核矿脉，由五宗轮流管理。", 34},
        {"sea", "无尽海", "wild", 96, 76, "宁州之外的广阔海域，灵舟远行与高阶海兽同在。", 80},
        {"fenglei", "风雷谷", "wild", 12, 11, "星河剑派后山的幽谷，寒月潭与惊雷谷藏着水道机缘。", 22},
        {"wojiao", "沃焦山", "wild", 68, 6, "离火门后山的火山地带，火脉与药田共生。", 45},
        {"lianshi", "连石山", "wild", 76, 11, "金虹剑派后山的矿山与药田，剑壁留有前人的感悟。", 22},
        {"zhengyang", "正阳山", "wild", 12, 42, "化尘教北面的山地，土系灵矿与药材产地。", 22},
        {"xiuluo", "岫络谷", "wild", 35, 37, "云汐西南的山谷，宁州古迹出世时各宗修士聚集于此。", 38},
        {"yishan", "沂山", "wild", 49, 43, "宁州中部的山地，小派驻地与峡谷药田并存。", 22},
        {"heshan", "禾山", "wild", 60, 43, "武陵西北的山林，药材、灵猴与禾山派传承见于其中。", 22},
        {"wushan", "巫山", "wild", 11, 66, "化尘教南面的山地，药草生长，山脚洞窟亦有魔影传闻。", 22},
        {"tianji", "天机阁", "sect", 41, 68, "宁州中南部的消息与珍品集散之所，外务执事常在各地行走。", 0},
        {"dongshi", "东石谷", "city", 62, 64, "武陵西南的修士集会之地，坊市与伴月楼拍卖会相邻。", 0},
        {"jiuyi", "九嶷山", "wild", 65, 79, "宁州南部的药山，偏僻山道与破庙藏着修士故事。", 22},
        {"yongan", "永安镇", "city", 85, 37, "武陵东北的小镇，学堂与民宅沿道路分布。", 0},
        {"daiyu", "岱舆山", "wild", 92, 49, "武陵东侧临海的药山，山林盛产修行所需草药。", 22},
        {"xuandao", "玄道宗", "sect", 88, 83, "宁州东南的宗门，前往天魔眼的修士在此集结。", 0},
        {"tianmo", "天魔眼", "wild", 83, 94, "宁州东南的魔物之地，灵核与危险并存。", 85},
    };
    return data;
}

// 月度计划
struct PlanDef {
    const char* id;
    const char* name;
    const char* icon;
    const char* desc;
};

inline const std::vector<PlanDef>& PLANS() {
    static const std::vector<PlanDef> data = {
        {"cultivate", "闭关修炼", "◈", "按天积攒修为 · 灵石按年费分摊"},
        {"gather", "采集灵药", "❧", "获得灵药 · 野外产出与风险更高"},
        {"work", "坊市营生", "◇", "赚取灵石 · 少量修为与见闻"},
        {"explore", "游历寻缘", "⌁", "见闻与机缘 · 有受伤风险"},
        {"rest", "休养调息", "☯", "恢复伤势 · 少量修为"},
    };
    return data;
}

// 天赋
struct TalentDef {
    const char* id;
    const char* name;
    const char* tag;
    int cost;
    const char* desc;
    const char* stat;   // 空表示不加成单项属性
    int value;
};

inline const std::vector<TalentDef>& TALENTS() {
    static const std::vector<TalentDef> data = {
        // 基础九项
        {"sword", "剑心通明", "剑道", 4, "资质 +6 · 初始剑道经验 +32", "", 0},
        {"herb", "丹道初识", "丹道", 4, "丹术 +20 · 灵药 +4", "", 0},
        {"root", "天灵根", "修炼", 7, "资质 +22 · 初始灵石 −30", "", 0},
        {"luck", "福缘深厚", "机缘", 5, "福缘 +22 · 交往好感额外 +2", "", 0},
        {"longlife", "长生根骨", "寿元", 4, "各境界寿元上限 +15 年", "", 0},
        {"merchant", "商贾世家", "资粮", 3, "营生收入 +18% · 购入价格 −10%", "", 0},
        {"body", "淬体根骨", "体魄", 3, "体魄 +2 · 休养额外恢复 8 气血", "", 0},
        {"steady", "道心坚毅", "突破", 3, "成功率 +4% · 失败损失 15%（化神 25%）", "", 0},
        {"solitary", "山野悟道", "散修", 2, "散修闭关效率 +8%", "", 0},
        // 拓展项
        {"savvy", "过目不忘", "气运", 3, "悟性 +12 · 研习时间 −20%", "insight", 12},
        {"rich", "家财万贯", "气运", 4, "初始灵石 +100", "stones", 100},
        {"poor", "家境清寒", "磨砺", -2, "初始灵石 −70", "stones", -70},
        {"weak", "体弱多病", "磨砺", -2, "资质 −8", "apt", -8},
        {"unlucky", "时运不济", "磨砺", -2, "福缘 −15", "luck", -15},
        {"short", "早年暗伤", "磨砺", -2, "寿元 −12 年", "lifeBonus", -12},
        {"bright", "聪慧早成", "气运", 2, "悟性 +7", "insight", 7},
        {"craft", "锻铁学徒", "气运", 2, "体魄 +1，灵石 +15", "body", 1},
        {"silver", "药圃出身", "气运", 2, "灵药 +6", "herbs", 6},
        {"blade", "剑术师承", "气运", 3, "初始剑道经验 +24", "sword", 12},
        {"calm", "心静如水", "气运", 2, "悟性 +6", "insight", 6},
        {"wild", "山林之子", "气运", 2, "福缘 +10", "luck", 10},
        {"sturdy", "筋骨强健", "气运", 3, "体魄 +3", "body", 3},
        {"water", "水灵根", "水道", 4, "资质 +12 · 每消耗一层水势返还1灵力", "apt", 12},
        {"fire", "火灵根", "火道", 4, "资质 +12 · 丹术 +5 · 引爆灼烧伤害 +10%", "apt", 12},
        {"wood", "木灵根", "木道", 4, "资质 +12 · 灵药 +2 · 每施加一层毒恢复0.5%气血", "apt", 12},
        {"gold", "金灵根", "金道", 4, "资质 +12 · 初始金道经验 +6 · 每弃一张牌获得1%气血护盾", "apt", 12},
        {"earth", "土灵根", "土道", 4, "资质 +12 · 体魄 +1 · 重击追加当前护盾15%的伤害", "apt", 12},
        {"genius", "悟性超群", "气运", 7, "悟性 +25", "insight", 25},
        {"fortune", "命带贵人", "气运", 3, "福缘 +14", "luck", 14},
        {"frugal", "勤俭持家", "气运", 2, "初始灵石 +45", "stones", 45},
        {"alchemy", "丹火童子", "丹道", 3, "丹术 +14 · 每炉成功产出 +1，最高4枚", "alchemy", 14},
        {"wanderer", "远游见闻", "气运", 2, "悟性 +5，福缘 +3", "insight", 5},
        {"hard", "百折不挠", "气运", 3, "体魄 +2，悟性 +4", "body", 2},
        {"mortal", "凡骨求道", "磨砺", -3, "资质 −12", "apt", -12},
        {"daoBody", "先天道体", "修炼", 4, "修炼效率 +10% · 筑基后灵气容量 +2", "", 0},
        {"immortal", "转世仙人", "悟道", 6, "悟性 +10 · 开局悟道点 +1 · 每次大境界突破额外悟道点 +1", "insight", 10},
        {"blessing", "天道福泽", "悟道", 4, "获得的大道经验 +20%", "", 0},
        {"dragonBlood", "龙族血脉", "体魄", 5, "体魄 +1 · 每回合结束恢复3%气血", "body", 1},
        {"oddMeridian", "先天异脉", "磨砺", -3, "丹药与妖丹无法提升修为", "", 0},
        {"greenBottle", "神秘绿瓶", "丹道", 3, "每月耗费5灵石催熟一份已有的一、二品草药；不催生妖丹，秘境中暂停", "", 0},
        {"lightStep", "轻功高手", "遁速", 2, "遁速 +6 · 影响先手、步行御器耗时与逃遁", "", 0},
        {"artisan", "百炼匠心", "器道", 3, "炼器时间 −15% · 自炼法器售值 +15%，遵守生活加成上限", "", 0},
        {"family", "修真家族", "传承", 3, "开局修得一层蛮牛护体功并领悟聚气", "", 0},
        {"spiritEyes", "魂穿异世", "神识", 2, "起始神识 +3", "", 0},
    };
    return data;
}

// 属性分配项
struct BuildStatDef {
    const char* id;
    const char* name;
    const char* desc;
    int max;
};

inline const std::vector<BuildStatDef>& BUILD_STATS() {
    static const std::vector<BuildStatDef> data = {
        {"apt", "灵根资质",
         "每点 +3 资质。资质是修炼速度的乘数（以 90 为基准，上限 200 处约 ×2），并提高突破成功率（最高 +8%）。灵根越好，同样的功法吸收得越多。", 10},
        {"luck", "福缘",
         "每点 +4 福缘。影响游历机缘与斗法中的运气（相对 45，最高 +8%），不影响正面斗法的伤害。", 10},
        {"insight", "悟性",
         "每点 +3 悟性。悟性是修炼速度的乘数（以 37 为基准，上限 200 处约 ×2），也加快功法参悟与修习，并提高突破成功率（最高 +12%）。", 10},
        {"body", "体魄",
         "每点 +6% 斗法气血上限；前 8 点满 +48%，之后每点只 +2%，总加成封顶 +72%。不再提供减伤 —— 抗打靠气血，不靠免伤。", 8},
        {"wealth", "家境", "每点 +20 初始灵石，便于早期配给与炼丹。", 8},

    };
    return data;
}

// ============================================================
// 常量（对应 JS 版 sim.mjs）
// ============================================================
inline constexpr int POPULATION[5] = {432, 190, 72, 24, 2};
inline constexpr int HERB_CAP = 4800;
inline constexpr int CORE_CAP = 240;
inline constexpr int PILL_CAP = 180;
inline constexpr int BASE_POINTS = 10;
inline constexpr int MAX_POINTS = 40;

// ============================================================
// 查找工具
// ============================================================
const RealmDef& realmOf(int stage);
const SectDef* findSect(const string& id);
const LocationDef* findLoc(const string& id);
const TalentDef* findTalent(const string& id);
const PlanDef* findPlan(const string& id);
bool isCity(const string& locId);

// 取境界名的 string 版本。RealmDef::name 是 const char*，
// 直接参与字符串拼接会退化成指针运算，所以统一用这个函数。
inline string realmName(int stage) {
    return realmOf(stage).name;
}

// ============================================================
// 人物数据
// ============================================================

// 属性分配结果
struct Build {
    int budget = BASE_POINTS;
    int spent = 0;
    vector<string> traits;
    map<string, int> allocation;
    int apt = 55;
    int luck = 45;
    int insight = 10;
    int sword = 2;
    int alchemy = 5;
    int body = 0;
    int lifeBonus = 0;
    int stones = 100;
    int herbs = 3;
};

// 一张神通卡（对应 JS 的 b.deck 元素）
struct Card {
    string uid;      // "card-0" 形式，牌组内唯一
    string spell;    // 神通 id；'strike' / 'defend' 为通用卡
};

struct Player;

// 人类对手使用同一套神通效果、灵力和牌堆规则。
struct EnemyCombat {
    bool enabled = false;
    int stage = 0, speed = 0, body = 0, mind = 0;
    string sect, dao, main;
    vector<string> aux, nodes;
    map<string, int> manuals;
    vector<Card> deck;
    vector<string> hand, draw, discard, exhausted, planned;
    map<string, int> cooldowns;
    int mana = 0, maxMana = 3, shuffles = 0, played = 0, turns = 0;
    bool attacked = false;
    int swordQi = 0, water = 0, soul = 0, charge = 0, lock = 0;
    int carryMana = 0, manaBonus = 0, manaPenalty = 0, drawNext = 0;
    int carryCap = 0, hpCostPct = 0, poison = 0, poisonTurns = 0;
    int burn = 0, burnTurns = 0, swordSequence = 0, comboDraws = 0;
    int poisonDamage = -1;         // exact shadow poison tick; -1 keeps legacy stack semantics
    double buff = 0, reduction = 0, guard = 0, discardPower = 0;
    string last;
};

// 斗法状态（对应 JS 的 p.arts.battle）。
// Demo 07 起改为卡牌斗法：每回合抽 5 张，出牌不触发反击，
// 「结束回合」才结算持续伤害、敌方行动与护盾。
struct Battle {
    // Detached immutable human loadout; never refers to an account or NPC world.
    // The pointed-to Player has no battle, preventing recursive snapshot graphs.
    std::shared_ptr<const Player> shadowPlayer;
    string shadowOutcome;          // empty / win / loss / draw / retreat
    EnemyCombat enemyCards;
    bool playerFirst = true;
    int playerSpeed = 0, enemySpeed = 0;
    int enemyStage = 0;
    string enemySect, enemyGender;
    string enemy;                  // 对手名
    string npcId;                  // 对手 NPC id，可能为空
    string quest;                  // 关联机缘 id
    string kind = "spar";          // spar / hunt / wild / rob / kill ...
    string tideEventId;             // 兽潮场次标识，避免把旧战果计入下一场兽潮
    int tideBattlefield = -1;        // 0/1/2/3 对应炼气/筑基/金丹/元婴战场
    int tideDaysCharged = 0;         // 本场已计入日历的回合天数
    bool lethal = false;           // 是否生死斗
    int turn = 0;
    int hp = 1;
    int max = 1;
    int enemyHp = 1;
    int enemyMax = 1;
    int enemyPower = 14;
    int enemySword = 0;            // 旧存档兼容字段，不参与伤害或境界修正
    bool enemySwordCult = false;   // 对手是否剑修
    int mana = 0;                  // 当前灵力
    int maxMana = 3;               // 每回合自然回复，随大境界和功法成长
    int shield = 0;
    int enemyShield = 0;
    int burn = 0;
    int swordQi = 0;               // 本场剑气（0–8），与永久剑意分开计算
    double buff = 0;
    double reduction = 0;
    int speed = 0;
    double guard = 0.0;            // 本回合额外减伤（阵道守势给 0.1）
    bool attacked = false;         // 本回合是否出过直接攻击牌
    int played = 0;                // 本回合已出牌数（上限 24）
    string style = "剑";
    bool retreat = false;
    bool won = false;

    // ---- 卡牌 ----
    vector<Card> deck;             // 全牌组
    vector<string> hand;           // 手牌（存 uid）
    vector<string> draw;           // 抽牌堆（存 uid）
    vector<string> discard;        // 弃牌堆（存 uid）
    vector<string> exhausted;      // 本场已移除卡（存 uid；仍保留 deck 定义）
    int shuffles = 0;

    // ---- 敌方意图（在玩家出牌前就已确定）----
    string intentKind;
    string intentName;
    double intentScale = 0;
    int intentRaw = 0;             // 已算好的原始伤害
    string intentSpell;            // 对方这回合用的是哪门神通（平A为空）

    // ---- 敌方功法（人类修士用神通，妖兽力大砖飞）----
    string enemyDao;               // 对方本命大道
    vector<string> enemySpells;    // 对方会的神通
    bool enemyBeast = false;       // 妖兽：不会神通，纯平A
    map<string, int> enemyCd;      // 对方神通的冷却
    int enemyWeaken = 0;           // 对方削弱你下回合伤害的层数

    // ---- 状态 ----
    map<string, int> cooldowns;    // 神通 id -> 剩余回合数
    int poison = 0;
    int poisonTurns = 0;
    int poisonDamage = -1;         // exact shadow poison tick after flat cleansing
    int playerPoison = 0;          // 敌方施加给玩家的每回合毒伤
    int playerPoisonTurns = 0;
    int burnTurns = 0;
    int water = 0;
    int soul = 0;
    int charge = 0;
    int lock = 0;                  // 悬灵术：接下来两回合灵力 −1
    // ---- 灵力经营（参考杀戮尖塔的能量机制）----
    // 三条路线各有代价，不能白拿：
    //   蓄灵 carryMana   —— 本回合没花完的灵力留到下回合（但有上限）
    //   聚气 manaBonus   —— 每回合回满的基数提高（代价是气血上限或受创加深）
    //   夺灵 burst       —— 立刻拿灵力（代价是气血 / 丹毒 / 下回合减少）
    int carryMana = 0;             // 结转到下回合的灵力
    int manaBonus = 0;             // 本场战斗每回合额外灵力
    int manaPenalty = 0;           // 本场战斗每回合减少灵力（负面代价）
    int drawNext = 0;              // 下回合额外抽牌
    int pendingDraw = 0;           // 本回合待抽（结算时由 battleTurn 执行）
    int carryCap = 0;              // 本场「蓄灵」的上限（0 表示不能蓄）
    int hpCostPct = 0;             // 本场因聚气付出的气血上限比例
    double discardPower = 0;       // 弃牌积攒的下一次直接攻击增伤，最高30%
    int swordSequence = 0;         // 本回合连续剑系出牌数
    int comboDraws = 0;            // 本回合相生额外抽牌触发数，最高2
    string last;                   // 上一手大道，用于相生连击
    vector<string> log;

    // 游历斗法的上下文（对应 JS 把 adventure 挂在 battle 上）
    string advKind;
    int advSeq = 0;
    string advChoice;
    int advFee = 0;
    int advGrade = 1;
    string advBook;
    string advNpcId;
    string advStyle;
    int huntFee = 0;               // 城市猎妖委托赏金，随战斗存档
    string wildTask;               // 野外悬赏 id，区别于游历遭遇
    string dungeonSite;
    string dungeonNode;
};

// 冷却查询与递减（对应 JS 在 battleTurn 里对 b.cooldowns 的处理）
inline int battleCooldown(const Battle& b, const string& spellId) {
    auto it = b.cooldowns.find(spellId);
    return it == b.cooldowns.end() ? 0 : it->second;
}
inline void battleTickCooldowns(Battle& b) {
    for (auto& [k, v] : b.cooldowns) v = std::max(0, v - 1);
}

// 法器（对应 JS 的 a.gear 元素）
struct Gear {
    string id;
    string name;
    string slot;        // weapon / armor / accessory
    string element;     // 五行属性
    string polarity;    // 天阳 / 地阴
    int grade = 1;
    double bonus = 0.0;
    bool inscribed = false;   // 是否铭刻
    int price = 0;
    int craftCost = 0;        // 铸造成本，转售时用于定价
    bool crafted = false;    // 玩家亲自炼制；购买与NPC装备不享制造专属溢价
    vector<int> dimensions;   // 五维
    string shape;            // sword / seal / armor / bell / pearl
};

// 洞府数据（对应 JS 的 p.arts）。
// 字段名与 JS 版一一对应，方便两版对照调试。
struct Arts {
    string focus = "气";                       // 主修大道
    map<string, int> exp;                      // 十二大道经验
    int points = 0;                            // 未分配的悟道点
    vector<string> nodes;                      // 已点亮节点，形如 "0-1"
    map<string, int> manuals;                  // 已修习功法 id -> 层数
    string main = "tuna";                      // 主修功法
    vector<string> aux;                        // 辅修功法，最多 4
    vector<string> spells;                     // 已携带神通
    vector<string> knownSpells;                // 已领悟神通；携带占用额外槽位
    map<string, int> herbs;                    // 已分类药材：名称 -> 数量
    map<string, int> potions;                  // 已炼成丹药 id -> 数量
    map<string, int> used;                     // 丹药服用记录
    int poison = 0;                            // 丹毒
    string furnace = "furnace-1";              // 当前丹炉
    int durability = 40;                       // 丹炉耐久
    map<string, int> materials;                // 炼器材料 id -> 数量
    vector<Gear> gear;                         // 拥有的法器
    map<string, string> equipped;              // 槽位 -> 法器 id
    int heart = 0;                             // 心境
    int mind = 0;                              // 神识
    int speed = 0;
    int hpBonus = 0;
    int aptPills = 0;                          // 本世资质丹已提升点数（上限 10）
    string breakBuff;                          // 突破增益（空/step/stage）
    double combatBuff = 0;
    int repel = 0;                             // 驱邪符效力
    struct Progress {
        int brew = 0;
        int forge = 0;
        int wins = 0;
        int quests = 0;
        int wealth = 0;
    } progress;
    map<string, int> quests;                   // 已完成的限时机缘
    map<string, int> visits;
    // 斗法（在途）
    optional<Battle> battle;
    // 斗法起手准备（对应 JS 的 combatPrep / combatBuff）
    map<string, double> combatPrep;
    // 已获得的功法抄本（对应 economy 的 books）
    vector<string> bookList;

    bool battleActive() const { return battle && battle->shadowOutcome.empty(); }
};

// 玩家对某个 NPC 的往来记录。
// 与好感度分开：好感是「态度」（可负，会掉），交情是「人情债」（只增，花掉才减）。
struct NpcBond {
    int credit = 0;        // 交情：只增不减（花掉才减），用来换对方背包里的东西
    int spent = 0;         // 累计花掉的交情，界面展示用
    bool greeted = false;  // 是否已打过招呼 —— 打招呼只有第一次加好感
    int greetYear = -1;    // 第一次打招呼的年份
    int talkMonth = -1;
    int giftMonth = -1;
    int giftsThisMonth = 0;
    int discussMonth = -1;
    int discussCount = 0;
    int travelMonth = -1;
    int sparMonth = -1;
    int learnMonth = -1;
    int learnedThisMonth = 0;
};

// 星标 NPC / 玩家共用的基础字段
struct Person {
    string name;
    string gender = "女";
    int age = 16;
    int lifespan = 100;
    int stage = 0;       // 境界序号 0..12
    int xp = 0;
    int apt = 55;
    int luck = 45;
    int insight = 10;
    int sword = 2;
    int alchemy = 5;
    int health = 100;
    int stones = 100;
    int herbs = 3;
    int pills = 0;
    int cores = 0;
    int body = 0;            // 体魄：降低游历伤害、减少斗法受伤
    int lifeBonus = 0;       // 寿元额外加成（天赋/丹药）
    string location = "wuling";
    map<string, int> relations;
};

// NPC
struct Npc : Person {
    string id;
    int bornYear = -1;
    string sect;                    // 空表示散修
    string personality;
    string goal = "修炼";
    bool alive = true;
    bool isNewborn = false;
    int deathYear = -1;             // -1 表示尚未离世
    vector<HistoryEntry> history;

    // ---- 背包与家当 ----
    // 玩家可以花「交情」从对方背包里换取东西（价格按便宜时期的坊市价）。
    // 玩家也可以送装备/丹药，NPC 会真的用起来 —— 比自身装备好就换上。
    map<string, int> bag;           // 物品 id -> 数量（丹药/药材/材料）
    vector<Gear> gear;              // 身上带的法器
    map<string, string> equipped;   // 槽位 -> 法器 id
    vector<string> pillsUsed;       // 服过的丹药（避免重复算效果）

    // ---- 功法与流派 ----
    // 人类修士的功法要和流派契合：有本命神通、有主修大道，
    // 打起来是「神通 + 平A」配合，不是只会平A。
    // 「力大砖飞」那种纯平A留给妖兽。
    string dao = "剑";              // 主修大道（剑/火/水/木/金/土/神/体/气/阵/丹/器）
    vector<string> spells;          // 会用的神通 id
    string combatProfileId;         // 可选稳定构筑 ID；特殊修士的实际战斗与兽潮积分共用
    bool beast = false;             // 是否妖兽（妖兽走「力大砖飞」那一套）
};

// 玩家
struct Player : Person {
    string talent = "sword";
    vector<string> traits;
    Build build;
    string sect;
    int renown = 0;
    int highestStage = 0;
    vector<std::pair<string, int>> achievements;  // 名称 -> 年份
    Arts arts;

    // ---- 道友往来 ----
    // relations 是好感度（-100~100，可负，代表「对方对你的态度」）。
    // npcBonds 是交情（人情债，只增不减，用来换对方背包里的东西）
    // 以及「是否打过招呼」—— 打招呼只有第一次有收益。
    map<string, NpcBond> npcBonds;

    // ---- 洞府与配给 ----
    // 洞府所在地：只有在这里闭关，灵气配给才生效。
    string cave;
    int caveTier = 0;
    // 灵石配给 0..3：花灵石买修炼资源，任何地方闭关都算。
    int stoneSupply = 0;
    // 灵气配给 0..3：更贵，且只在洞府闭关时生效。
    int qiSupply = 0;
    // 本月配给已经扣过费的月份，避免一个月内重复扣
    int supplyPaidMonth = -1;
};


// 世界
struct SectState {
    int treasury = 3000;
    int influence = 50;
    int members = 0;
};

struct World {
    int prosperity = 72;
    double market = 1.0;
    string mineOwner = "jinhong";
    int herbs = 3200;
    int coreStock = 120;
    int pillStock = 90;
    string weather = "春和景明";
    map<string, SectState> sects;
};

// 月历
struct Clock {
    int days = 0;
    int ageBase = 0;
    map<string, double> carry;      // 跨行动累积的小数收益
};

// 在途工作（对应 JS 的 clock.job）
struct Job {
    int craftOutput = 1;
    int travelChecks = 0;
    int travelAmbushes = 0;
    string actionType;              // plan / travel / join / breakthrough / social / brew / ...
    string name;
    string plan;                    // plan 类型时的具体计划
    int total = 0;
    int remaining = 0;
    int started = 0;
    int cost = 0;
    bool paid = true;
    int gift = 0;
    double chance = 1.0;            // 成功率（炼丹/炼器/委托）
    int fee = 0;                    // 工钱或赏金
    int batch = 1;                  // 批量炉数
    string destination;
    string origin;                 // 赶路出发地；到达前地点保持不变
    string travelMode;             // walk / boat / flight，旧档缺省boat
    string sect;
    string npcId;
    string kind;                    // social 的 talk/gift/spar；委托的 brew/forge
    // 突破参数
    bool breakthroughPrepared = false; // 辅助资源消耗前锁定本次突破参数
    bool breakthroughMajor = false;
    double breakthroughChance = 0.0;
    double breakthroughFailureLoss = 0.0;
};

// 俸禄账簿（对应 JS 的 s.bonds.payroll）
struct Payroll {
    string sect;          // 记账时的所属宗门，空表示散修
    int lastMonth = 0;
    int balance = 0;      // 待领俸禄
    int credited = 0;     // 已记账月数
};

// 人际关系与因缘（对应 JS 的 s.bonds）
struct Bonds {
    Payroll payroll;
    string master;                 // 师尊 NPC id
    vector<string> disciples;      // 弟子，最多两人
    string partner;                // 道侣 NPC id
    int stipend = -1;              // 上次领俸禄的月份
    int lessons = 0;
    int mentorMonth = -1;
    int discipleMonth = -1;
    int partnerMonth = -1;
    int bondDays = 0;
    int notoriety = 0;             // 恶名，40 以上暂停俸禄
    int wildWins = 0;
    int wildMonth = -1;
    int wildCount = 0;
    int aptStory = 0;
    map<string, int> stories;      // 已完成的原创支线
    vector<string> achievementTexts;
    vector<std::pair<string, string>> origins;   // 天赋 id -> 起源文本
};

// 游历状态（对应 JS 的 s.adventure，Demo 06 新增）
struct Adventure {
    int since = 0;          // 距上次遇事累计的有效游历天数
    int total = 0;          // 累计游历天数
    int seq = 0;            // 遇事序号
    int events = 0;         // 遇事次数
    int wins = 0;           // 游历斗法胜场
    string lastKind;        // 上次遇事类型（避免连续重复）
    int backhillMonth = -1; // 本月是否已探访后山
};

// 待处理抉择
struct ChoiceOption {
    string id;
    string name;
    string desc;
    bool disabled = false;
};

// 营生与坊市状态（对应 JS 的 s.economy）
struct Economy {
    vector<string> books;                 // 已获得的功法抄本 id
    int contribution = 0;                 // 宗门贡献
    map<string, int> taskCounts;          // 本月各类任务已接次数
    int huntCounts = 0;                   // 猎妖完成次数
    int commissionBrew = 0;               // 炼丹委托完成数
    int commissionForge = 0;              // 炼器委托完成数
    int cityTasks = 0;
    int month = -1;                       // 已刷新到的月份
    map<string, int> orders;              // 本月已接委托
    map<string, int> sold;                // 本月已售货品
    int marketCash = 400;                 // 坊市收购资金
    int seq = 0;                          // 法器编号序列
};

// 游历遇事的上下文（对应 JS 挂在 pending 上的 adventure 字段）
struct AdvContext {
    string kind;        // beast / bandit / justice / backhill / heritage / caravan
    int seq = 0;
    int stage = 0;      // 对手境界
    int fee = 0;        // 胜后赏金
    int grade = 1;      // 法器品阶
    string book;        // 可获得的功法 id，空表示无
    string npcId;
    string style;       // 对手战斗风格
};

struct Pending {
    string type;                    // sectInvitation / valley / adventure
    string title;
    string subtitle;
    string text;
    vector<ChoiceOption> options;
    string npcId;
    optional<AdvContext> adventure; // type == "adventure" 时有效
};

// 结算
struct Ending {
    string cause;
    int year = 0;
    int age = 0;
    string realm;
    int friends = 0;
    int score = 0;
    int legacyRewardPoints = 0;
    vector<string> legacyReasons;
    bool legacyEligible = false;
};

// 存档根对象（对应 JS 的 s）
// 兽潮：全图起效的大灾。妖潮自险地涌出，波及所有地点，
// 低境界修士首当其冲 —— NPC 会真的死。
// 对玩家是成长动力：迎战有厚赏，躲着则错过机缘。
struct TideParticipant {
    string actorId, name, sect, combatProfileId;
    int entryStage = 0, score = 0, rank = 0;
    bool elite = false;
};
struct TideMarketStock {
    string kind, id;
    int stock = 0;
};
struct BeastTide {
    bool active = false;
    int startYear = -1;
    int monthsLeft = 0;       // 还剩几个月
    int severity = 1;         // 1~3，越高越凶
    int wavesFought = 0;      // 玩家迎战次数
    int wavesWon = 0;
    int npcDeaths = 0;        // 本次兽潮死了多少修士
    vector<string> fallen;    // 陨落者的名字（给玩家看）
    bool warned = false;      // 是否已播报「妖潮将起」
    int rulesVersion = 0;     // 0 表示旧版月度兽潮；迁移时只初始化一次
    string eventId;
    int startDay = -1, endDay = -1, durationDays = 60, lastProcessedDay = -1;
    int playerScore = 0, playerEntryStage = 0, playerRank = 0;
    bool playerJoined = false, settled = false, rewardPaid = false;
    int rewardStones = 0, totalScore = 0, marketExpiresDay = -1;
    vector<TideParticipant> participants;
    vector<TideMarketStock> marketStock;
};

struct DungeonReward {
    string id, kind, name, desc, spell, relic, manual;
    int amount = 0, grade = 1;
};
struct DungeonSite {
    string id, name, type, location;
    int openedYear = 0, expiresYear = 0, stage = 0;
    bool revealed = true, cleared = false;
    string targetKind, targetId, targetName;
    int targetGrade = 1;
    vector<DungeonReward> bossRewards;
};
struct DungeonNode {
    string id, type, name, text;
    int row = 0, lane = 0;
    vector<string> next;
    bool visited = false;
};
struct DungeonRun {
    string siteId, siteName, type, location, phase = "route", current;
    int stage = 0, health = 100, row = -1, seq = 0, elapsedDays = 0;
    vector<DungeonNode> nodes;
    vector<Card> deck;
    vector<string> relics;
    vector<DungeonReward> rewards;
    vector<DungeonReward> bossRewards;
    optional<DungeonReward> bossRewardTaken;
    bool bossRewardClaimed = false;
    map<string, int> herbs, materials, potions;
    int stones = 0, cores = 0, insight = 0;
    bool cleared = false;
    string targetKind, targetId, targetName;
    int targetGrade = 1;
    bool targetGranted = false;
};
struct DungeonWorld {
    bool initialized = false;
    int lastSpawnYear = -1, seq = 0;
    int intelMonth = -1, intelPurchases = 0;
    vector<DungeonSite> sites;
    optional<DungeonRun> run;
};
struct AutoCheckpoint {
    bool enabled = false;
    int days = 0, lastAutoYear = 0, restores = 0, slot = 4;
    string source = "auto", snapshot, lastReason;
};

struct Game {
    int version = VERSION;
    uint32_t rng = 20261004;
    uint32_t seed = 20261004;
    int year = 0;
    int eventSeq = 0;
    int npcSeq = 0;
    bool ended = false;
    string lifeId;
    string plan = "cultivate";
    map<string, int> flags;
    vector<LogEntry> logs;
    vector<Npc> npcs;
    World world;
    Player player;
    Clock clock;
    Economy economy;
    Bonds bonds;
    Adventure adventure;
    BeastTide tide;
    DungeonWorld dungeons;
    AutoCheckpoint checkpoint;
    optional<Job> job;
    optional<Pending> pending;
    optional<Ending> ending;
};

// ============================================================
// 人物生成与判定
// ============================================================
// 与 JS 版 buildCharacter 对应：非法入参抛 std::invalid_argument。
// seed = 0 时用固定的基准资质/悟性（自测用，保证可复现）；
// seed != 0 时按种子掷出初始资质与悟性 —— 每个人生来灵根不同。
Build buildCharacter(const vector<string>& traits,
                     const map<string, int>& allocation,
                     int budget = BASE_POINTS, unsigned seed = 0);

// ---- 资质 / 悟性的上下限 ----
// 上限 200：丹药、机缘、天赋都能往上推，但到 200 封顶。
// 下限是「不能比凡人更差」的地板。
inline constexpr int APT_MIN = 20;
inline constexpr int APT_MAX = 200;
inline constexpr int INSIGHT_MIN = 5;
inline constexpr int INSIGHT_MAX = 200;
// 初始掷出的上限：开局不能直接顶到 200
inline constexpr int APT_ROLL_MAX = 120;
inline constexpr int INSIGHT_ROLL_MAX = 60;
// 初始掷出的下限
inline constexpr int APT_ROLL_MIN = 60;
inline constexpr int INSIGHT_ROLL_MIN = 15;

// 按种子掷出的初始资质/悟性（界面上要展示，所以单独暴露）
struct InnateRoll {
    int apt = 90;
    int insight = 37;
};
InnateRoll rollInnate(unsigned seed);



bool hasTalent(const Player& p, const string& id);
double legacyEfficiency(const Player& p);

// ============================================================
// 随机数
// ============================================================
// JS 版把 rng 状态放在 s.rng 上，用 random(s) 推进。
// C++ 里直接内联这两个函数，保证「同一份状态、同一串序列」。
inline double randomOf(Game& g) {
    g.rng = static_cast<uint32_t>(g.rng * 1664525u + 1013904223u);
    return static_cast<double>(g.rng) / 4294967296.0;
}
inline int randIntOf(Game& g, int a, int b) {
    return a + static_cast<int>(randomOf(g) * static_cast<double>(b - a + 1));
}

// 从数组里均匀取一个元素（对应 JS 的 pick(s, x)）。
// 放在这里是因为它需要 Game 的随机数状态。
template <typename T>
const T& pickOf(Game& g, const vector<T>& items) {
    return items[static_cast<size_t>(randomOf(g) *
                                     static_cast<double>(items.size()))];
}
// 指针数组的重载，避免 vector<T*> 被推导成 T
template <typename T>
T* pickOf(Game& g, vector<T*>& items) {
    return items[static_cast<size_t>(randomOf(g) *
                                     static_cast<double>(items.size()))];
}

} // namespace nz
