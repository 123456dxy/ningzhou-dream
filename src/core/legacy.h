// 宁州一梦 · C++ 版 — 转世与传承
//
// 对应 JS sim.mjs 的 createLegacy / talentBudget / validateLegacy /
// settleLegacy / draftTalents。
//
// 这是原版「重复游玩」的全部动机：一世结束时按里程碑折算传承点，
// 传承点提高下一世的建号预算，建号时从天赋池里随机抽 12 项。
// C++ 之前整块没有，所以结局之后毫无延续。
#pragma once

#include <string>
#include <vector>

#include "core/model.h"

namespace nz::legacy {

// 传承档案（对应 JS 的 createLegacy 结构）
struct Archive {
    int version = 1;
    int earned = 0;                  // 累计传承点
    int runs = 0;                    // 已结算的世数
    std::vector<std::string> claimed;      // 已结算的 lifeId
    std::vector<std::string> milestones;   // 已达成的里程碑 key
};

Archive create();

// 建号预算 = min(40, 10 + earned)
int talentBudget(const Archive& a);

// 校验（对应 validateLegacy）
std::string validate(const Archive& a);

// 里程碑条目（界面上要展示「这一世拿到了什么」）
struct Milestone {
    std::string key;
    std::string label;
    int points = 1;
    bool reached = false;
    bool alreadyHad = false;
};

// 列出当前这一世能拿到的全部里程碑（含未达成的，供界面显示进度）
std::vector<Milestone> milestonesOf(const Game& g, const Archive& a);

struct SettleResult {
    bool eligible = false;
    int points = 0;
    std::vector<std::string> reasons;
    std::string note;
};
// 结算这一世（对应 settleLegacy）
SettleResult settle(const Game& g, Archive& a);

// 建号抽天赋（对应 draftTalents）：抽 12 项，可重抽
// roll 是重抽次数，用同一 seed 得到确定的结果
std::vector<std::string> draft(uint32_t seed, int roll);

// 传承档案的存读（JSON 字符串）
std::string toJson(const Archive& a);
Archive fromJson(const std::string& text, bool* ok = nullptr);

} // namespace nz::legacy
