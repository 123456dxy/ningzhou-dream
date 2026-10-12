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

// 用途：创建空的账号传承档案。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：Archive结果，包含说明所述状态或计算数据。
Archive create();

// 用户实际新开一世时使用；世界种子仍只负责可复现的游戏随机数。
// 用途：创建独立人生结算标识。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入uint32_t seed，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string newLifeId(uint32_t seed);

// 建号预算 = min(40, 10 + earned)
// 用途：把传承点换算为新世可分配天赋预算。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int talentBudget(const Archive& a);

// 校验（对应 validateLegacy）
// 用途：校验账号传承的结构与点数范围。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：校验失败原因；合法档案返回空串。
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
// 用途：列出本世达成情况与账号已领取里程碑。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Game& g, const Archive& a，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
std::vector<Milestone> milestonesOf(const Game& g, const Archive& a);

struct SettleResult {
    bool eligible = false;
    int points = 0;
    std::vector<std::string> reasons;
    std::string note;
};
// 结算这一世（对应 settleLegacy）
// 用途：仅结算一次已结束人生的新增传承收益。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Game& g, Archive& a，按本模块入口调用。
// 返回值：新增点数、理由与是否有资格；重复人生不重复领取。
SettleResult settle(const Game& g, Archive& a);

// 建号抽天赋（对应 draftTalents）：抽 12 项，可重抽
// roll 是重抽次数，用同一 seed 得到确定的结果
// 用途：按种子与刷新次数生成天赋候选列表。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入uint32_t seed, int roll，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::vector<std::string> draft(uint32_t seed, int roll);

// 传承档案的存读（JSON 字符串）
// 用途：序列化账号传承档案。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const Archive& a，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
std::string toJson(const Archive& a);
// 用途：解析并校验账号传承JSON。
// 参与流程：人生结局、成就传承与新世天赋选择。
// 使用方式：传入const std::string& text, bool* ok = nullptr，按本模块入口调用。
// 返回值：Archive结果，包含说明所述状态或计算数据。
Archive fromJson(const std::string& text, bool* ok = nullptr);

} // namespace nz::legacy
