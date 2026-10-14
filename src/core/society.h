// 宁州一梦 · C++ 版 — 宗门与知交（对应 JS 版 society.mjs）
#pragma once

#include <string>

#include "core/model.h"
#include "data/reference_tables.h"

namespace nz {

// NPC 姓名生成（姓 + 双字名组合，并排除重名）
// 对应 JS 版 society.mjs 的 socNPCName：用乘法定序取字，
// 保证同一个序号总是得到同一个名字。
// 用途：按名册序号生成稳定修士姓名。
// 参与流程：NPC 初始名册生成。
// 使用方式：传非负序号，组合姓名空间用尽后附序号。
// 返回值：确定性的姓名字符串。
string socNPCName(int index);

// 弟子身份（按大境界）
// 用途：按人物大境界确定宗门身份。
// 参与流程：角色和 NPC 宗门职阶展示。
// 使用方式：传合法人物境界。
// 返回值：外门至太上长老的身份名称。
string socRank(const Person& p);
// 每月俸禄
// 用途：按人物大境界查询基础月俸。
// 参与流程：俸禄初始化与月度记账。
// 使用方式：传人物，不含实际府库支付限制。
// 返回值：当前身份的基础月俸灵石数。
int socStipend(const Person& p);

// 待领俸禄记账（对应 socAccrue）
// 用途：为未记录月份累积应付俸禄。
// 参与流程：月度宗门生活结算。
// 使用方式：传游戏，恶名过高时暂停新增，宗门变化时重建账。
// 返回值：无；修改 payroll 余额、记账次数和已记月份。
void socAccrue(Game& g);

// 重建俸禄账簿（对应 socInitPayroll）
// 用途：重建当前宗门俸禄账。
// 参与流程：入宗、宗门变更与旧账迁移。
// 使用方式：传游戏；legacy 可按旧入宗日志补记。
// 返回值：无；修改 payroll 的宗门、月份、余额和记账次数。
void socInitPayroll(Game& g, bool legacy = false);

// 初始化（新档或读旧档）
// 用途：补齐天赋起源与历史资质丹额度。
// 参与流程：新档和旧档社会状态初始化。
// 使用方式：传游戏，仅在对应记录为空/零时补齐。
// 返回值：无；更新 bonds.origins 和 Arts.aptPills。
void ensureSociety(Game& g);

// ============================================================
// 原创支线（8 段，含 5 段隐藏）
// ============================================================
// 前置条件是否满足（对应 JS 里各故事自己的 requires 函数）
// 用途：检查支线特有的成长或名分条件。
// 参与流程：支线目录和开始校验。
// 使用方式：传游戏与故事定义，按 conditionId 分派。
// 返回值：故事成长条件满足时为真。
bool storyRequiresMet(const Game& g, const data::StoryDef& s);
// 返回空字符串表示可以触发；否则是拒绝原因
// 用途：检查支线定义、前置、地点及是否已完成。
// 参与流程：支线报名与目录资格。
// 使用方式：传游戏和可为空的故事定义。
// 返回值：拒绝原因；可以触发时为空。
string storyError(const Game& g, const data::StoryDef* s);
// 选择本身的资源与重复结算检查；不满足时不得推进因缘记录。
// 用途：检查故事选项是否合法且可再次结算。
// 参与流程：故事开始与完成共用。
// 使用方式：传游戏、故事和零/一选项，残碑承接需额外灵石。
// 返回值：拒绝原因；可选择时为空。
string storyChoiceError(const Game& g, const data::StoryDef& s, int optionIndex);

// API 与控制台共同安排真实支线工作；奖励由 monthComplete 在到期时结算。
// 用途：为已校验的支线选项建立耗时工作。
// 参与流程：API 和控制台安排故事。
// 使用方式：传游戏、定义及选项，奖励待完成时结算。
// 返回值：成功为空并建立 Job；拒绝为原因。
string storyBegin(Game& g, const data::StoryDef& s, int optionIndex);

// 完成一段支线并发放奖励。
// optionIndex 为 0/1，对应两个选项。
// 用途：按选项发放支线奖励并记本世完成。
// 参与流程：故事 Job 到期结算。
// 使用方式：传游戏、定义和选项，已完成或资源不足不重复发奖。
// 返回值：无；修改故事记录、属性、抄本、贡献和结算日志。
void storyComplete(Game& g, const data::StoryDef& s, int optionIndex);

// ============================================================
// 野外悬赏（3 类，有陨落风险的斗法）
// ============================================================
// 用途：按 ID 查野外悬赏定义。
// 参与流程：悬赏校验和报名。
// 使用方式：传悬赏 ID。
// 返回值：匹配定义指针；未知任务为空。
const data::WildTaskDef* findWildTask(const string& id);
// 用途：检查野外悬赏的地点、状态、月额与费用。
// 参与流程：悬赏报名预览。
// 使用方式：传只读游戏和任务 ID。
// 返回值：拒绝原因；可接取时为空。
string wildActionError(const Game& g, const string& id);
// 用途：占用悬赏月额、支付报名并安排准备工作。
// 参与流程：接取野外悬赏。
// 使用方式：传任务 ID，内部先验证资格。
// 返回值：无；修改月额、灵石、Job 和报名日志。
void societyWildBegin(Game& g, const string& id);

// ============================================================
// 师徒 / 道侣
// ============================================================
// 收徒与结道侣的名额检查
// 用途：判断弟子名额是否仍有空缺。
// 参与流程：收徒动作条件。
// 使用方式：传只读游戏。
// 返回值：弟子数量少于两名时为真。
bool bondCanTakeDisciple(const Game& g);

// 每月授业/相伴的校验；返回空字符串表示可以进行
// 用途：校验授业、教学或共修的名分与月额。
// 参与流程：师门日常前置校验。
// 使用方式：传游戏和 lesson/teach/together 类型。
// 返回值：拒绝原因；本月仍可安排时为空。
string bondMonthlyError(const Game& g, const string& kind);

struct BondActionInfo {
    bool valid = false;
    string id, name, desc, npcId;
    int days = 0;
    int cost = 0;
};
// 用途：生成师门往来的费用、耗时和对象信息。
// 参与流程：师门动作报价与执行。
// 使用方式：传游戏与往来类型，解除名分不需要 NPC 对象。
// 返回值：BondActionInfo；未知动作 valid 为假。
BondActionInfo bondActionInfo(const Game& g, const string& kind);
// 用途：校验师门往来的活动阻塞、资源和同地条件。
// 参与流程：往来安排前置。
// 使用方式：传游戏和往来类型，内部共用月额校验。
// 返回值：拒绝原因；可执行时为空。
string bondActionError(const Game& g, const string& kind);
// 用途：支付费用并建立师门往来工作。
// 参与流程：授业、共修及解除名分动作。
// 使用方式：传游戏与类型，内部先校验。
// 返回值：无；修改灵石、Job 和安排日志。
void societyBondBegin(Game& g, const string& kind);

// ============================================================
// 关系往来的结算（拜师 / 授业 / 收徒 / 道侣）
// ============================================================
// npc 可为空（解除类动作不需要对象）
// 用途：结算师徒、道侣及日常往来的效果。
// 参与流程：社会 Job 完成分派。
// 使用方式：传游戏、类型和可为空的目标；解除类不需目标。
// 返回值：无；修改名分、月额、关系、成长、弟子属性和日志。
void societyBondComplete(Game& g, const string& kind, const Npc* npc);

// 野外悬赏完成时开战
// 用途：把完成准备的悬赏上下文登记为战斗。
// 参与流程：野外悬赏工作到期。
// 使用方式：传游戏和已完成 Job，先携带任务与赏金字段。
// 返回值：无；开始斗法，写入战斗资源、准备和 RNG。
void societyWildStart(Game& g, const Job& job);

// 野外悬赏斗法胜利后的奖励
// 用途：兑现真正获胜的悬赏奖励。
// 参与流程：battleFinish 的野外悬赏回调。
// 使用方式：传完成战斗快照，排除奇遇与无任务上下文。
// 返回值：无；增加灵石、声望、悬赏胜场和适用材料。
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
// 类型：宗门待领俸禄的只读快照；无外部资源，不累计工资或改变府库，Paid/Owed 非负。
struct StipendQuote {
    bool Enabled = false;
    string Reason;
    int Paid = 0;
    int Owed = 0;
};
// 用途：报价本月远程俸禄；参与宗门/纪事展示与动作复核；传只读人生，返回实付、余欠和拒绝原因。
[[nodiscard]] StipendQuote mfQuoteStipend(const Game& State);
// 用途：原子领取报价中的俸禄；参与既有 stipend 路由；传活动人生，返回实付/余欠，拒绝不改变状态。
StipendResult mfCollectStipend(Game& State);
// 用途：从真实宗门库藏领取应付俸禄。
// 参与流程：山门俸禄动作。
// 使用方式：传游戏，要求本月未领且恶名允许，可在宗门外领取；旧公共接口委托 mfCollectStipend。
// 返回值：实付和余欠结果；成功修改府库、玩家灵石及工资账。
StipendResult socCollectStipend(Game& g);

} // namespace nz
