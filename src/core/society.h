// 宁州一梦 · C++ 版 — 宗门与知交（对应 JS 版 society.mjs）
#pragma once

#include <string>

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz {

// NPC 姓名生成（姓 + 双字名组合，并排除重名）
// 对应 JS 版 society.mjs 的 socNPCName：用乘法定序取字，
// 保证同一个序号总是得到同一个名字。
string socNPCName(int index);

// 弟子身份（按大境界）
string socRank(const Person& p);
// 每月俸禄
int socStipend(const Person& p);

// 待领俸禄记账（对应 socAccrue）
void socAccrue(Game& g);

// 重建俸禄账簿（对应 socInitPayroll）
void socInitPayroll(Game& g, bool legacy = false);

// 初始化（新档或读旧档）
void ensureSociety(Game& g);

// ============================================================
// 原创支线（8 段，含 5 段隐藏）
// ============================================================
// 前置条件是否满足（对应 JS 里各故事自己的 requires 函数）
bool storyRequiresMet(const Game& g, const data::StoryDef& s);
// 返回空字符串表示可以触发；否则是拒绝原因
string storyError(const Game& g, const data::StoryDef* s);
// 选择本身的资源与重复结算检查；不满足时不得推进因缘记录。
string storyChoiceError(const Game& g, const data::StoryDef& s, int optionIndex);

// 完成一段支线并发放奖励。
// optionIndex 为 0/1，对应两个选项。
void storyComplete(Game& g, const data::StoryDef& s, int optionIndex);

// ============================================================
// 野外悬赏（3 类，不致死的斗法）
// ============================================================
const data::WildTaskDef* findWildTask(const string& id);
string wildActionError(const Game& g, const string& id);
void societyWildBegin(Game& g, const string& id);

// ============================================================
// 师徒 / 道侣
// ============================================================
// 收徒与结道侣的名额检查
bool bondCanTakeDisciple(const Game& g);

// 每月授业/相伴的校验；返回空字符串表示可以进行
string bondMonthlyError(const Game& g, const string& kind);

struct BondActionInfo {
    bool valid = false;
    string id, name, desc, npcId;
    int days = 0;
    int cost = 0;
};
BondActionInfo bondActionInfo(const Game& g, const string& kind);
string bondActionError(const Game& g, const string& kind);
void societyBondBegin(Game& g, const string& kind);

// ============================================================
// 关系往来的结算（拜师 / 授业 / 收徒 / 道侣）
// ============================================================
// npc 可为空（解除类动作不需要对象）
void societyBondComplete(Game& g, const string& kind, const Npc* npc);

// 野外悬赏完成时开战
void societyWildStart(Game& g, const Job& job);

// 野外悬赏斗法胜利后的奖励
void societyWildEnd(Game& g, const Battle& b);

// ============================================================
// 领取俸禄（对应 socInstant 的 stipend 分支）
// ============================================================
struct StipendResult {
    bool ok = true;
    string error;
    int paid = 0;    // 本次实付
    int owed = 0;    // 领取后仍欠（府库不足时保留）
};
StipendResult socCollectStipend(Game& g);

} // namespace nz
