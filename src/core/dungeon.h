#pragma once
#include "core/model.h"

namespace nz {

struct DungeonRelicDef {
    string id, name, desc;
    int mana = 0, draw = 0, heal = 0;
    double shield = 0;
};
struct DungeonIntelOption {
    string kind, id, name, desc;
    int grade = 1, stage = 0, cost = 0;
    bool enabled = false;
    string reason;
};

// 扫荡报价只描述现有入口；RequiredStage可能为15，表示当前最高境界仍无法越境扫荡。
struct DungeonSweepQuote {
    string SiteId, Reason;
    int SiteStage = 0, RequiredStage = 3, Days = 1;
    bool Enabled = false;
};

// 用途：提供秘境临时遗物及其收益定义。
// 参与流程：秘境奖励、战斗资源与战后恢复。
// 使用方式：无参数，读取固定表。
// 返回值：DungeonRelicDef 列表常量引用。
const vector<DungeonRelicDef>& dungeonRelics();
// 用途：首次初始化秘境世界并生成当前入口。
// 参与流程：建号与秘境动作入口。
// 使用方式：传游戏，initialized 标记防重复生成。
// 返回值：无；修改初始化标记、序号、入口和冻结奖励。
void ensureDungeons(Game& g);
// 旧版缺少永久奖励字段时，在存档状态校验前确定性补齐。
// 用途：为缺永久奖励记录的旧秘境补齐冻结字段。
// 参与流程：读档状态校验之前。
// 使用方式：传游戏，使用局部种子；已存在奖励时保留。
// 返回值：无；更新旧 site/run 奖励及相关领取/目标字段。
void migrateDungeonRewards(Game& g);
// 用途：维护周期生成、特殊窗口和过期入口。
// 参与流程：岁末秘境世界结算。
// 使用方式：传游戏，未初始化时先补入口。
// 返回值：无；更新秘境列表、生成记录及冻结奖励。
void dungeonYearTick(Game& g);
// 用途：检查入口有效期、身份、地点与境界。
// 参与流程：秘境进入预览。
// 使用方式：传游戏和 site，先检查活动阻塞。
// 返回值：拒绝原因；可进入时为空。
string dungeonEnterError(const Game& g, const DungeonSite& site);
// 用途：判断节点是否沿当前路线下一层可达。
// 参与流程：路线展示与选择校验。
// 使用方式：传 run 与节点 ID，要求 route 阶段且未访问。
// 返回值：可以沿连线进入该节点时为真。
bool dungeonNodeAvailable(const DungeonRun& run, const string& nodeId);
// 用途：进入有效秘境并冻结本次探索状态。
// 参与流程：秘境进入动作。
// 使用方式：传 site ID，内部校验后复制奖励和构建临时牌组/路线。
// 返回值：成功为空并建立 run/入场标记；失败为原因。
string dungeonEnter(Game& g, const string& siteId);
// 用途：验证并冻结本次临时卡奖励偏好；流程：秘境目录进入；用法：传入口和偏好，非法偏好在初始化/入场前拒绝；返回：错误或空，永久奖励和携带牌不变。
string mfEnterDungeonWithCardRewards(Game& State, const string& SiteId, const DungeonCardRewardPreference& Preference);
// 用途：按入口大境界与真实入场资格报价一键扫荡；流程：目录按钮和实际命令共用。
// 使用：传现有site ID，不生成入口、时间或RNG；返回：资格/原因及离境结算的一天耗时。
DungeonSweepQuote mfQuoteDungeonSweep(const Game& State, const string& SiteId);
// 用途：越大境界扫荡并停在原最终奖励选择；流程：秘境目录动作。
// 使用：传现有site ID，候选副本通过完整校验才提交；返回：成功为空，失败不改变资源/进度。
string mfSweepDungeon(Game& State, const string& SiteId);
// 用途：按已验证瓶阶创建并进入一次专属灵材秘境；流程：green_bottle命令；返回：错误或空，真实爬塔/结算，无额外门票和人物境界锁。
string mfOpenBottleMaterialDungeon(Game& State, int MaterialGrade);
// 用途：以同一入场偏好开启瓶境；流程：洞府专属材料秘境；用法：传材料品阶和临时卡偏好；返回：错误或空，非法偏好不会生成入口或改变状态。
string mfOpenBottleMaterialDungeon(Game& State, int MaterialGrade, const DungeonCardRewardPreference& Preference);
// 用途：进入可达节点并开始战斗或形成节点奖励。
// 参与流程：探索路线选择动作。
// 使用方式：传游戏和 node ID，战斗上下文先登记再调用 battleStart。
// 返回值：成功为空；更新 run、累计耗时及必要战斗，失败为原因。
string dungeonChoose(Game& g, const string& nodeId);
// 用途：领取或跳过当前合法节点奖励。
// 参与流程：秘境抉择、商人和永久首领奖励。
// 使用方式：传 reward ID，永久奖励依据入场冻结值并去重。
// 返回值：成功为空并更新临时行囊/牌组或永久传承；失败为原因。
string dungeonClaim(Game& g, const string& rewardId);
// 用途：在非斗法阶段主动离境兑现已有收获。
// 参与流程：秘境退出动作。
// 使用方式：传游戏，通关后需先选永久奖励。
// 返回值：成功为空并带回全部行囊、结算耗时；失败为原因。
string dungeonLeave(Game& g);
// 身亡或外部中断时丢弃临时物资，不制造额外日历安排。
// 用途：外部中断时丢弃尚未兑现的探索物资。
// 参与流程：角色身亡或强制中断。
// 使用方式：传游戏，不制造额外日历工作。
// 返回值：无；移除未通关入口并清 dungeons.run。
void dungeonAbandon(Game& g);
// 用途：报价高阶定向材料秘境情报。
// 参与流程：天机阁地点服务目录。
// 使用方式：传只读游戏，检查位置、月额和灵石；stage保留探索最低境界，购买不设境界限制。
// 返回值：情报选项与门槛列表，不生成入口或消费资源。
vector<DungeonIntelOption> dungeonIntelOptions(const Game& g);
// 用途：支付情报价款并生成定向材料秘境。
// 参与流程：地点服务情报兑换动作。
// 使用方式：传 herb/material 与目标 ID，按当前报价再次校验。
// 返回值：成功为空并修改灵石、月额、site 和日志；失败为原因。
string dungeonIntel(Game& g, const string& kind, const string& id);
// 用途：将节点战斗胜负接回秘境探索。
// 参与流程：battleFinish 秘境回调。
// 使用方式：传完成快照，核对 site/node；战败或撤离结束探索。
// 返回值：无；更新健康、收获、节点阶段，或折半带出物资并离境。
void dungeonBattleComplete(Game& g, const Battle& battle, bool won);
// 用途：校验秘境入口、路线、牌组与奖励的完整性。
// 参与流程：读档与导入状态验证。
// 使用方式：传游戏，只读逐项检查冻结字段及跨模块一致性。
// 返回值：第一处错误原因；合法状态为空。
string dungeonStateError(const Game& g);

} // namespace nz
