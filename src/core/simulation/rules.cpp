// 宁州一梦 · C++ 版 — 规则表查找与人物生成
#include "core/model.h"
#include "core/talents.h"

#include <stdexcept>

namespace nz {

// 用途：按阶段查境界定义并限制有效范围。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入int stage，按本模块入口调用。
// 返回值：const RealmDef&结果，包含说明所述状态或计算数据。
const RealmDef& realmOf(int stage) {
    const auto& r = REALMS();
    if (stage < 0) stage = 0;
    if (stage >= static_cast<int>(r.size())) stage = static_cast<int>(r.size()) - 1;
    return r[static_cast<size_t>(stage)];
}

// 用途：按稳定ID查宗门定义。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const SectDef* findSect(const string& id) {
    for (const auto& s : SECTS())
        if (id == s.id) return &s;
    return nullptr;
}

// 用途：按稳定ID查地点定义。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const LocationDef* findLoc(const string& id) {
    for (const auto& l : LOCATIONS())
        if (id == l.id) return &l;
    return nullptr;
}

// 用途：按稳定ID查天赋定义。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const TalentDef* findTalent(const string& id) {
    for (const auto& t : TALENTS())
        if (id == t.id) return &t;
    return nullptr;
}

// 用途：按稳定ID查月度计划定义。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const PlanDef* findPlan(const string& id) {
    for (const auto& p : PLANS())
        if (id == p.id) return &p;
    return nullptr;
}

// 用途：查询地点定义判断是否城池。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const string& locId，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool isCity(const string& locId) {
    const LocationDef* l = findLoc(locId);
    return l && string(l->type) == "city";
}

// ------------------------------------------------------------
// 人物生成（对应 JS 版 buildCharacter）
// ------------------------------------------------------------
// JS 里对 traits/allocation 有一整套合法性校验，非法就抛错。
// 这里保持一致：C++ 版同样抛 std::invalid_argument，
// 调用方用 try/catch 处理，语义和 JS 的 throw 一样。
// 按种子掷初始资质/悟性。
// 资质 42~68、悟性 6~22：跨度刻意做得不小，
// 让「这一世灵根如何」成为一件值得看一眼的事，
// 而不是所有人都从 55/10 开始。
// 用途：按种子生成资质悟性福缘等初始属性。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入unsigned seed，按本模块入口调用。
// 返回值：InnateRoll结果，包含说明所述状态或计算数据。
InnateRoll rollInnate(unsigned seed) {
    InnateRoll r;
    if (seed == 0) return r;          // 自测用固定基准
    Rng rng(mixSeed(seed));
    // 范围刻意做宽：换一颗种子要能明显看出灵根好坏
    const int aptSpan = APT_ROLL_MAX - APT_ROLL_MIN + 1;
    const int insSpan = INSIGHT_ROLL_MAX - INSIGHT_ROLL_MIN + 1;
    r.apt = APT_ROLL_MIN + static_cast<int>(rng.next() * aptSpan);
    r.insight = INSIGHT_ROLL_MIN + static_cast<int>(rng.next() * insSpan);
    return r;
}


// 用途：校验天赋互斥与分配预算并生成初始角色。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const vector<string>& traits, const map<string, int>& allocation, int budget, unsigned seed，按本模块入口调用。
// 返回值：Build结果，包含说明所述状态或计算数据。
Build buildCharacter(const vector<string>& traits,
                     const map<string, int>& allocation,
                     int budget, unsigned seed) {
    if (budget < BASE_POINTS || budget > MAX_POINTS)
        throw std::invalid_argument("建号点数不合法。");
    // 天赋数量不再设上限 —— 只受点数约束。
    // 想选十项便宜的就选十项，只要点数够。

    // 不能重复选同一个天赋；也不能选不存在的天赋
    for (size_t i = 0; i < traits.size(); ++i) {
        if (!findTalent(traits[i]))
            throw std::invalid_argument("没有这个天赋。");
        for (size_t j = i + 1; j < traits.size(); ++j)
            if (traits[i] == traits[j])
                throw std::invalid_argument("同一个天赋不能选两次。");
    }
    const string conflict = talentConflict(traits);
    if (!conflict.empty()) throw std::invalid_argument(conflict);

    // 属性分配不能有未知键，也不能超出每项上限
    map<string, int> points;
    for (const auto& b : BUILD_STATS()) points[b.id] = 0;
    for (const auto& [k, v] : allocation) {
        if (!points.count(k)) throw std::invalid_argument("属性分配无效。");
        points[k] = v;
    }
    for (const auto& b : BUILD_STATS()) {
        int v = points[b.id];
        if (v < 0 || v > b.max)
            throw std::invalid_argument("属性点数超出范围。");
    }

    // 已花费点数 = 天赋费用之和 + 属性点之和
    int spent = 0;
    for (const auto& id : traits) spent += findTalent(id)->cost;
    for (const auto& [k, v] : points) spent += v;
    if (spent > budget)
        throw std::invalid_argument("点数超支 " + std::to_string(spent - budget) + " 点，请减少天赋或属性分配。");

    auto has = [&](const char* id) {
        return std::find(traits.begin(), traits.end(), string(id)) != traits.end();
    };

    Build r;
    r.budget = budget;
    r.spent = spent;
    r.traits = traits;
    r.allocation = points;

    const int pApt = points["apt"];
    const int pLuck = points["luck"];
    const int pInsight = points["insight"];
    const int pBody = points["body"];
    const int pWealth = points["wealth"];

    // 基础值公式与 JS 版逐字对应
    const InnateRoll innate = rollInnate(seed);
    r.apt = clampInt(innate.apt + pApt * 3 + (has("sword") ? 6 : 0) +
                        (has("root") ? 22 : 0), 0, 999);
    r.luck = clampInt(45 + pLuck * 4 + (has("luck") ? 22 : 0), 0, 999);
    r.insight = innate.insight + pInsight * 3;
    r.sword = has("sword") ? 18 : 2;
    r.alchemy = has("herb") ? 25 : 5;
    r.body = pBody + (has("body") ? 2 : 0);
    r.lifeBonus = has("longlife") ? 15 : 0;
    r.stones = 100 + pWealth * 20 - (has("root") ? 30 : 0);
    r.herbs = 3 + (has("herb") ? 4 : 0);

    // 天赋的单项属性加成
    for (const auto& id : traits) {
        const TalentDef* t = findTalent(id);
        if (!t || !t->stat || !*t->stat) continue;
        const string st = t->stat;
        const int v = t->value;
        if (st == "apt") r.apt += v;
        else if (st == "luck") r.luck += v;
        else if (st == "insight") r.insight += v;
        else if (st == "sword") r.sword += v;
        else if (st == "alchemy") r.alchemy += v;
        else if (st == "body") r.body += v;
        else if (st == "stones") r.stones += v;
        else if (st == "herbs") r.herbs += v;
        else if (st == "lifeBonus") r.lifeBonus += v;
    }

    // 几项天赋除了主加成还有额外效果（与 JS 版一致）
    if (has("craft")) r.stones += 15;
    if (has("fire")) r.alchemy += 5;
    if (has("wood")) r.herbs += 2;
    if (has("gold")) r.sword += 3;
    if (has("earth")) r.body++;
    if (has("wanderer")) r.luck += 3;
    if (has("hard")) r.insight += 4;

    r.apt = clampInt(r.apt, APT_MIN, APT_MAX);
    r.insight = clampInt(r.insight, INSIGHT_MIN, INSIGHT_MAX);

    r.luck = clampInt(r.luck, 0, 150);
    if (r.stones < 0) r.stones = 0;
    return r;
}

// 用途：检查角色是否拥有指定天赋。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const Player& p, const string& id，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool hasTalent(const Player& p, const string& id) {
    return std::find(p.traits.begin(), p.traits.end(), id) != p.traits.end();
}

// 天赋买点的加成：超出基础预算的部分提升修炼效率
// 用途：读取传承修炼效率加成。
// 参与流程：角色创建和稳定定义查询。
// 使用方式：传入const Player& p，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double legacyEfficiency(const Player& p) {
    const int budget = p.build.budget > 0 ? p.build.budget : BASE_POINTS;
    return 1.0 + std::max(0, budget - BASE_POINTS) * 0.015;
}

} // namespace nz
