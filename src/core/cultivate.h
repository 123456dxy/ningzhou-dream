// 宁州一梦 · C++ 版 — 修炼速度与配给
//
// 这一层把「修炼有多快」拆成玩家能看懂的几个因子：
//
//     修炼速度 = 功法基础 × 资质乘数 × 悟性乘数 × 境界系数 × 配给系数
//
//   · 功法基础：主修功法决定（每层给多少），这是「你会什么」
//   · 资质乘数：灵根好坏，决定同样的功法你能吸收多少
//   · 悟性乘数：理解力，决定功法发挥出几成
//   · 境界系数：越高境界需要的灵气越多，但基数也大
//   · 配给系数：灵石配给（到处都算）+ 灵气配给（只在洞府算）
//
// 旧版是 `(6 + apt*0.2) * 境界 * 传承 * 功法`，资质是唯一变量，
// 悟性只管参悟，配给完全没有 —— 玩家除了堆资质没别的选择。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::cultivate {

// ------------------------------------------------------------
// 三个乘数（界面上要如实展示，所以都单独暴露）
// ------------------------------------------------------------
// 资质乘数：以 55 为基准 1.0，每点 ±1%
double aptMul(const Player& p);
// 悟性乘数：以 10 为基准 1.0，每点 +1.2%
double insightMul(const Player& p);
// 功法基础：主修功法与洞府加成（对应 artMods.cult）
double manualBase(const Player& p);
// 境界系数
double realmMul(const Player& p);

// ------------------------------------------------------------
// 配给
// ------------------------------------------------------------
// 配给档位 0..3
int maxSupplyLevel();
// 当前洞府档位解锁的配给上限（0/1/2档洞府分别为1/2/3档配给）。
int unlockedSupplyLevel(const Player& p);

// 每月的灵石开销。境界越高越贵。
// 灵气比灵石贵一倍：灵石随处可用，灵气只在洞府生效。
int supplyCost(const Player& p, const std::string& kind, int level);

// 配给带来的乘数（灵石部分，到处都算）
double stoneSupplyMul(const Player& p);
// 灵气部分（只有「在洞府闭关」时才计入）
double qiSupplyMul(const Player& p);
// 洞府气场只在府内闭关生效，独立于是否付费配给。
double caveMul(const Player& p);

// 当前是否在洞府
bool atCave(const Player& p);
// 灵气配给此刻是否生效（在洞府 + 有档位）
bool qiActive(const Player& p);

// 本次闭关的配给总乘数
double supplyMul(const Player& p);

// ------------------------------------------------------------
// 总速度
// ------------------------------------------------------------
// 修炼速度（不含配给），用于界面展示「基础速度」
double baseRate(const Player& p);
// 最终修炼速度（含配给）
double rate(const Player& p);

// 把速度拆成可展示的几项
struct Breakdown {
    double manual = 0;      // 功法基础
    double apt = 1;         // 资质乘数
    double insight = 1;     // 悟性乘数
    double realm = 1;       // 境界系数
    double legacy = 1;      // 传承
    double stone = 1;       // 灵石配给
    double qi = 1;          // 灵气配给
    double cave = 1;        // 洞府气场
    double total = 0;       // 合计
    std::string caveNote;   // 灵气是否生效的说明
};
Breakdown breakdown(const Player& p);

// ------------------------------------------------------------
// 其它受资质/悟性影响的
// ------------------------------------------------------------
// 突破成功率的资质/悟性加成（对应 JS 的 insight + apt 项）
double breakthroughBonus(const Player& p);
// 修习功法所需天数：悟性越高越快（对应 ecoActionInfo 的 learn 分支）
int learnDays(const Player& p, int level);

// ------------------------------------------------------------
// 体魄提供的气血上限
// ------------------------------------------------------------
// 前 8 点每点 +6%（满 +48%）；8 点之后收益减半，每点 +2%；
// 总加成硬顶在 +72%。
//
// 为什么要软上限：体魄不只是属性点能加，天赋里也叠 ——
// 淬体根骨 +2、筋骨强健 +3、百折不挠 +2、土灵根 +1。
// 属性 8 点再加满天赋能到 15 点以上，纯线性会给出 +90% 气血，
// 比任何功法加成都强，于是「堆体魄」变成唯一解。
double bodyHpBonus(int body);
// 软上限的起点与硬顶（界面上要如实展示，所以暴露出来）
int bodySoftCap();
double bodyHpCap();

// 配给档位的中文名
std::string supplyName(const std::string& kind, int level);
// 配给档位的效果说明
std::string supplyDesc(const std::string& kind, int level);

} // namespace nz::cultivate
