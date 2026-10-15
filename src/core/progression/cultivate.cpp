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

// 用途：把小境界映射为修炼倍率的大境界组。
// 参与流程：境界系数与配给开销内部计算。
// 使用方式：传合法角色境界，最高取第四组。
// 返回值：从零开始的大境界组序号。
int groupOf(const Player& p) { return std::min(4, p.stage / 3); }

// 灵气配给的加成幅度：比灵石猛得多，但只在洞府生效
const double QI_STEP = 0.30;      // 每档 +30%
const double STONE_STEP = 0.12;   // 每档 +12%
} // namespace

// 用途：给出灵石配给全局最高档位。
// 参与流程：配给设置校验与倍率限制。
// 使用方式：无参数，零表示未配给。
// 返回值：最高档位三。
int maxSupplyLevel() { return 3; }
// 用途：计算洞府当前解锁的配给上限。
// 参与流程：配给选项与灵气倍率限制。
// 使用方式：传角色，按洞府 tier 夹在零至二后换算。
// 返回值：一至三档配给上限。
int unlockedSupplyLevel(const Player& p) { return 1 + clampInt(p.caveTier, 0, 2); }

// ------------------------------------------------------------
// 三个乘数
// ------------------------------------------------------------
// 用途：将资质转为修炼速度乘数。
// 参与流程：基础修炼速度与拆解展示。
// 使用方式：传角色，以资质九十为一倍基准。
// 返回值：限制在 0.5..2.0 的资质倍率。
double aptMul(const Player& p) {
    // 以资质 90 为基准 1.0（初始掷出 60–120 的中值），
    // 斜率取到 200 时约 2.0 —— 正好用满上限而不溢出。
    return clamp(1.0 + (p.apt - 90) * 0.009, 0.50, 2.00);
}

// 用途：将悟性转为修炼速度乘数。
// 参与流程：基础修炼速度与拆解展示。
// 使用方式：传角色，以悟性三十七为一倍基准。
// 返回值：限制在 0.5..2.0 的悟性倍率。
double insightMul(const Player& p) {
    // 以悟性 37 为基准 1.0（初始掷出 15–60 的中值），
    // 同样在 200 处约 2.0。
    return clamp(1.0 + (p.insight - 37) * 0.006, 0.50, 2.00);
}

// 用途：计算功法等常驻修炼系数的基础速度。
// 参与流程：基础修炼速度第一项。
// 使用方式：传角色，从 artModsOf 取 cult，缺省保留可修炼基数。
// 返回值：旧十八点基数乘统一修为单位倍率，再乘有效 cult；药效不经过此入口。
double manualBase(const Player& p) {
    // 功法基础速度：主修功法每层的修炼加成（artMods.cult）
    // 一个功法都没修的话给个最低值，不至于完全练不动
    const double cult = artModsOf(p.arts).cult;
    return 18.0 * CultivationUnits::Scale * (cult > 0 ? cult : 1.0);
}

// 用途：按大境界组读取修炼倍率。
// 参与流程：基础修炼速度与拆解展示。
// 使用方式：传角色，使用既有 GROUP_MUL 表。
// 返回值：当前大境界组的倍率。
double realmMul(const Player& p) { return GROUP_MUL[groupOf(p)]; }

// ------------------------------------------------------------
// 配给
// ------------------------------------------------------------
// 用途：计算指定配给档位的每月灵石开销。
// 参与流程：配给预览和月度资源结算。
// 使用方式：传角色、stone/qi 和档位；零档不花费。
// 返回值：按大境界与灵气双倍费用计算的灵石数。
int supplyCost(const Player& p, const string& kind, int level) {
    if (level <= 0) return 0;
    const int g = groupOf(p);
    // 基础 10 灵石/档，每境界组翻倍式增长；灵气贵一倍
    const int base = 10 + g * 14 + g * g * 6;
    const int mul = (kind == "qi") ? 2 : 1;
    return level * base * mul;
}

// 用途：计算随处生效的灵石配给倍率。
// 参与流程：最终修炼速度。
// 使用方式：传角色，档位限制到全局配给上限。
// 返回值：一加每档十二个百分点的倍率。
double stoneSupplyMul(const Player& p) {
    const int lv = clampInt(p.stoneSupply, 0, maxSupplyLevel());
    return 1.0 + lv * STONE_STEP;
}

// 用途：计算在洞府生效的灵气配给倍率。
// 参与流程：最终修炼速度和配给展示。
// 使用方式：传角色，不在洞府或未引灵气时不加成。
// 返回值：有效档位的灵气倍率，否则为一。
double qiSupplyMul(const Player& p) {
    if (!qiActive(p)) return 1.0;
    const int lv = clampInt(p.qiSupply, 0, unlockedSupplyLevel(p));
    return 1.0 + lv * QI_STEP;
}

// 用途：计算府内洞府气场倍率。
// 参与流程：最终修炼速度的独立洞府收益。
// 使用方式：传角色，需已设置洞府且当前在府内。
// 返回值：洞府 tier 每档加一成，否则为一。
double caveMul(const Player& p) {
    return atCave(p) ? 1.0 + 0.10 * clampInt(p.caveTier, 0, 2) : 1.0;
}

// 用途：判断玩家是否处于已安置洞府的地点。
// 参与流程：灵气配给和洞府气场生效校验。
// 使用方式：传角色，未安置洞府不能当作在府内。
// 返回值：已安府且当前位置相同为真。
bool atCave(const Player& p) {
    // 没安府就是没洞府 —— 灵气配给必须先在洞府才谈得上。
    // 之前这里把「没设过洞府」当成「就在洞府」，于是新号一天府都没安
    // 也能开灵气配给，等于白送加成。
    if (p.cave.empty()) return false;
    return p.cave == p.location;
}

// 用途：检查当前灵气配给能否生效。
// 参与流程：灵气倍率查询。
// 使用方式：传角色，要求府内且灵气档位大于零。
// 返回值：灵气配给此刻有效时为真。
bool qiActive(const Player& p) {
    return atCave(p) && p.qiSupply > 0;
}

// 用途：合并灵石与有效灵气配给倍率。
// 参与流程：最终修炼速度结算。
// 使用方式：传角色，乘上两项配给系数。
// 返回值：配给总乘数。
double supplyMul(const Player& p) {
    return stoneSupplyMul(p) * qiSupplyMul(p);
}

// ------------------------------------------------------------
// 总速度
// ------------------------------------------------------------
// 用途：合成功法、资质、悟性、境界、传承和天赋基础速度。
// 参与流程：闭关速度及基础速度展示。
// 使用方式：传角色，配给和洞府气场由 rate 后续乘入。
// 返回值：不含配给与洞府气场的修炼速度。
double baseRate(const Player& p) {
    return manualBase(p) * aptMul(p) * insightMul(p) * realmMul(p) *
           legacyEfficiencyOf(p) * talentCultivationMul(p);
}

// 用途：将基础速度乘以配给和府内气场。
// 参与流程：最终修炼量计算。
// 使用方式：传角色只读状态。
// 返回值：当前条件下最终修炼速度。
double rate(const Player& p) { return baseRate(p) * supplyMul(p) * caveMul(p); }

// 用途：拆解当前修炼速度及府内灵气生效原因。
// 参与流程：修炼页面解释和数值预览。
// 使用方式：传角色，所有因子共用实际速度函数。
// 返回值：各倍率、总速度和洞府说明的 Breakdown 值。
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
// 用途：计算资质和悟性对突破概率的贡献。
// 参与流程：突破成功率汇总。
// 使用方式：传角色；悟性平方根和资质项各自限制收益。
// 返回值：两项合计的概率加成，可含资质负修正。
double breakthroughBonus(const Player& p) {
    // 悟性：平方根增长，上限 +12%（理解力帮你稳住关口）
    const double ins = std::min(0.12, jsSqrt(p.insight) * 0.012);
    // 资质：以 90 为基准（初始中值），上限 +8%（灵根好，冲关顺）
    const double apt = clamp((p.apt - 90) * 0.0008, -0.05, 0.08);
    return ins + apt;
}

// 用途：按功法层数、悟性和聪颖天赋计算修习天数。
// 参与流程：学习功法行动报价。
// 使用方式：传角色与要学层数。
// 返回值：至少三天的修习耗时。
int learnDays(const Player& p, int level, int tier) {
    // level 为已修层数；基础四十天，每层 ×1.2、每阶 ×2。
    const double base = 40.0 * std::pow(1.2, clampInt(level, 0, 4)) *
                        std::pow(2.0, clampInt(tier, 1, 5) - 1);
    const double speed = clamp(1.0 - (static_cast<double>(p.insight) - 10.0) * 0.010, 0.6, 1.5);
    return std::max(3, static_cast<int>(std::ceil(base * speed *
        (hasTalent(p, "savvy") ? 0.80 : 1.0) - 1e-9)));
}


// ------------------------------------------------------------
// 体魄 → 气血上限（带软上限）
// ------------------------------------------------------------
// 用途：提供体魄气血收益的软上限起点。
// 参与流程：体魄收益计算与界面解释。
// 使用方式：无参数，与 bodyHpBonus 使用同一常量。
// 返回值：八点体魄。
int bodySoftCap() { return 8; }
// 用途：提供体魄对气血倍率的硬上限。
// 参与流程：体魄收益计算与界面解释。
// 使用方式：无参数。
// 返回值：最高额外气血比例 0.72。
double bodyHpCap() { return 0.72; }

// 用途：按软上限计算体魄增加的斗法气血比例。
// 参与流程：斗法开局气血上限与预览。
// 使用方式：传体魄点数，前八点每点百分之六，之后每点百分之二。
// 返回值：不超过百分之七十二的额外气血比例。
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
// 用途：按配给种类和档位生成中文名称。
// 参与流程：配给选项展示。
// 使用方式：传 stone/qi 及档位，零档返回未开启名称。
// 返回值：对应灵石或灵气档位名称。
string supplyName(const string& kind, int level) {
    const bool qi = (kind == "qi");
    if (level <= 0) return qi ? "不引灵气" : "不配给";
    if (level == 1) return qi ? "涓流" : "薄给";
    if (level == 2) return qi ? "充盈" : "常给";
    return qi ? "鼎盛" : "厚给";
}

// 用途：解释指定配给档位的速度增益与地点条件。
// 参与流程：配给页面说明。
// 使用方式：传 stone/qi 及档位，沿用实际每档倍率常量。
// 返回值：可展示的配给效果文案。
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
