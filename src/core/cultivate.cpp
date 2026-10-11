// 宁州一梦 · C++ 版 — 修炼速度与配给（实现）
#include "core/cultivate.h"

#include <algorithm>
#include <cmath>

#include "core/arts.h"
#include "core/base.h"
#include "core/sim.h"
#include "core/talents.h"

namespace nz::cultivate {

using std::string;

namespace {
// 境界系数：越高境界基数越大（保留原有手感）
const double GROUP_MUL[5] = {1.0, 1.38, 3.2, 4.5, 8.0};

int groupOf(const Player& p) { return std::min(4, p.stage / 3); }

// 灵气配给的加成幅度：比灵石猛得多，但只在洞府生效
const double QI_STEP = 0.30;      // 每档 +30%
const double STONE_STEP = 0.12;   // 每档 +12%
} // namespace

int maxSupplyLevel() { return 3; }
int unlockedSupplyLevel(const Player& p) { return 1 + clampInt(p.caveTier, 0, 2); }

// ------------------------------------------------------------
// 三个乘数
// ------------------------------------------------------------
double aptMul(const Player& p) {
    // 以资质 90 为基准 1.0（初始掷出 60–120 的中值），
    // 斜率取到 200 时约 2.0 —— 正好用满上限而不溢出。
    return clamp(1.0 + (p.apt - 90) * 0.009, 0.50, 2.00);
}

double insightMul(const Player& p) {
    // 以悟性 37 为基准 1.0（初始掷出 15–60 的中值），
    // 同样在 200 处约 2.0。
    return clamp(1.0 + (p.insight - 37) * 0.006, 0.50, 2.00);
}

double manualBase(const Player& p) {
    // 功法基础速度：主修功法每层的修炼加成（artMods.cult）
    // 一个功法都没修的话给个最低值，不至于完全练不动
    const double cult = artModsOf(p.arts).cult;
    return 18.0 * (cult > 0 ? cult : 1.0);
}

double realmMul(const Player& p) { return GROUP_MUL[groupOf(p)]; }

// ------------------------------------------------------------
// 配给
// ------------------------------------------------------------
int supplyCost(const Player& p, const string& kind, int level) {
    if (level <= 0) return 0;
    const int g = groupOf(p);
    // 基础 10 灵石/档，每境界组翻倍式增长；灵气贵一倍
    const int base = 10 + g * 14 + g * g * 6;
    const int mul = (kind == "qi") ? 2 : 1;
    return level * base * mul;
}

double stoneSupplyMul(const Player& p) {
    const int lv = clampInt(p.stoneSupply, 0, maxSupplyLevel());
    return 1.0 + lv * STONE_STEP;
}

double qiSupplyMul(const Player& p) {
    if (!qiActive(p)) return 1.0;
    const int lv = clampInt(p.qiSupply, 0, unlockedSupplyLevel(p));
    return 1.0 + lv * QI_STEP;
}

double caveMul(const Player& p) {
    return atCave(p) ? 1.0 + 0.10 * clampInt(p.caveTier, 0, 2) : 1.0;
}

bool atCave(const Player& p) {
    // 没安府就是没洞府 —— 灵气配给必须先在洞府才谈得上。
    // 之前这里把「没设过洞府」当成「就在洞府」，于是新号一天府都没安
    // 也能开灵气配给，等于白送加成。
    if (p.cave.empty()) return false;
    return p.cave == p.location;
}

bool qiActive(const Player& p) {
    return atCave(p) && p.qiSupply > 0;
}

double supplyMul(const Player& p) {
    return stoneSupplyMul(p) * qiSupplyMul(p);
}

// ------------------------------------------------------------
// 总速度
// ------------------------------------------------------------
double baseRate(const Player& p) {
    return manualBase(p) * aptMul(p) * insightMul(p) * realmMul(p) *
           legacyEfficiencyOf(p) * talentCultivationMul(p);
}

double rate(const Player& p) { return baseRate(p) * supplyMul(p) * caveMul(p); }

Breakdown breakdown(const Player& p) {
    Breakdown b;
    b.manual = manualBase(p);
    b.apt = aptMul(p);
    b.insight = insightMul(p);
    b.realm = realmMul(p);
    b.legacy = legacyEfficiencyOf(p);
    b.stone = stoneSupplyMul(p);
    b.qi = qiSupplyMul(p);
    b.cave = caveMul(p);
    b.total = rate(p);
    if (p.qiSupply <= 0)
        b.caveNote = "未设灵气配给，洞府灵气未启用。";
    else if (!atCave(p))
        b.caveNote = "不在洞府，灵气配给不生效。回到洞府闭关才有加成。";
    else
        b.caveNote = "正在洞府闭关，灵气配给已生效。";
    if (b.cave > 1.0) b.caveNote += "洞府气场额外提升修炼速度 " + std::to_string(jsRound((b.cave - 1.0) * 100)) + "%。";
    return b;
}

// ------------------------------------------------------------
// 资质/悟性对其它系统的影响
// ------------------------------------------------------------
double breakthroughBonus(const Player& p) {
    // 悟性：平方根增长，上限 +12%（理解力帮你稳住关口）
    const double ins = std::min(0.12, jsSqrt(p.insight) * 0.012);
    // 资质：以 90 为基准（初始中值），上限 +8%（灵根好，冲关顺）
    const double apt = clamp((p.apt - 90) * 0.0008, -0.05, 0.08);
    return ins + apt;
}

int learnDays(const Player& p, int level) {
    // 基础 20 天 + 每层 10 天；悟性每点快 1%，最快打到六折
    const int base = 20 + level * 10;
    const double speed = clamp(1.0 - (p.insight - 10) * 0.010, 0.6, 1.5);
    return std::max(3, jsRound(base * speed * (hasTalent(p, "savvy") ? 0.80 : 1.0)));
}


// ------------------------------------------------------------
// 体魄 → 气血上限（带软上限）
// ------------------------------------------------------------
int bodySoftCap() { return 8; }
double bodyHpCap() { return 0.72; }

double bodyHpBonus(int body) {
    if (body <= 0) return 0.0;
    const int soft = bodySoftCap();
    const int capped = std::min(body, soft);
    const int over = std::max(0, body - soft);
    // 前 8 点每点 6%；超出部分每点 2%（收益降到三分之一）
    const double raw = capped * 0.06 + over * 0.02;
    return std::min(raw, bodyHpCap());
}

// ------------------------------------------------------------
// 文案
// ------------------------------------------------------------
string supplyName(const string& kind, int level) {
    const bool qi = (kind == "qi");
    if (level <= 0) return qi ? "不引灵气" : "不配给";
    if (level == 1) return qi ? "涓流" : "薄给";
    if (level == 2) return qi ? "充盈" : "常给";
    return qi ? "鼎盛" : "厚给";
}

string supplyDesc(const string& kind, int level) {
    const bool qi = (kind == "qi");
    if (level <= 0) return qi ? "洞府灵气闲置，闭关没有额外加成。"
                              : "不投入灵石，修炼只有功法本身的效率。";
    const double step = qi ? QI_STEP : STONE_STEP;
    const int pct = jsRound(level * step * 100);
    if (qi)
        return "洞府灵气提升修炼速度 +" + std::to_string(pct) +
               "%。只在洞府闭关时生效。";
    return "投入灵石改善修炼条件，速度 +" + std::to_string(pct) +
           "%。在任何地方闭关都算。";
}

} // namespace nz::cultivate
