#pragma once
#include "core/model.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace nz::crafting {
// 升级材料报价值：仅拥有显示和校验所需数据，不拥有游戏状态或资源。
struct GreenBottleMaterialQuote {
    std::string Id, Name;
    int Required = 0, Owned = 0, Missing = 0;
};
// 永久品阶升级统一报价：先校验当前阶位再升级一级，动作及界面共用。
struct GreenBottleUpgradeQuote {
    bool Enabled = false;
    std::string Reason;
    int TargetGrade = 0, StoneCost = 0, OwnedStones = 0;
    std::vector<GreenBottleMaterialQuote> Materials;
};
// 专属秘境入口报价值：只描述真实秘境，不在查询中生成site或扣款。
struct GreenBottleDungeonQuote {
    bool Enabled = false;
    std::string Reason, Name, MaterialId, MaterialName;
    int Grade = 0, MinimumBottleGrade = 0, Stage = 0, RewardCount = 0;
};
// 用途：取得持有天赋者的真实日增；流程：灵田报价/日结算；返回：0..700，无天赋恒0，不改变阶位。
int mfGreenBottleDailyBonus(const Player& Owner);
// 用途：校验永久阶位范围与天赋归属；流程：保存/动作；返回：错误或空，无迁移副作用。
std::string mfGreenBottleStateError(const Player& Owner);
// 用途：统一专属灵材入库容量；流程：入口报价/离境兑现/在途保存；返回：错误或空，未知ID/数量/溢出明确拒绝，不改背包。
std::string mfGreenBottleMaterialCapacityError(const Player& Owner, std::string_view MaterialId, int RewardCount);
// 用途：统一升一级报价；流程：洞府视图与执行；返回：资格、费用、专属材料缺量，不写状态。
GreenBottleUpgradeQuote mfQuoteGreenBottleUpgrade(const Game& State);
// 用途：按当前洞府自然日增预估六品成熟天数；流程：当前/下一阶比较；返回：六项天数，参数非法明确抛错。
std::array<int, 6> mfGreenBottleMaturityDays(int CaveTier, int BottleGrade);
// 用途：按最新报价永久升级一级；流程：洞府动作；参数ExpectedGrade防重复或过期请求；返回：错误或空，拒绝无部分扣费。
std::string mfUpgradeGreenBottle(Game& State, int ExpectedGrade);
// 用途：查询该阶一次通关应得的灵材；流程：报价、入场冻结和存档校验；用法：传材料品阶；返回：与升阶需求相同的数量，未知阶位为0。
int mfGreenBottleMaterialRewardCount(int MaterialGrade);
// 用途：报价瓶阶解锁的专属秘境；流程：洞府入口/实际进入；返回：固定难度、资格和一次升阶足量目标。
GreenBottleDungeonQuote mfQuoteGreenBottleDungeon(const Game& State, int Grade);
// 用途：列出已解锁的三类秘境入口；流程：洞府纯查询；返回：仅持有天赋可见，满六阶保留六阶入口。
std::vector<GreenBottleDungeonQuote> mfGreenBottleDungeonOptions(const Game& State);
// 用途：进入新一轮真实爬塔；流程：专属秘境动作；返回：错误或空，无门票费，失败不生成site/消耗序号。
std::string mfEnterGreenBottleDungeon(Game& State, int Grade, int ExpectedGrade);
}
