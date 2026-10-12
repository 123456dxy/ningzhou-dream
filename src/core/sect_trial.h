#pragma once

#include "core/model.h"

namespace nz {

struct SectTrialDef {
    string sect, name, desc, style, manual, spell;
    int minStage = 0, fee = 10, days = 3;
};

// 用途：提供五宗入门考核与奖励配置。
// 参与流程：考核目录和报名。
// 使用方式：无参数，读取固定定义。
// 返回值：考核定义常量列表引用。
const vector<SectTrialDef>& sectTrialDefs();
// 用途：按宗门 ID 查入门考核。
// 参与流程：考核校验与战后奖励。
// 使用方式：传宗门 ID。
// 返回值：考核定义指针；不存在时为空。
const SectTrialDef* findSectTrial(const string& sect);
// 返回空字符串表示可在本宗山门报名；失败请求完全不改变游戏。
// 用途：校验报名身份、地点、声望、性别和资源。
// 参与流程：入门考核开始前。
// 使用方式：传只读游戏与宗门 ID，不创建报名记录。
// 返回值：拒绝原因；可报名时为空。
string sectTrialError(const Game& g, const string& sect);
// 用途：登记唯一考核、收费并开始真实试炼。
// 参与流程：宗门入门动作。
// 使用方式：传游戏与宗门，先校验，先登记耗时再开战。
// 返回值：成功为空；建立标记、Job 和 Battle，失败为原因。
string sectTrialBegin(Game& g, const string& sect);
// 必须使用 battleFinish 保留的完整快照，重复结算不发奖励。
// 用途：按真实考核胜负决定入宗并只奖励一次。
// 参与流程：battleFinish 的考核回调。
// 使用方式：传保留快照、胜负和撤离标记，核对报名序号。
// 返回值：无；清活动标记，胜利时入宗、给传承并初始化俸禄。
void sectTrialBattleComplete(Game& g, const Battle& battle, bool won, bool escaped = false);
// 旧档 join 工作完成后转为实战考核，不能自动入宗或传送。
// 用途：把旧版入宗安排迁移成真实考核。
// 参与流程：旧 join Job 完成兼容。
// 使用方式：传旧工作，已付旧费先退再调用考核报名。
// 返回值：无；更新灵石、考核工作和日志，不直接入宗。
void sectTrialLegacyComplete(Game& g, const Job& job);
// 用途：校验考核标记、工作与斗法的交叉一致性。
// 参与流程：导入和存档状态完整性检查。
// 使用方式：传游戏，不修复或改写状态。
// 返回值：错误原因；记录一致时为空。
string sectTrialStateError(const Game& g);

// 师门入口与 NPC 往来共用同一份拜师条件；候选在外游历时仍可查看，必须当面拜师。
// 用途：筛选本门存活且境界更高的师长。
// 参与流程：师门候选目录。
// 使用方式：传游戏，优先现任师尊、同地和较好关系。
// 返回值：排序后的 NPC 常量指针列表。
vector<const Npc*> sectMentorCandidates(const Game& g);
// 用途：检查本门当面拜师的统一条件。
// 参与流程：NPC 往来和师门拜师入口。
// 使用方式：传目标 ID，共用 interactionError、好感、声望和礼资条件。
// 返回值：拒绝原因；可拜师时为空。
string sectMasterError(const Game& g, const string& npcId);

struct SectTaskDef {
    string id, name, desc;
    int days = 0, fee = 0, stage = 0, contribution = 0, daoExp = 0;
};
// 用途：提供本门差事的费用、贡献和道经验定义。
// 参与流程：宗门工作目录与实际报价。
// 使用方式：无参数，读取固定任务表。
// 返回值：任务定义常量列表引用。
const vector<SectTaskDef>& sectTaskDefs();
// 用途：按动作 ID 查宗门差事。
// 参与流程：经济工作报价与月额核算。
// 使用方式：传任务 ID。
// 返回值：任务定义指针；未知 ID 为空。
const SectTaskDef* findSectTask(const string& id);
// 新旧宗门差事共用每月三件额度；接下即占额度，中止不能重刷。
// 用途：合计所有本门差事已占用的月额度。
// 参与流程：每月三件宗门差事限制。
// 使用方式：传游戏，月份变化视为零，合计时防整数溢出。
// 返回值：当前月已接任务总数。
int sectTasksTakenThisMonth(const Game& g);

} // namespace nz
