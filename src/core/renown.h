// 宁州一梦 · C++ 版 — 声望系统
//
// 原版（JS）里「宁州声望」只是结局评分的一项，结局弹窗自己写着
// 「分数只用于比较你自己的不同人生」—— 游戏内没有任何判定读它。
// 也就是说玩家刷了一辈子声望，除了结算时多几分，什么也换不到。
//
// 这里把它做成一套真正的「信誉资本」：
//   名声是你在宁州的招牌，它影响别人怎么对你、愿意给你什么价钱。
//
// 所有加成集中在这个文件里，方便一处调平衡、也方便界面上如实展示。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::renown {

// 名望档位（给玩家看的身份标签）
struct Tier {
    const char* id;
    const char* name;
    const char* desc;
    int at;              // 达到该档位所需声望
};

const std::vector<Tier>& tiers();

// 当前档位
const Tier& tierOf(int renown);
// 距下一档还差多少（已满级返回 -1）
int toNextTier(int renown);
// 档位进度 0..1
double tierProgress(int renown);

// ------------------------------------------------------------
// 具体加成
// ------------------------------------------------------------
// 坊市买入折扣（0..0.15）：名声大的人，商家愿意让利
double buyDiscount(int renown);
// 坊市卖出溢价（0..0.20）
double sellBonus(int renown);
// 每月同类货品需求上限（基础 8）
int sellLimit(int renown);
// 坊市收购资金上限（基础 400）
int marketCashCap(int renown);

// 初见印象：名声在外，陌生人初始就对你有所耳闻（0..25）
int firstImpression(int renown);

// 藏经阁兑换折扣（0..0.25）
double libraryDiscount(int renown);

// 山门差事工钱加成（0..0.30）
double stipendBonus(int renown);

// 宗门入门要求（部分大派看名声）
int sectRequirement(const std::string& sectId);
// 拜师要求
int masterRequirement();

// 声望增减的说明文案（界面上用）
std::string describe(int renown);
struct Entry {
    std::string tab, sub, location, label;
};
struct Source {
    std::string id, name, gainText;
    std::vector<std::string> conditions;
    std::string limit;
    bool enabled = false;
    std::string reason;
    Entry entry;
};
// 只读目录，列出真实结算来源及当前资格；查看不会推进时间或随机数。
std::vector<Source> sources(const Game& g);
// 旧调用方的简明说明。
std::vector<std::string> sources();

} // namespace nz::renown
