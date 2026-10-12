#pragma once
#include "core/model.h"
namespace nz {
struct NpcLesson {
    string kind, id, name, dao, reason;
    int stage = 0, days = 2, creditCost = 0, stoneCost = 0, affinityNeed = 20;
    bool learned = false, enabled = false;
};
// 用途：列出目标可传内容及玩家当前的学习资格。
// 参与流程：NPC 传授面板与开始请教。
// 使用方式：传只读游戏和修士 ID，检查好感、资源、同地和月额。
// 返回值：带已学、可用及拒绝原因的候选列表，不改游戏。
vector<NpcLesson> npcLessons(const Game& g, const string& npcId);
// 用途：支付交情与学费并安排真实授业时间。
// 参与流程：NPC 请教动作入口。
// 使用方式：传老师 ID、spell/manual 与内容 ID，按候选重新校验。
// 返回值：成功为空并更新资源、月额、Job 和日志；失败为原因。
string npcLearnBegin(Game& g, const string& npcId, const string& kind, const string& id);
// 用途：到期时校验老师、内容和费用并只结算一次。
// 参与流程：请教 Job 完成回调。
// 使用方式：传真实完成工作；目标失效或未完成时退款。
// 返回值：无；登记结算标记，更新已知神通/功法、关系和悟道或退款。
void npcLearnComplete(Game& g, const Job& job);
// 用途：取得兼容人物原型。
// 参与流程：按玩家身份、性别及宗门选图。
// 使用方式：主角或无稳定身份的人物调用。
// 返回值：立绘资源路径。
string personPortrait(const string& gender, const string& sect, bool player = false);
// 用途：稳定选择人类 NPC 立绘。
// 参与流程：对身份做固定哈希后按宗门、性别选原型或变体。
// 使用方式：侧栏与战斗传同一 NPC id。
// 返回值：立绘资源路径。
string npcPortrait(const string& npcId, const string& gender, const string& sect);
// 用途：按 NPC 实体统一选图。
// 参与流程：妖兽沿用境界图，人类调用稳定身份选择。
// 使用方式：社交列表传入现有 NPC。
// 返回值：立绘资源路径。
string npcPortrait(const Npc& npc);
// 用途：取得妖兽原型。
// 参与流程：按境界分组并限制到已有图池。
// 使用方式：妖兽侧栏或战斗调用。
// 返回值：妖兽立绘资源路径。
string beastPortrait(int stage);
}
