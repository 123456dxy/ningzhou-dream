// 宁州一梦 · C++ 版 — 洞府道法实现（对应 JS 版 arts.mjs）
#include "core/arts.h"
#include "core/economy.h"

#include <algorithm>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

#include "core/battle.h"
#include "core/sim.h"
#include "core/source_limits.h"
#include "core/talents.h"
#include "data/reference_tables.h"

namespace nz {

const char* const ART_DAOS[12] = {"金", "木", "水", "火", "土", "神",
                                  "体", "剑", "气", "阵", "丹", "器"};
const char* const ART_LEVELS[6] = {"未入门", "初窥门径", "略有小成",
                                   "融会贯通", "道之真境", "大道已成"};
const int ART_THRESHOLDS[6] = {0, 30, 100, 300, 900, 2000};

int daoIndex(const string& dao) {
    for (int i = 0; i < 12; ++i)
        if (dao == ART_DAOS[i]) return i;
    return -1;
}

int artLevel(int exp) {
    // JS: ART_THRESHOLDS.filter(n=>exp>=n).length-1
    int n = 0;
    for (int i = 0; i < 6; ++i)
        if (exp >= ART_THRESHOLDS[i]) n = i;
    return n;
}

bool artHas(const Arts& a, const string& dao, int level) {
    const int idx = daoIndex(dao);
    if (idx < 0) return false;
    const string key = std::to_string(idx) + "-" + std::to_string(level);
    return std::find(a.nodes.begin(), a.nodes.end(), key) != a.nodes.end();
}

// ------------------------------------------------------------
// 道法节点表（对应 JS 的 ART_NODES）
// ------------------------------------------------------------
const vector<NodeDef>& artNodes() {
    static const vector<NodeDef> data = [] {
        // 与 JS 版 ART_NODES 逐字对应：每个大道 5 个节点
        static const char* d1[12] = {
            "金系伤害 +10%", "治疗 +15%", "气血上限 +12%", "火系伤害 +10%",
            "护盾 +20%", "灵力上限 +2", "气血上限 +15%", "剑系伤害 +12%",
            "每回合多回灵力 1", "首回合护盾12%", "采集多得 1 药材",
            "炼器成功率 +10%"};
        static const char* d2[12] = {
            "金系技能消耗 −1", "每回合自愈 1%", "水系技能消耗 −1",
            "灼烧伤害翻倍", "所受伤害 −8%", "神识技能伤害 +20%",
            "所受伤害 −8%；遁速 +6", "初始剑气 +2", "初始灵力 +3",
            "护盾当回合消散，减伤 +4%", "每年额外化毒 4", "材料利用率 +15%"};
        static const char* d3[12] = {
            "本系伤害额外 +8%", "修炼效率 +8%", "修炼效率 +8%",
            "本系伤害额外 +8%", "修炼效率 +8%", "突破成功率 +3%",
            "气血上限 +20%", "剑意伤害 +10%", "修炼效率 +10%",
            "灵力上限 +2", "耐药上限翻倍", "装备效果 +20%；炼器时间缩短30%"};
        static const char* d4[12] = {
            "每弃置一张未打出的手牌，获得气血上限2%的护盾；本回合护盾总上限30%",
            "每新增一层毒，回复气血上限0.3%的气血",
            "浪击每消耗一层水势，回灵系数+0.35；向下取整，每次最多回复2灵力",
            "每次施加的灼烧额外增加50%；灼烧天然叠加，再次施火刷新3回合",
            "实际获得护盾时，额外积蓄1层重势；弃牌凝盾也可触发",
            "抽牌、预谋、重抽及移除类神通的灵力消耗−1，最低0",
            "本回合每累计打出3张牌，回复气血上限2%的气血；遁速 +30",
            "本回合从第二张连续剑招起，每连一剑追加段伤害+25%，上限50%；非剑招打断连剑",
            "蓄灵神通的封存上限+2；保留灵力仍在下回合使用",
            "触发五行相生连击时，额外回复1灵力",
            "本回合每累计打出3张牌，回复气血上限1.5%的气血；高阶炉炼低阶丹不损耐久",
            "斗法开始时获得气血上限10%的护盾；护盾上限30%；自炼法器售值 +15%"};
        static const char* d5[12] = {
            "每弃置一张未打出的手牌，下一次直接攻击增伤4%；锐势上限30%，命中后消耗",
            "回合结束时，尚未失效的毒额外增加1层；毒最多99层，到期解除",
            "浪击保留消耗前50%的水势；保留层数向下取整",
            "引爆灼烧的附加伤害+50%；引爆后清除灼烧",
            "消耗重势的重击额外造成当前护盾40%的伤害，不消耗护盾",
            "立即抽牌类神通额外抽1张牌；手牌仍受上限约束",
            "敌方攻击被护盾格挡时，反震格挡伤害的15%",
            "本回合每连续打出第3张剑招，回复1灵力；非剑招打断连剑",
            "本回合出牌不超过2张，次回合额外获得1灵力",
            "相生连击额外抽1张牌；每回合最多额外抽2张",
            "治疗神通使自身每回合毒伤减少2点；毒伤降至0则解除中毒；每炉成功额外成丹1枚，最多4枚",
            "每回合开始时获得气血上限4%的护盾，首回合也生效；护盾上限30%；自炼法器售值 +25%，总加成上限50%"};
        static const char* names4[12] = {"弃牌凝盾", "毒息回春", "叠浪返灵", "薪火", "盾蓄重势", "凝神省力",
                                       "幻身调息", "连剑", "储灵", "相生聚灵", "控火调息", "精铸凝盾"};
        static const char* names5[12] = {"锐势", "蔓毒", "留水", "爆焰", "以盾为锋", "运筹",
                                       "反震", "剑返灵", "静息聚灵", "相生启智", "丹圣清浊", "巧作守御"};
        vector<NodeDef> out;
        for (int i = 0; i < 12; ++i) {
            const string dao = ART_DAOS[i];
            out.push_back({i, 1, 1, dao + "道入门", d1[i]});
            out.push_back({i, 2, 2, dao + (i == 6 ? "道·轻身" : "道精研"), d2[i]});
            out.push_back({i, 3, 4, dao + (i == 11 ? "道·增速" : "道贯通"), d3[i]});
            out.push_back({i, 4, 6, dao + "道·" + names4[i], d4[i]});
            out.push_back({i, 5, 8, dao + "道·" + names5[i], d5[i]});
        }
        return out;
    }();
    return data;
}

const NodeDef* findNode(const string& id) {
    for (const auto& n : artNodes())
        if (id == std::to_string(n.dao) + "-" + std::to_string(n.level))
            return &n;
    return nullptr;
}

// ------------------------------------------------------------
// 功法表（基础 14 部 + 拓展 26 部，对应 JS 的 ART_MANUALS）
// ------------------------------------------------------------
struct ManualEntry : CombatHooks, LifeHooks {
    string id;
    string name;
    string dao;
    int stage;
    int price;
    string effect;
    // 每层加成
    double cult = 0, attack = 0, sword = 0, heal = 0, hp = 0, shield = 0;
    double mana = 0, brew = 0, forge = 0, reduction = 0, charge = 0;
    double water = 0, poison = 0, soul = 0, drain = 0, fire = 0, steal = 0;
    double roundShield = 0, openingShield = 0, openingMana = 0, openingQi = 0;
    double quietHeal = 0, combo = 0, burnBonus = 0, qiGain = 0, thirdHeal = 0;
};

namespace {
struct HookSpec {
    double CombatHooks::* value;
    const char* name;
    const char* unit;
    bool percent = false;
};

const vector<HookSpec>& hookSpecs() {
    static const vector<HookSpec> specs = {
        {&CombatHooks::discardShield, "弃牌护盾系数", "%气血上限", true},
        {&CombatHooks::discardPower, "弃牌锐势系数", "%", true},
        {&CombatHooks::waterMana, "消耗水势回灵系数", ""},
        {&CombatHooks::waterKeep, "浪击留水比例", "%", true},
        {&CombatHooks::burnStack, "每次施加灼烧额外增加", "%", true},
        {&CombatHooks::burnExplosion, "引爆灼烧增伤", "%", true},
        {&CombatHooks::poisonHeal, "每新增毒层治疗", "%气血上限", true},
        {&CombatHooks::poisonGrowth, "回合末活跃毒增长系数", ""},
        {&CombatHooks::shieldCharge, "获得盾时额外重势系数", ""},
        {&CombatHooks::shieldStrike, "重击附加护盾伤害", "%", true},
        {&CombatHooks::swordCombo, "每多连一剑的追加段伤害", "%", true},
        {&CombatHooks::swordMana, "每连续三剑回灵系数", ""},
        {&CombatHooks::drawCost, "抽牌及移除类减费系数", ""},
        {&CombatHooks::drawBonus, "立即抽牌额外张数系数", ""},
        {&CombatHooks::carryBonus, "蓄灵封存上限系数", ""},
        {&CombatHooks::quietMana, "至多两次出牌的次回合聚灵系数", ""},
        {&CombatHooks::comboMana, "相生连击回灵系数", ""},
        {&CombatHooks::comboDraw, "相生连击额外抽牌系数", ""},
        {&CombatHooks::bodyCounter, "护盾格挡反震伤害", "%", true},
        {&CombatHooks::poisonCleanse, "治疗时清除自身毒伤系数", ""},
    };
    return specs;
}

string manualTriggerDesc(const ManualEntry& man) {
    string text;
    for (const auto& spec : hookSpecs()) {
        const double v = man.*(spec.value);
        if (v <= 0) continue;
        std::ostringstream number;
        number << std::setprecision(4) << (spec.percent ? v * 100 : v);
        text += "；每层" + string(spec.name) + "+" + number.str() + spec.unit;
    }
    const auto life = [&](double value, const char* label, bool percent) {
        if (value <= 0) return;
        std::ostringstream number;
        number << std::setprecision(4) << (percent ? value * 100 : value);
        text += "；每层" + string(label) + "+" + number.str() + (percent ? "%" : "");
    };
    life(man.brewYield, "额外成丹系数", false);
    life(man.forgeSpeed, "炼器时间缩短", true);
    life(man.craftedSale, "自炼法器售值", true);
    life(man.escapeSpeed, "遁速", false);
    if (!text.empty())
        text += "（触发系数逐层累加；主修全额、辅修减半，整数收益向下取整）";
    return text;
}
} // namespace

const vector<ManualEntry>& artManuals() {
    static const vector<ManualEntry> data = [] {
        vector<ManualEntry> v;
        auto add = [&](const char* id, const char* name, const char* dao,
                       int stage, int price, const char* effect) {
            ManualEntry m;
            m.id = id; m.name = name; m.dao = dao;
            m.stage = stage; m.price = price; m.effect = effect;
            v.push_back(m);
            return &v.back();
        };
        // ---- 基础 14 部（arts.mjs 中显式写出的）----
        add("tuna", "吐纳功", "气", 0, 0, "每层修炼 +2%")->cult = 0.02;
        add("bull", "蛮牛护体功", "体", 0, 70, "每层斗法气血 +5%")->hp = 0.05;
        add("wood", "青木诀", "木", 0, 65, "每层治疗 +4%，修炼 +1%")->heal = 0.04;
        v.back().cult = 0.01;
        add("sword", "剑典·人道篇", "剑", 0, 90, "每层剑系伤害 +4%")->sword = 0.04;
        add("water", "星河真法", "水", 3, 150, "每层修炼 +3%，护盾 +3%")->cult = 0.03;
        v.back().shield = 0.03;
        add("fire", "焚心诀", "火", 3, 150, "每层伤害 +3%")->attack = 0.03;
        add("earth", "厚土诀", "土", 0, 70, "每层护盾 +5%")->shield = 0.05;
        add("gold", "金虹诀", "金", 3, 160, "每层伤害 +2%，修炼 +1%")->attack = 0.02;
        v.back().cult = 0.01;
        add("longlife", "长生诀", "木", 3, 220, "每层修炼 +3%，治疗 +3%")->cult = 0.03;
        v.back().heal = 0.03;
        add("zhou", "大周天吐纳", "气", 6, 400, "每层修炼 +4%")->cult = 0.04;
        add("mind", "养魂诀", "神", 0, 80, "每层灵力上限 +1")->mana = 1;
        add("array", "两仪阵经", "阵", 3, 180, "每层护盾 +4%，灵力上限 +1")->shield = 0.04;
        v.back().mana = 1;
        add("pill", "丹火诀", "丹", 0, 70, "每层成丹率 +2%")->brew = 0.02;
        add("forge", "百炼真解", "器", 0, 70, "每层炼器率 +2%")->forge = 0.02;

        // ---- 拓展 26 部（由 build-data.mjs 导出，字段用 mods 字符串承载）----
        for (const auto& bm : data::ManualDef_TABLE()) {
            ManualEntry m;
            m.id = bm.id; m.name = bm.name; m.dao = bm.dao;
            m.stage = bm.stage; m.price = bm.price; m.effect = bm.effect;
            // mods 形如 "mana=0.4;charge=0.12"
            size_t pos = 0;
            while (pos < bm.mods.size()) {
                size_t semi = bm.mods.find(';', pos);
                const string kv = bm.mods.substr(
                    pos, semi == string::npos ? string::npos : semi - pos);
                const size_t eq = kv.find('=');
                if (eq != string::npos) {
                    const string k = kv.substr(0, eq);
                    const double val = std::stod(kv.substr(eq + 1));
                    if (k == "cult") m.cult = val;
                    else if (k == "attack") m.attack = val;
                    else if (k == "sword") m.sword = val;
                    else if (k == "heal") m.heal = val;
                    else if (k == "hp") m.hp = val;
                    else if (k == "shield") m.shield = val;
                    else if (k == "mana") m.mana = val;
                    else if (k == "brew") m.brew = val;
                    else if (k == "forge") m.forge = val;
                    else if (k == "reduction") m.reduction = val;
                    else if (k == "charge") m.charge = val;
                    else if (k == "water") m.water = val;
                    else if (k == "poison") m.poison = val;
                    else if (k == "soul") m.soul = val;
                    else if (k == "drain") m.drain = val;
                    else if (k == "fire") m.fire = val;
                    else if (k == "steal") m.steal = val;
                    else if (k == "roundShield") m.roundShield = val;
                    else if (k == "openingShield") m.openingShield = val;
                    else if (k == "openingMana") m.openingMana = val;
                    else if (k == "openingQi") m.openingQi = val;
                    else if (k == "quietHeal") m.quietHeal = val;
                    else if (k == "combo") m.combo = val;
                    else if (k == "burnBonus") m.burnBonus = val;
                    else if (k == "qiGain") m.qiGain = val;
                    else if (k == "thirdHeal") m.thirdHeal = val;
                }
                if (semi == string::npos) break;
                pos = semi + 1;
            }
            v.push_back(m);
        }

        // 保留数据表的前三层基础数值，在本地补充可组合的战斗触发。
        auto hook = [&](const char* id, double CombatHooks::* key, double value) {
            for (auto& man : v)
                if (man.id == id) { man.*key = value; break; }
        };
        hook("gold", &CombatHooks::discardShield, 0.006);
        hook("gold", &CombatHooks::discardPower, 0.012);
        hook("bladecare", &CombatHooks::discardPower, 0.02);
        hook("heavy", &CombatHooks::shieldStrike, 0.035);
        hook("goldenrain", &CombatHooks::discardShield, 0.008);
        hook("goldenrain", &CombatHooks::swordCombo, 0.035);
        hook("wood", &CombatHooks::poisonHeal, 0.001);
        hook("longlife", &CombatHooks::poisonHeal, 0.002);
        hook("venom", &CombatHooks::poisonGrowth, 0.2);
        hook("turtle", &CombatHooks::poisonHeal, 0.001);
        hook("evergreen", &CombatHooks::poisonGrowth, 0.2);
        hook("evergreen", &CombatHooks::poisonHeal, 0.0015);
        hook("water", &CombatHooks::waterKeep, 0.04);
        hook("rainform", &CombatHooks::waterMana, 0.04);
        hook("wave", &CombatHooks::waterMana, 0.05);
        hook("ocean", &CombatHooks::waterKeep, 0.04);
        hook("ocean", &CombatHooks::waterMana, 0.05);
        hook("deepwater", &CombatHooks::waterKeep, 0.03);
        hook("deepwater", &CombatHooks::swordCombo, 0.03);
        hook("fire", &CombatHooks::burnStack, 0.08);
        hook("playfire", &CombatHooks::burnStack, 0.1);
        hook("flame", &CombatHooks::burnExplosion, 0.04);
        hook("sunwheel", &CombatHooks::burnExplosion, 0.06);
        hook("sunwheel", &CombatHooks::burnStack, 0.06);
        hook("kindle", &CombatHooks::comboMana, 0.2);
        hook("earth", &CombatHooks::shieldCharge, 0.2);
        hook("mountain", &CombatHooks::shieldCharge, 0.2);
        hook("jadecloud", &CombatHooks::shieldStrike, 0.04);
        hook("gravity", &CombatHooks::shieldCharge, 0.2);
        hook("gravity", &CombatHooks::shieldStrike, 0.03);
        hook("sword", &CombatHooks::swordCombo, 0.035);
        hook("hidden", &CombatHooks::swordMana, 0.2);
        hook("human", &CombatHooks::swordCombo, 0.035);
        hook("swordcraft", &CombatHooks::swordCombo, 0.04);
        hook("mind", &CombatHooks::drawCost, 0.2);
        hook("visual", &CombatHooks::drawBonus, 0.2);
        hook("soulfire", &CombatHooks::drawBonus, 0.1);
        hook("tuna", &CombatHooks::quietMana, 0.2);
        hook("zhou", &CombatHooks::carryBonus, 0.4);
        hook("zhou", &CombatHooks::quietMana, 0.2);
        hook("cloud", &CombatHooks::quietMana, 0.2);
        hook("wheel", &CombatHooks::carryBonus, 0.2);
        hook("steal", &CombatHooks::carryBonus, 0.2);
        hook("array", &CombatHooks::comboMana, 0.2);
        hook("bull", &CombatHooks::bodyCounter, 0.025);
        hook("pill", &CombatHooks::poisonCleanse, 0.2);
        hook("forge", &CombatHooks::shieldStrike, 0.025);
        for (auto& man : v) {
            if (man.id == "pill") man.brewYield = 0.2;
            if (man.id == "forge") {
                man.forgeSpeed = 0.025;
                man.craftedSale = 0.02;
            }
            if (man.id == "cloud") man.escapeSpeed = 3;
            if (man.id == "rainform") man.escapeSpeed = 2;
        }
        for (auto& man : v)
            if (man.id == "fetal")
                man.effect = "主修每层修炼+2%；本回合每累计打出3张牌，每层回复1.2%气血（治疗比例上限12%，辅修战斗效果减半）";
        return v;
    }();
    return data;
}

const ManualEntry* findManual(const string& id) {
    for (const auto& m : artManuals())
        if (m.id == id) return &m;
    return nullptr;
}

// ------------------------------------------------------------
// 洞府加成（对应 JS 的 artMods）
// ------------------------------------------------------------
const ArtMods& artModsOf(const Arts& a) {
    // artMods 只依赖 Arts 本身，所以可以按内容缓存；
    // 这里为了正确性和可调试性，每次都重新计算（性能足够）。
    thread_local ArtMods m;
    m = ArtMods{};

    auto has = [&](const char* dao, int lv) { return artHas(a, dao, lv); };

    // 1) 功法加成：主修吃满，辅修一半；基础修炼与制造成功率仅主修。
    // 生活触发（数量、时间、售价、遁速）与战斗触发都遵守主满辅半。
    vector<string> wear;
    wear.push_back(a.main);
    for (const auto& x : a.aux) wear.push_back(x);

    for (const auto& id : wear) {
        const ManualEntry* man = findManual(id);
        if (!man) continue;
        auto it = a.manuals.find(id);
        const int lv = (it == a.manuals.end()) ? 0 : it->second;
        if (lv == 0) continue;
        const bool isMain = (id == a.main);
        const double mult = lv * (isMain ? 1.0 : 0.5);

        // JS: if(id===a.main || !['cult','brew','forge'].includes(k))
        // 也就是说辅修完全不提供修炼效率、成丹率、炼器率三项加成。
        if (isMain) m.cult += man->cult * mult;
        m.attack += man->attack * mult;
        m.sword += man->sword * mult;
        m.heal += man->heal * mult;
        m.hp += man->hp * mult;
        m.shield += man->shield * mult;
        m.mana += static_cast<int>(man->mana * mult);
        if (isMain) {
            m.brew += man->brew * mult;
            m.forge += man->forge * mult;
        }
        m.reduction += man->reduction * mult;
        m.charge += man->charge * mult;
        m.water += man->water * mult;
        m.poison += man->poison * mult;
        m.soul += man->soul * mult;
        m.drain += man->drain * mult;
        m.fire += man->fire * mult;
        m.steal += man->steal * mult;
        m.roundShield += man->roundShield * mult;
        m.openingShield += man->openingShield * mult;
        m.openingMana += man->openingMana * mult;
        m.openingQi += man->openingQi * mult;
        m.quietHeal += man->quietHeal * mult;
        m.combo += man->combo * mult;
        // 这三个之前漏了：弄焰诀/胎息经/剑术要诀等功法的效果恒为 0，
        // 而 battle.cpp 一直在读它们。
        m.burnBonus += man->burnBonus * mult;
        m.thirdHeal += man->thirdHeal * mult;
        m.qiGain += man->qiGain * mult;
        for (const auto& spec : hookSpecs())
            m.*(spec.value) += man->*(spec.value) * mult;
        m.brewYield += man->brewYield * mult;
        m.forgeSpeed += man->forgeSpeed * mult;
        m.craftedSale += man->craftedSale * mult;
        m.escapeSpeed += man->escapeSpeed * mult;
    }

    // 2) 真境与大成改变触发规则，不再给十二道相同的面板加成。
    for (const auto& id : a.nodes) {
        const NodeDef* n = findNode(id);
        if (!n) continue;
        if (n->level == 4) {
            switch (n->dao) {
                case 0: m.discardShield += 0.02; break;
                case 1: m.poisonHeal += 0.003; break;
                case 2: m.waterMana += 0.35; break;
                case 3: m.burnStack += 0.5; break;
                case 4: m.shieldCharge += 1; break;
                case 5: m.drawCost += 1; break;
                case 6: m.thirdHeal += 0.02; m.escapeSpeed += 30; break;
                case 7: m.swordCombo += 0.25; break;
                case 8: m.carryBonus += 2; break;
                case 9: m.comboMana += 1; break;
                case 10: m.thirdHeal += 0.015; break;
                case 11: m.openingShield += 0.1; m.craftedSale += 0.15; break;
            }
        } else if (n->level == 5) {
            switch (n->dao) {
                case 0: m.discardPower += 0.04; break;
                case 1: m.poisonGrowth += 1; break;
                case 2: m.waterKeep += 0.5; break;
                case 3: m.burnExplosion += 0.5; break;
                case 4: m.shieldStrike += 0.4; break;
                case 5: m.drawBonus += 1; break;
                case 6: m.bodyCounter += 0.15; break;
                case 7: m.swordMana += 1; break;
                case 8: m.quietMana += 1; break;
                case 9: m.comboDraw += 1; break;
                case 10: m.poisonCleanse += 2; m.brewYield += 1; break;
                case 11: m.roundShield += 0.04; m.craftedSale += 0.25; break;
            }
        }
    }

    // 3) 各道贯通效果
    for (const char* d : {"木", "水", "土"})
        if (has(d, 3)) m.cult += 0.08;
    if (has("气", 3)) m.cult += 0.1;
    if (has("金", 3)) m.elements[daoIndex("金")] += 0.08;
    if (has("火", 3)) m.elements[daoIndex("火")] += 0.08;
    if (has("水", 1)) m.hp += 0.12;
    if (has("体", 1)) m.hp += 0.15;
    if (has("体", 3)) m.hp += 0.2;
    if (has("剑", 3)) m.sword += 0.1;
    if (has("土", 1)) m.shield += 0.2;
    if (has("神", 1)) m.mana += 2;
    if (has("阵", 3)) m.mana += 2;
    if (has("阵", 2)) m.reduction += 0.04;
    if (has("体", 2)) { m.reduction += 0.08; m.escapeSpeed += 6; }
    if (has("器", 3)) m.forgeSpeed += 0.3;
    if (has("土", 2)) m.reduction += 0.08;

    m.mana += a.mind / 4;
    m.hp += a.hpBonus / 100.0;

    // 4) 法器
    const double gearMul = has("器", 3) ? 1.2 : 1.0;
    for (const auto& [slot, gid] : a.equipped) {
        const Gear* gr = nullptr;
        for (const auto& g : a.gear)
            if (g.id == gid) { gr = &g; break; }
        if (!gr) continue;
        const double b = gr->bonus * gearMul * (gr->inscribed ? 1.12 : 1.0);
        if (gr->slot == "weapon") m.attack += b;
        if (gr->slot == "armor") m.hp += b;
        if (gr->slot == "accessory")
            m.mana += jsRound(b * 10);
        const int ei = daoIndex(gr->element);
        if (ei >= 0) m.elements[ei] += gr->bonus * 0.5;
        if (gr->polarity == "天阳") m.attack += gr->bonus * 0.1;
        else m.shield += gr->bonus * 0.2;
    }

    // 5) 上限
    m.attack = std::min(2.0, m.attack);
    m.hp = std::min(2.5, m.hp);
    m.mana = std::min(30, m.mana);
    m.heal = std::min(1.5, m.heal);
    m.sword = std::min(1.8, m.sword);
    m.fire = std::min(1.6, m.fire);
    m.brewYield = clamp(m.brewYield, 0.0, 3.0);
    m.forgeSpeed = clamp(m.forgeSpeed, 0.0, 0.6);
    m.craftedSale = clamp(m.craftedSale, 0.0, 0.5);
    m.escapeSpeed = clamp(m.escapeSpeed, 0.0, 200.0);
    // 丹毒超过 20 会降低修炼效率
    m.cult *= std::max(0.5, 1.0 - std::max(0, a.poison - 20) / 160.0);
    return m;
}

int artEscapeSpeed(const Player& p) {
    const ArtMods mods = talentMods(p);
    return 20 + clampInt(p.stage, 0, 12) * 8 +
           clampInt(p.arts.speed, 0, 200) + static_cast<int>(std::floor(mods.escapeSpeed));
}

Arts artNpcLoadout(const Npc& npc) {
    Arts a;
    a.gear = npc.gear;
    a.equipped = npc.equipped;
    if (npc.beast) { a.main.clear(); return a; }
    const int stage = clampInt(npc.stage, 0, 12);
    const int major = stage / 3;
    const bool sect = !npc.sect.empty();
    string dao = daoIndex(npc.dao) >= 0 ? npc.dao : "剑";
    if (npc.sect == "jinhong") dao = "金";
    else if (npc.sect == "xinghe") dao = "水";
    else if (npc.sect == "zhushan") dao = "木";
    else if (npc.sect == "lihuo") dao = "火";
    else if (npc.sect == "huachen") dao = "土";
    a.focus = dao;
    const int level = std::min(5, (sect ? 2 : 1) + major);
    const auto equipManual = [&](const string& id, bool main) {
        const ManualEntry* man = findManual(id);
        if (!man || man->stage > stage || a.manuals.count(id)) return false;
        a.manuals[id] = main ? level : std::max(1, level - 1);
        if (main) a.main = id;
        else if (a.aux.size() < 4) a.aux.push_back(id);
        return true;
    };
    vector<string> manuals;
    vector<string> spells;
    if (dao == "金") { manuals = {"gold", "bladecare", "hidden", "heavy"}; spells = {"gold", "goldslash", "metalSeal", "refresh"}; }
    else if (dao == "木") { manuals = {"longlife", "venom", "wood", "turtle"}; spells = {"corrode", "wood", "woodwall", "poisonBurst"}; }
    else if (dao == "水") { manuals = {"water", "rainform", "wave", "swordcraft"}; spells = {"waterSeal", "wavehit", "water", "softwater", "sword"}; }
    else if (dao == "火") { manuals = {"fire", "flame", "playfire", "pill"}; spells = {"fire", "fireburst", "spitfire", "fireSeal", "sun"}; }
    else if (dao == "土") { manuals = {"earth", "mountain", "gravity", "jadecloud"}; spells = {"mountainSeal", "collapse", "earth", "aegis"}; }
    else if (dao == "神") { manuals = {"mind", "visual", "soulfire", "cloud"}; spells = {"soulspark", "soulshock", "skim", "mind", "purge"}; }
    else if (dao == "体") { manuals = {"bull", "fetal", "steal", "cloud"}; spells = {"body", "collapse", "cutmana", "bloodfuel"}; }
    else if (dao == "阵") { manuals = {"array", "cloud", "mind", "earth"}; spells = {"woodSeal", "fireSeal", "mountainSeal", "array", "scheme"}; }
    else if (dao == "丹") { manuals = {"pill", "wood", "mind", "flame"}; spells = {"wood", "fire", "corrode", "skim"}; }
    else if (dao == "器") { manuals = {"forge", "earth", "bull", "heavy"}; spells = {"earth", "mountainSeal", "gold", "collapse"}; }
    else if (dao == "气") { manuals = {"zhou", "cloud", "tuna", "wheel"}; spells = {"gather", "hoard", "skim", "qiFlow"}; }
    else { manuals = {"sword", "hidden", "swordcraft", "human"}; spells = {"sword", "swordFocus", "goldcover", "humanSword", "rain"}; }
    // 初期散修只具吐纳与少量本命神通；高境界逐步取得完整传承。
    a.main.clear();
    if (!sect && stage < 3) equipManual("tuna", true);
    else for (const auto& id : manuals) if (equipManual(id, true)) break;
    if (a.main.empty()) equipManual("tuna", true);
    const int auxLimit = sect ? std::min(4, 1 + major) : (stage < 3 ? 0 : std::min(3, major));
    for (const auto& id : manuals) {
        if (static_cast<int>(a.aux.size()) >= auxLimit) break;
        equipManual(id, false);
    }
    const int nodeLevel = sect ? std::min(5, 2 + major) : (stage < 3 ? 0 : std::min(5, 1 + major));
    const int di = daoIndex(dao);
    a.exp[dao] = ART_THRESHOLDS[nodeLevel];
    for (int l = 1; l <= nodeLevel; ++l)
        a.nodes.push_back(std::to_string(di) + "-" + std::to_string(l));
    Player slotOwner; slotOwner.stage = stage;
    const int spellLimit = sect ? battleSpellSlots(slotOwner) : std::min(battleSpellSlots(slotOwner), 2 + major);
    const auto addSpell = [&](const string& id) {
        const SpellDef* sp = battleSpell(id);
        if (!sp || sp->stage > stage || id == "strike" || id == "defend" ||
            static_cast<int>(a.spells.size()) >= spellLimit ||
            std::find(a.spells.begin(), a.spells.end(), id) != a.spells.end()) return;
        a.spells.push_back(id);
    };
    for (const auto& id : spells) addSpell(id);
    for (const auto& id : npc.spells) addSpell(id);
    a.knownSpells = a.spells;
    return a;
}

// ------------------------------------------------------------
// 初始化洞府（对应 JS 的 ensureArts）
// ------------------------------------------------------------
void ensureArts(Game& g) {
    Player& p = g.player;
    Arts& a = p.arts;

    // 洞府已经初始化过：只做「补齐」，不要整块重置，
    // 否则读档会把玩家辛苦攒的经验、功法、法器全部抹掉。
    const bool initialized = !a.exp.empty() || !a.manuals.empty() ||
                             !a.gear.empty() || !a.herbs.empty() ||
                             !a.potions.empty() || !a.materials.empty() ||
                             !a.nodes.empty() || a.battle.has_value();
    if (initialized) {
        for (const char* dao : ART_DAOS) a.exp.try_emplace(dao, 0);
        if (daoIndex(a.focus) < 0) a.focus = "气";
        if (!a.manuals.count(a.main) || a.manuals[a.main] <= 0) {
            a.manuals.try_emplace("tuna", 1);
            a.main = "tuna";
        }
        // 灵击和守势固定自带，不占配装槽。保留玩家主动卸空的选择，
        // 仅去除旧档遗留的固定牌和重复项。
        vector<string> unique;
        for (const auto& id : a.spells)
            if (id != "strike" && id != "defend" &&
                std::find(unique.begin(), unique.end(), id) == unique.end())
                unique.push_back(id);
        a.spells = std::move(unique);
        for (const auto& id : a.spells)
            if (std::find(a.knownSpells.begin(), a.knownSpells.end(), id) == a.knownSpells.end())
                a.knownSpells.push_back(id);
        for (const char* id : {"skim", "refresh", "purge"})
            if (std::find(a.knownSpells.begin(), a.knownSpells.end(), id) == a.knownSpells.end())
                a.knownSpells.push_back(id);
        return;
    }

    a.focus = "气";
    for (int i = 0; i < 12; ++i) a.exp[ART_DAOS[i]] = 0;
    a.points = 0;
    a.nodes.clear();
    a.manuals["tuna"] = 1;
    a.main = "tuna";
    a.aux.clear();
    a.spells = {"sword", "water", "wood", "skim"};
    a.knownSpells = {"sword", "water", "wood", "skim", "refresh", "purge"};
    a.herbs.clear();
    a.potions.clear();
    a.used.clear();
    a.poison = 0;
    a.furnace = "furnace-1";
    a.durability = 40;
    a.materials = {{"iron", 4}, {"jade", 3}, {"wood", 3}};
    a.gear.clear();
    a.equipped.clear();
    a.heart = 0;
    a.mind = 0;
    a.speed = 0;
    a.hpBonus = 0;
    a.breakBuff.clear();
    a.combatBuff = 0;
    a.repel = 0;
    a.progress = Arts::Progress{};
    a.progress.wealth = p.stones;
    a.quests.clear();
    a.visits.clear();

    // 初始药材：最多 30 份分摊到三种基础药材
    int left = std::min(p.herbs, 30);
    static const char* starters[3] = {"银月花", "流莹草", "恒心草"};
    for (int i = 0; i < 3; ++i) {
        const int count = left / (3 - static_cast<int>(a.herbs.size()));
        a.herbs[starters[i]] = count;
        left -= count;
    }
}

// ------------------------------------------------------------
// 大道经验（对应 JS 的 artGain）
// ------------------------------------------------------------
void artGain(Player& p, const string& dao, double amount) {
    Arts& a = p.arts;
    if (amount > 0 && daoIndex(dao) >= 0 && hasTalent(p, "blessing")) amount *= 1.20;
    const int before = artLevel(a.exp[dao]);
    a.exp[dao] = std::min(10000, a.exp[dao] + jsRound(amount));
    const int after = artLevel(a.exp[dao]);
    for (int i = before + 1; i <= after; ++i)
        a.points += (i <= 2) ? 1 : 2;
}

// ------------------------------------------------------------
// 药材同步（对应 JS 的 artSyncHerbs）
// ------------------------------------------------------------
void artSyncHerbs(Player& p) {
    Arts& a = p.arts;
    int typed = 0;
    for (const auto& [k, v] : a.herbs) typed += v;
    int excess = typed - p.herbs;
    if (excess <= 0) return;
    for (auto& [name, count] : a.herbs) {
        const int take = std::min(excess, count);
        count -= take;
        excess -= take;
        if (excess <= 0) break;
    }
}

// ------------------------------------------------------------
// 品阶上限（对应 JS 的 artTier）
// ------------------------------------------------------------
int artTier(const Player& p) {
    // JS: min(6, 2 + floor(stage/3) + (stage%3===2 ? 1 : 0))
    return std::min(6, 2 + p.stage / 3 + ((p.stage % 3 == 2) ? 1 : 0));
}

// ------------------------------------------------------------
// 炼丹（对应 artBrewInfo）
// ------------------------------------------------------------
const data::RecipeDef* findRecipe(const string& id) {
    for (const auto& r : data::RecipeDef_TABLE())
        if (r.id == id) return &r;
    return nullptr;
}

const data::FurnaceDef* findFurnace(const string& id) {
    for (const auto& f : data::FurnaceDef_TABLE())
        if (f.id == id) return &f;
    return nullptr;
}

// 解析 ingredients 字段："药材id:数量;药材id:数量"
vector<std::pair<string, int>> parseIngredients(const string& s) {
    vector<std::pair<string, int>> out;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t semi = s.find(';', pos);
        const string item = s.substr(
            pos, semi == string::npos ? string::npos : semi - pos);
        const size_t colon = item.find(':');
        if (colon != string::npos)
            out.emplace_back(item.substr(0, colon),
                             std::stoi(item.substr(colon + 1)));
        if (semi == string::npos) break;
        pos = semi + 1;
    }
    return out;
}

// 药材 id -> 名称 与 名称 -> 定义
const data::HerbDef* findHerbById(const string& id) {
    for (const auto& h : data::HerbDef_TABLE())
        if (h.id == id) return &h;
    return nullptr;
}

// 丹方的 ingredients 字段里写的是药材「名称」（如 "银月花:1;流莹草:1"），
// 不是 id。所以解析出来的键要用这个函数去查，否则永远查不到，
// 材料就白扣不了。这个坑之前一直存在，只是没人发现。
const data::HerbDef* findHerbByName(const string& name) {
    for (const auto& h : data::HerbDef_TABLE())
        if (h.name == name) return &h;
    return nullptr;
}

BrewInfo artBrewInfo(const Player& p, const string& recipeId) {
    BrewInfo info;
    const data::RecipeDef* r = findRecipe(recipeId);
    if (!r) return info;
    const data::FurnaceDef* f = findFurnace(p.arts.furnace);
    info.valid = true;
    info.recipeId = r->id;
    info.recipeName = r->name;
    info.grade = r->grade;
    info.furnaceGrade = f ? f->grade : 1;
    info.cost = 8 + r->grade * 6;
    const ArtMods mods = talentMods(p);
    info.output = clampInt(1 + static_cast<int>(std::floor(mods.brewYield + 1e-9)), 1, 4);
    // JS: artCap(.46 + sqrt(alchemy)*.045 + brew + (lihuo?.08:0) - grade*.035, .1, .95)
    info.chance = clamp(
        0.46 + jsSqrt(static_cast<double>(p.alchemy)) * 0.045 +
            mods.brew + (p.sect == "lihuo" ? 0.08 : 0.0) -
            r->grade * 0.035,
        0.1, 0.95);
    for (const auto& [herbName, n] : parseIngredients(r->ingredients))
        info.count += n;
    return info;
}

// ------------------------------------------------------------
// 炼器（对应 artForgeInfo）
// ------------------------------------------------------------
static const data::MaterialDef* findMaterial(const string& id) {
    for (const auto& m : data::MaterialDef_TABLE())
        if (m.id == id) return &m;
    return nullptr;
}

static constexpr int FORGE_QUANTITIES[3] = {4, 3, 3};

vector<ForgeMaterialCost> artForgeMaterials(const Player& p,
                                           const string& mainId,
                                           const string& auxId,
                                           const string& coreId) {
    const string ids[3] = {mainId, auxId, coreId};
    const string roles[3] = {"main", "aux", "core"};
    for (const auto& id : ids) if (!findMaterial(id)) return {};
    vector<ForgeMaterialCost> costs;
    for (int i = 0; i < 3; ++i) {
        auto cost = std::find_if(costs.begin(), costs.end(), [&](const auto& item) {
            return item.id == ids[i];
        });
        if (cost == costs.end()) {
            ForgeMaterialCost item;
            item.id = ids[i]; item.name = findMaterial(ids[i])->name;
            const auto owned = p.arts.materials.find(ids[i]);
            item.owned = owned == p.arts.materials.end() ? 0 : std::max(0, owned->second);
            costs.push_back(std::move(item)); cost = std::prev(costs.end());
        }
        cost->roles.push_back(roles[i]);
        cost->required += FORGE_QUANTITIES[i];
        cost->missing = std::max(0, cost->required - cost->owned);
    }
    return costs;
}

static vector<int> parseDims(const string& s) {
    vector<int> out;
    size_t pos = 0;
    while (pos < s.size()) {
        const size_t comma = s.find(',', pos);
        out.push_back(std::stoi(s.substr(
            pos, comma == string::npos ? string::npos : comma - pos)));
        if (comma == string::npos) break;
        pos = comma + 1;
    }
    return out;
}

ForgeInfo artForgeInfo(const Player& p, const string& mainId,
                       const string& auxId, const string& coreId,
                       const string& shapeId) {
    ForgeInfo info;
    const data::MaterialDef* mats[3] = {findMaterial(mainId),
                                        findMaterial(auxId),
                                        findMaterial(coreId)};
    const data::ShapeDef* form = nullptr;
    for (const auto& s : data::ShapeDef_TABLE())
        if (s.id == shapeId) form = &s;
    if (!mats[0] || !mats[1] || !mats[2] || !form) return info;

    vector<vector<int>> dimsList;
    for (int i = 0; i < 3; ++i) dimsList.push_back(parseDims(mats[i]->dimensions));

    // 五维加权求和后除以 10
    vector<int> dims(5, 0);
    for (int i = 0; i < 5; ++i) {
        double v = 0;
        for (int k = 0; k < 3; ++k) {
            const auto& d = dimsList[k];
            v += (i < static_cast<int>(d.size()) ? d[i] : 0) * FORGE_QUANTITIES[k];
        }
        dims[i] = jsRound(v / 10.0);
    }
    int total = 0;
    for (int d : dims) total += d;

    const int grade = std::min({mats[0]->grade, mats[1]->grade, mats[2]->grade});
    double util = std::min(1.0, total / 2200.0) *
                  (artHas(p.arts, "器", 2) ? 1.15 : 1.0);
    const int dmin = *std::min_element(dims.begin(), dims.end());
    const int dmax = std::max(1, *std::max_element(dims.begin(), dims.end()));
    const double balanced = 0.75 + 0.25 * dmin / dmax;
    util = std::min(1.15, util);

    info.valid = true;
    info.grade = grade;
    info.util = util;
    info.cost = 15 + grade * 18;
    // JS 用 toFixed(3) 截断到三位小数
    info.bonus = std::floor(grade * 0.07 * util * balanced * 1000.0 + 0.5) / 1000.0;
    info.chance = clamp(0.65 + artLevel(p.arts.exp.count("器") ? p.arts.exp.at("器") : 0) * 0.05 +
                            talentMods(p).forge +
                            (artHas(p.arts, "器", 1) ? 0.1 : 0.0) - grade * 0.05,
                        0.1, 0.95);
    info.shapeId = form->id;
    info.shapeName = form->name;
    info.shapeSlot = form->slot;
    info.mainName = mats[0]->name;
    info.mainElement = mats[0]->element;
    info.mainPolarity = mats[0]->polarity;
    return info;
}

// ------------------------------------------------------------
// 丹药效果（对应 artPillEffect / artCombatPill）
// ------------------------------------------------------------
CombatPill artCombatPill(const data::RecipeDef& r) {
    const string& t = r.sourceEffect;
    auto has = [&](const char* kw) { return t.find(kw) != string::npos; };
    if (has("护罩") || has("护盾"))
        return {"shield", 0.2, "下一场起手护盾 +20%气血"};
    if (has("减伤") || has("免疫") || has("抗性"))
        return {"reduction", 0.12, "下一场所受伤害 −12%"};
    if (has("剑气")) return {"qi", 2, "下一场起手剑气 +2"};
    if (has("抽") || has("神识"))
        return {"mana", 2, "下一场初始与上限灵力 +2"};
    if (has("遁速")) return {"speed", 10, "下一场撤出切磋损伤减轻"};
    return {"attack", 0.1, "下一场伤害 +10%（同类不叠加）"};
}

PillEffect artPillEffect(const Player& p, const data::RecipeDef& r) {
    // 各境界所需修为
    static const int needs[13] = {110, 190, 320, 900, 1600, 2800, 3600,
                                  5400, 8000, 18000, 30000, 46000, 0};
    PillEffect e;
    const int stage = clampInt(p.stage, 0, 12);

    if (r.kind == "xp") {
        e.value = hasTalent(p, "oddMeridian") ? 0 :
            std::min(jsRound(needs[stage] * 0.25), jsRound(jsSqrt(static_cast<double>(r.value)) * 4));
    } else if (r.kind == "dao") {
        e.value = jsRound(r.value / 10.0);
    } else if (r.kind == "body") {
        e.value = std::min(20, r.value);
    } else {
        e.value = r.value;
    }

    if (r.kind == "xp")
        e.text = hasTalent(p, "oddMeridian") ? "先天异脉：丹药无法提升修为" :
            "修为 +" + std::to_string(e.value) + "（不超过本小境25%）";
    else if (r.kind == "dao")
        e.text = r.dao + "道经验 +" + std::to_string(e.value);
    else if (r.kind == "body")
        e.text = "斗法气血倍率 +" + std::to_string(e.value) + "%";
    else if (r.kind == "heal")
        e.text = "恢复气血 " + std::to_string(std::min(100, e.value));
    else if (r.kind == "detox")
        e.text = "丹毒 −" + std::to_string(e.value);
    else if (r.kind == "points")
        e.text = "悟道点 +" + std::to_string(e.value);
    else if (r.kind == "apt") {
        const int cap = std::max(0, std::min({e.value, 10 - p.arts.aptPills, APT_MAX - p.apt}));
        e.value = cap;
        e.text = "资质 +" + std::to_string(cap) + "（本世资质丹累计上限10）";
    } else if (r.kind == "life")
        e.text = "寿元 +" + std::to_string(e.value);
    else if (r.kind == "insight")
        e.text = "悟性 +" + std::to_string(e.value);
    else if (r.kind == "heart")
        e.text = "心境 +" + std::to_string(e.value);
    else if (r.kind == "mind")
        e.text = "神识 +" + std::to_string(e.value);
    else if (r.kind == "speed")
        e.text = "遁速 +" + std::to_string(e.value);
    else if (r.kind == "breakthrough")
        e.text = "下一次对应大境界突破 +6%（不叠加）";
    else if (r.kind == "combat")
        e.text = artCombatPill(r).text;
    else if (r.kind == "lure")
        e.text = "即刻获得同阶妖兽材料 ×2";
    else if (r.kind == "repel")
        e.text = "两年内游历风险降低";
    else
        e.text = "斗法伤害 +10%";
    return e;
}

// ------------------------------------------------------------
// 成就表
// ------------------------------------------------------------
const vector<AchievementDef>& artAchievements() {
    static const vector<AchievementDef> data = [] {
        vector<AchievementDef> v;
        for (const auto& a : data::AchievementDef_TABLE())
            v.push_back({a.id, a.key, a.target, a.name, a.points});
        return v;
    }();
    return data;
}

int artProgress(const Player& p, const string& key) {
    const Arts& a = p.arts;
    if (key == "nodes") return static_cast<int>(a.nodes.size());
    if (key == "manuals") return static_cast<int>(a.manuals.size());
    if (key == "brew") return a.progress.brew;
    if (key == "forge") return a.progress.forge;
    if (key == "wins") return a.progress.wins;
    if (key == "quests") return a.progress.quests;
    if (key == "wealth") return a.progress.wealth;
    return 0;
}

// ------------------------------------------------------------
// 限时机缘（对应 artQuestError / artRewardQuest）
// ------------------------------------------------------------
static const data::QuestDef* findQuest(const string& id) {
    for (const auto& q : data::QuestDef_TABLE())
        if (q.id == id) return &q;
    return nullptr;
}

string artQuestError(const Game& g, const data::QuestDef* q) {
    if (!q) return "无此机缘。";
    const Player& p = g.player;
    const Arts& a = p.arts;
    if (a.quests.count(q->id)) return "这段因缘已经完成。";

    // 按「行动结束年份」判断是否在窗口内
    const int endYear = g.clock.days ? (g.clock.days + 7) / 360 + 1 : g.year + 1;
    if (endYear < q->start || endYear > q->end)
        return "不在机缘时间窗口内（按行动结束年份判断）。";
    if (p.stage < q->stage || p.stage > q->maxStage) return "境界不符合。";
    if (p.location != q->location) return "请先前往机缘地点。";
    if (!q->requiresQuest.empty() && !a.quests.count(q->requiresQuest))
        return "尚未完成前置事件。";
    if (q->id == "dan" && p.alchemy < 25) return "需要丹术 25。";
    if (p.stones < q->price) return "机缘需要更多资粮。";
    return "";
}

void artRewardQuest(Game& g, const data::QuestDef& q) {
    Player& p = g.player;
    Arts& a = p.arts;
    a.quests[q.id] = g.year;
    a.progress.quests++;

    // 各机缘的大道经验奖励
    struct Grant { const char* id; const char* dao; int amount; };
    static const vector<Grant> grants = {
        {"school", "气", 20},   {"star", "阵", 40},
        {"cave", "体", 60},     {"cave", "水", 60},
        {"dan", "丹", 80},      {"temple", "神", 50},
        {"swordwall", "剑", 100},{"snow", "丹", 60},
        {"again", "阵", 100},   {"traitor", "火", 100},
        {"demon", "气", 150},   {"tournament", "剑", 60},
    };
    for (const auto& gr : grants)
        if (q.id == gr.id) artGain(p, gr.dao, gr.amount);

    if (q.id == "school" || q.id == "snow") a.heart += 10;
    if (q.id == "star") p.stones += 50;
    if (q.id == "dan") p.alchemy = std::min(500, p.alchemy + 10);

    // 赠送功法抄本
    static const std::pair<const char*, const char*> manuals[6] = {
        {"cave", "bull"}, {"temple", "mind"}, {"swordwall", "sword"},
        {"again", "array"}, {"traitor", "fire"}, {"demon", "zhou"}};
    for (const auto& [qid, mid] : manuals) {
        if (q.id != qid) continue;
        if (std::find(a.bookList.begin(), a.bookList.end(), mid) ==
            a.bookList.end())
            a.bookList.push_back(mid);
    }

    if (q.id == "pair") a.points += 2;

    // 赠送丹药
    static const std::pair<const char*, const char*> pills[2] = {
        {"tournament", "筑基丹"}, {"pair", "聚顶丹"}};
    for (const auto& [qid, pname] : pills) {
        if (q.id != qid) continue;
        for (const auto& r : data::RecipeDef_TABLE())
            if (r.name == pname) a.potions[r.id] = a.potions[r.id] + 1;
    }

    logEvent(g, "self", "完成「" + q.name + "」：" + q.reward + "。", true);
}

// ------------------------------------------------------------
// 年度洞府行动
// ------------------------------------------------------------
// 统一的功法查询（基础表 + 拓展表）
struct ManualEntryView {
    string id, name, dao, effect;
    int stage = 0, price = 0;
    string coefficients;
};

static bool lookupManual(const string& id, ManualEntryView& out) {
    for (const auto& m : data::BaseManualDef_TABLE()) {
        if (m.id != id) continue;
        out = {m.id, m.name, m.dao, m.effect, m.stage, m.price,
               m.coefficients};
        return true;
    }
    for (const auto& m : data::ManualDef_TABLE()) {
        if (m.id != id) continue;
        out = {m.id, m.name, m.dao, m.effect, m.stage, m.price, m.mods};
        return true;
    }
    return false;
}

// 公开版：给界面用（lookupManual 是本文件的内部实现）
bool findManualInfo(const string& id, ManualInfo& out) {
    ManualEntryView v;
    if (!lookupManual(id, v)) return false;
    out.found = true;
    out.id = v.id;
    out.name = v.name;
    out.dao = v.dao;
    const ManualEntry* man = findManual(id);
    out.effect = man ? man->effect + manualTriggerDesc(*man) : v.effect;
    out.coefficients = v.coefficients;
    out.stage = v.stage;
    out.price = v.price;
    return true;
}

const vector<BuildInfo>& artBuilds() {
    static const vector<BuildInfo> builds = {
        {"metal-discard", "金虹弃牌", "金",
         "明心咒补牌，换念诀弃置余牌凝盾并积锐势；下一次金罡斩消耗锐势，重势再放大伤害。回合末未打出的牌也能凝盾，护盾仅在本回合使用。",
         {"gold", "bladecare", "mind", "heavy"},
         {"refresh", "skim", "goldcover", "goldslash"}, {"0-4", "0-5", "4-4"}},
        {"water-wave", "叠浪回流", "水",
         "罡水印积蓄水势，浪击消耗水势爆发并按水势返灵；水道大成保留部分水势供下一轮使用。相生时先金后水，不必每次清空所有积累。",
         {"wave", "rainform", "ocean", "water"},
         {"waterSeal", "wavehit", "metalSeal", "softwater"}, {"2-4", "2-5"}},
        {"fire-stack", "积焰引爆", "火",
         "上清印衔接吐焰触发木生火；灼烧天然累积并刷新持续回合，火系功法增加每次施加量。引焰爆按剩余灼烧回合引爆，随后清空灼烧。",
         {"playfire", "flame", "kindle", "sunwheel"},
         {"woodSeal", "spitfire", "fireSeal", "fireburst"}, {"3-4", "3-5", "9-4"}},
        {"wood-poison", "毒生回春", "木",
         "腐毒咒施毒时按新增毒层回血，保留活跃毒到回合末继续增长；在毒将失效或需要斩杀时打引毒咒消耗毒层。回春术和木壁术保障叠毒时间。",
         {"venom", "turtle", "wood", "evergreen"},
         {"corrode", "poisonBurst", "wood", "woodwall"}, {"1-4", "1-5", "10-5"}},
        {"earth-charge", "厚土蓄势", "土",
         "获得护盾触发重势，三山印衔接金罡斩形成土生金；重击消耗重势并附加当前护盾伤害。盾被敌方攻击消耗时可用体道反震，不能跨回合无代价囤盾。",
         {"mountain", "earth", "jadecloud", "bull"},
         {"pull", "mountainSeal", "goldslash", "aegis"}, {"4-4", "4-5", "6-5"}},
        {"sword-chain", "连剑归锋", "剑",
         "同一回合连续剑招逐步增加追加伤害段，每连三剑返灵；人剑消耗剑气和少量气血作终结。中途使用非剑牌会打断连剑，剑意激发应在连剑前铺好。",
         {"swordcraft", "hidden", "human", "sword"},
         {"swordFocus", "sword", "humanSword", "rain"}, {"7-4", "7-5"}},
        {"mind-cycle", "神识精筹", "神",
         "明心咒和洞观减费多抽；换念诀重整手牌，忘机咒选择一张牌并移除它在本场的所有副本，再抽牌。少出牌时聚灵，藏灵诀封存余灵支持下一回合的爆发。",
         {"visual", "mind", "zhou", "cloud"},
         {"skim", "refresh", "purge", "hoard"}, {"5-4", "5-5", "8-4", "8-5"}},
        {"five-elements", "五行相生", "阵",
         "按土→金→水→木→火→土排列神通，每次相生返灵；阵道大成额外抽牌，每回合最多额外两张。先用藏机术准备下一轮手牌，再衔接相生，气道封存余灵使循环更稳定。",
         {"array", "kindle", "wheel", "zhou"},
         {"mountainSeal", "metalSeal", "waterSeal", "woodSeal", "fireSeal", "scheme"},
         {"9-4", "9-5", "8-4"}},
    };
    return builds;
}

string artAnnualError(const Game& g, const string& type, const string& id) {
    const Player& p = g.player;
    const Arts& a = p.arts;

    if (type == "learn") {
        ManualEntryView m;
        if (!lookupManual(id, m)) return "无此功法。";
        // 抄本门槛：JS 里没抄本就不能修习。
        // 之前漏了这条，等于所有功法都能凭空学。
        const bool hasBook =
            std::find(g.economy.books.begin(), g.economy.books.end(), id) !=
                g.economy.books.end() ||
            std::find(a.bookList.begin(), a.bookList.end(), id) !=
                a.bookList.end();
        if (!hasBook)
            return "尚未获得此功法，请从书坊、藏经阁或机缘获取。";
        if (p.stage < m.stage) return "境界尚不足。";
        auto it = a.manuals.find(m.id);
        if (it != a.manuals.end() && it->second >= 5) return "功法已满五层。";
        if (p.stones < m.price) return "修习资粮不足。";
        return "";
    }
    if (type == "meditate") {
        return daoIndex(id) >= 0 ? "" : "未知大道。";
    }
    if (type == "brew") {
        const BrewInfo b = artBrewInfo(p, id);
        if (!b.valid) return "无此丹方。";
        const data::RecipeDef* r = findRecipe(id);
        const data::FurnaceDef* f = findFurnace(a.furnace);
        if (r->grade > artTier(p)) return "此境界只能炼制低阶丹药。";
        if (!f || f->grade < r->grade || f->capacity < b.count)
            return "丹炉品阶或容量不足。";
        if (a.durability < 1) return "丹炉需修理。";
        if (p.stones < b.cost) return "开炉灵石不足。";
        for (const auto& [herbName, n] : parseIngredients(r->ingredients)) {
            const data::HerbDef* h = findHerbByName(herbName);
            if (!h) return "指定药材不足；泛用灵药不能替代丹方。";
            auto it = a.herbs.find(h->name);
            if (it == a.herbs.end() || it->second < n)
                return "指定药材不足；泛用灵药不能替代丹方。";
        }
        return "";
    }
    if (type == "forge") {
        const auto info = ecoActionInfo(g, "forge", id);
        if (!info.valid) return "配料或器型无效。";
        const ForgeInfo b = artForgeInfo(p, info.mainId, info.auxId, info.coreId, info.shapeId);
        if (b.grade > 1 + p.stage / 3) return "境界不足以炼制此品阶法器。";
        if (b.grade > 1 + artLevel(a.exp.count("器") ? a.exp.at("器") : 0))
            return "器道不足以炼制此品阶法器。";
        if (p.stones < b.cost) return "铸炼灵石不足。";
        string shortages;
        for (const auto& material : artForgeMaterials(p, info.mainId, info.auxId, info.coreId)) {
            if (material.missing == 0) continue;
            if (!shortages.empty()) shortages += "；";
            shortages += material.name + "需 " + std::to_string(material.required) +
                         " 份，持有 " + std::to_string(material.owned) + " 份，缺 " +
                         std::to_string(material.missing) + " 份";
        }
        if (!shortages.empty()) return "炼器材料不足：" + shortages + "。";
        return "";
    }
    if (type == "mine") return "";
    if (type == "arena") {
        if (p.health < 35) return "气血不足，请先养伤。";
        if (p.stones < 10) return "报名需 10 灵石。";
        return "";
    }
    if (type == "quest") {
        return artQuestError(g, findQuest(id));
    }
    return "";
}

// ------------------------------------------------------------
// 年度洞府结算（对应 artAnnual）
// ------------------------------------------------------------
void artAnnual(Game& g, const string& type, const string& id) {
    Player& p = g.player;
    Arts& a = p.arts;

    if (type == "learn") {
        ManualEntryView m;
        if (!lookupManual(id, m)) return;
        p.stones -= m.price;
        a.manuals[m.id] = a.manuals[m.id] + 1;
        artGain(p, m.dao, 12);
        logEvent(g, "self",
                 "修习" + m.name + "至第 " + std::to_string(a.manuals[m.id]) +
                     " 层。",
                 false, "", "灵石 −" + std::to_string(m.price));
    } else if (type == "meditate") {
        artGain(p, id, jsRound(10 + p.insight * 0.25));
        p.insight = std::min(INSIGHT_MAX, p.insight + 2);
        a.focus = id;
        logEvent(g, "self", "参悟" + id + "之大道，当前" +
                                ART_LEVELS[artLevel(a.exp[id])] + "。");
    } else if (type == "brew") {
        const BrewInfo b = artBrewInfo(p, id);
        const data::RecipeDef* r = findRecipe(id);
        if (!r) return;
        p.stones -= b.cost;
        // 扣材料
        for (const auto& [herbName, n] : parseIngredients(r->ingredients)) {
            const data::HerbDef* h = findHerbByName(herbName);
            if (!h) continue;
            a.herbs[h->name] -= n;
            p.herbs -= n;
        }
        const data::FurnaceDef* f = findFurnace(a.furnace);
        // 高阶炉炼低阶丹不损耐久
        if (!(artHas(a, "丹", 4) && f && f->grade > r->grade)) a.durability--;
        p.alchemy = std::min(500, p.alchemy + 3);
        artGain(p, "丹", 12);
        const bool win = randomOf(g) < b.chance;
        if (win) {
            a.potions[r->id] = a.potions[r->id] + b.output;
            a.progress.brew++;
        }
        logEvent(g, "self",
                 r->name + "开炉" + (win ? "成丹" + std::to_string(b.output) + "枚" : "失败，材料耗尽") + "。",
                 false, "", "丹术 +3 · 灵石 −" + std::to_string(b.cost));
    } else if (type == "forge") {
        // id 编码 "main|aux|core|shape"
        const size_t p1 = id.find('|');
        if (p1 == string::npos) return;
        const size_t p2 = id.find('|', p1 + 1);
        const size_t p3 = id.find('|', p2 + 1);
        if (p2 == string::npos || p3 == string::npos) return;
        const string mainId = id.substr(0, p1);
        const string auxId = id.substr(p1 + 1, p2 - p1 - 1);
        const string coreId = id.substr(p2 + 1, p3 - p2 - 1);
        const string shapeId = id.substr(p3 + 1);
        const ForgeInfo b = artForgeInfo(p, mainId, auxId, coreId, shapeId);
        if (!b.valid) return;
        p.stones -= b.cost;
        for (const auto& material : artForgeMaterials(p, mainId, auxId, coreId))
            a.materials[material.id] -= material.required;
        artGain(p, "器", 15);
        const bool win = randomOf(g) < b.chance;
        if (win) {
            Gear gear;
            gear.id = "forge-" + std::to_string(g.year) + "-" +
                      std::to_string(a.progress.forge);
            gear.name = b.mainName + b.shapeName;
            gear.slot = b.shapeSlot;
            gear.shape = b.shapeId;
            gear.grade = b.grade;
            gear.bonus = b.bonus;
            gear.element = b.mainElement;
            gear.polarity = b.mainPolarity;
            gear.inscribed = false;
            gear.craftCost = b.cost;
            gear.crafted = true;
            a.gear.push_back(gear);
            a.progress.forge++;
        }
        logEvent(g, "self",
                 "铸炼" + b.mainName + b.shapeName +
                     (win ? "成功" : "失败") + "，材料利用率 " +
                     std::to_string(jsRound(b.util * 100)) + "%。",
                 win);
    } else if (type == "mine") {
        const int tier = ordinaryMaterialGradeLimit(p);
        vector<const data::MaterialDef*> pool;
        for (const auto& m : data::MaterialDef_TABLE())
            if (m.grade <= tier) pool.push_back(&m);
        if (pool.empty()) return;
        const data::MaterialDef* m = pickOf(g, pool);
        a.materials[m->id] = a.materials[m->id] + 3;
        artGain(p, "器", 5);
        logEvent(g, "self", "沿山脉寻矿，获得" + m->name + " ×3。");
    } else if (type == "arena") {
        p.stones -= 10;
        EnemySpec enemy;
        enemy.name = "演武场同阶修士";
        enemy.stage = p.stage;
        enemy.kind = "spar";
        battleStart(g, enemy);
    } else if (type == "quest") {
        const data::QuestDef* q = findQuest(id);
        if (!q) return;
        p.stones -= q->price;
        a.visits[q->id] = a.visits[q->id] + 1;
        if (a.visits[q->id] < q->visits) {
            artGain(p, "剑", 15);
            logEvent(g, "self", "参悟剑壁 " + std::to_string(a.visits[q->id]) +
                                    " / " + std::to_string(q->visits) + " 次。");
        } else if (q->battle) {
            EnemySpec enemy;
            enemy.name = q->name + "守关修士";
            enemy.stage = p.stage;
            enemy.quest = q->id;
            enemy.kind = "spar";
            battleStart(g, enemy);
        } else {
            artRewardQuest(g, *q);
        }
    }
}

// ============================================================
// 洞府即时操作（对应 JS 的 artInstant）
// ============================================================
// 这个入口在 arts.h 里声明了很久却一直没有实现，于是「携带神通 /
// 卸下神通」在 C++ 版里根本不存在：开局虽然由 ensureArts 装好了
// 四个初始神通，但玩家既看不到「装备」这个动作，也没法换神通，
// 斗法牌组只能一直用开局那四张。
//
// 这里按 JS artInstant 的分支补齐，规则与 JS 逐条对齐。
ActionError artInstant(Game& g, const string& type, const string& id) {
    ActionError out;
    Player& p = g.player;
    Arts& a = p.arts;

    auto fail = [&](const string& msg) {
        ActionError e;
        e.ok = false;
        e.error = msg;
        return e;
    };

    // 与 JS 一致的前置门槛
    if (g.ended || g.pending.has_value()) return fail("请先处理当前事件。");
    if (a.battle.has_value() && type != "battle")
        return fail("请先完成斗法。");

    if (type == "focus") {
        if (daoIndex(id) < 0) return fail("未知大道。");
        a.focus = id;
    } else if (type == "node") {
        const NodeDef* n = findNode(id);
        if (!n) return fail("没有这个道法节点。");
        if (std::find(a.nodes.begin(), a.nodes.end(), id) != a.nodes.end())
            return fail("这个节点已经点亮。");
        const string dao = ART_DAOS[n->dao];
        const int exp = a.exp.count(dao) ? a.exp.at(dao) : 0;
        if (artLevel(exp) < n->level) return fail("道法境界不足。");
        if (a.points < n->cost) return fail("悟道点不足。");
        a.points -= n->cost;
        a.nodes.push_back(id);
    } else if (type == "main" || type == "aux") {
        if (!a.manuals.count(id) || a.manuals[id] <= 0)
            return fail("尚未学会该功法。");
        if (type == "main") {
            a.main = id;
            a.aux.erase(std::remove(a.aux.begin(), a.aux.end(), id), a.aux.end());
        } else if (std::find(a.aux.begin(), a.aux.end(), id) != a.aux.end()) {
            a.aux.erase(std::remove(a.aux.begin(), a.aux.end(), id), a.aux.end());
        } else {
            if (a.main == id || a.aux.size() >= 4)
                return fail("辅修最多四项，且不能与主修重复。");
            a.aux.push_back(id);
        }
    } else if (type == "spell") {
        // 携带 / 卸下额外神通。灵击和守势固定自带，不占槽也不能卸下。
        const SpellDef* spell = battleSpell(id);
        if (!spell) return fail("没有这个神通。");
        if (id == "strike" || id == "defend")
            return fail("灵击和守势固定自带，不占额外神通槽。");
        auto it = std::find(a.spells.begin(), a.spells.end(), id);
        if (it != a.spells.end()) {
            a.spells.erase(it);
        } else {
            if (p.stage < spell->stage) return fail("境界不足。");
            if (std::find(a.knownSpells.begin(), a.knownSpells.end(), id) == a.knownSpells.end())
                return fail("尚未领悟，请向熟悉此术的道友请教。");
            const int slots = battleSpellSlots(p);
            if (a.spells.size() >= static_cast<size_t>(slots))
                return fail("最多携带" + std::to_string(slots) + "个额外神通，请先卸下一个。");
            a.spells.push_back(id);
        }
    } else if (type == "equip") {
        const Gear* gear = nullptr;
        for (const auto& x : a.gear)
            if (x.id == id) gear = &x;
        if (!gear) return fail("法器不存在。");
        if (gear->grade > artTier(p)) return fail("境界不足以装备此品阶法器。");
        auto it = a.equipped.find(gear->slot);
        if (it != a.equipped.end() && it->second == id)
            a.equipped.erase(it);
        else
            a.equipped[gear->slot] = id;
    } else if (type == "salvage") {
        const Gear* gear = nullptr;
        for (const auto& x : a.gear)
            if (x.id == id) gear = &x;
        if (!gear) return fail("法器不存在。");
        const string slot = gear->slot;
        const int grade = gear->grade;
        a.gear.erase(std::remove_if(a.gear.begin(), a.gear.end(),
                                    [&](const Gear& x) { return x.id == id; }),
                     a.gear.end());
        auto it = a.equipped.find(slot);
        if (it != a.equipped.end() && it->second == id) a.equipped.erase(it);
        p.stones += 5 * grade;
    } else if (type == "inscribe") {
        Gear* gear = nullptr;
        for (auto& x : a.gear)
            if (x.id == id) gear = &x;
        if (!gear) return fail("法器不存在。");
        if (gear->inscribed) return fail("这件法器已经铭刻过。");
        if (artLevel(a.exp.count("器") ? a.exp.at("器") : 0) < 1)
            return fail("铭刻需器道入门。");
        const int cost = 30 * gear->grade;
        if (p.stones < cost) return fail("铭刻需 30×品阶 灵石。");
        p.stones -= cost;
        gear->inscribed = true;
    } else {
        return fail("未知洞府操作。");
    }

    a.progress.wealth = std::max(a.progress.wealth, p.stones);
    return out;   // ok = true
}

} // namespace nz
