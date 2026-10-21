#pragma once

#include "core/content/game_ids.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nz {
struct DungeonRun;
struct SpellDef;

enum class DungeonCardRewardMode { All, None, Selected };
struct DungeonCardRewardPreference {
    DungeonCardRewardMode Mode = DungeonCardRewardMode::All;
    std::vector<content::DaoId> Daos;
};

// 用途：解析临时卡奖励模式；流程：入场偏好边界；用法：传all/none/selected；返回：白名单模式，未知为空。
std::optional<DungeonCardRewardMode> mfParseDungeonCardRewardMode(std::string_view Mode);
// 用途：输出模式稳定协议键；流程：存档与视图；用法：传模式；返回：对应ASCII键，未知枚举为空。
std::string_view mfDungeonCardRewardModeKey(DungeonCardRewardMode Mode);
// 用途：列出真正存在非普攻防御神通的大道；流程：入场单系选择和领域校验共用；用法：只读查询；返回：按注册顺序排列的白名单副本，不推进RNG。
std::vector<content::DaoId> mfDungeonCardRewardDaos();
// 用途：校验冻结临时卡偏好组合，保留先前多系在途探索；流程：读档与奖励池校验；用法：传偏好；返回：错误或空，不改变任何状态。
std::string mfValidateDungeonCardRewardPreference(const DungeonCardRewardPreference& Preference);
// 用途：要求新探索仅选单系或全部/不出；流程：普通与绿瓶入场边界；用法：传待冻结偏好；返回：错误或空，拒绝多系且不修改输入或游戏状态。
std::string mfValidateDungeonCardRewardEntryPreference(const DungeonCardRewardPreference& Preference);
// 用途：判断一张临时奖励牌是否符合入场偏好；流程：战胜/商人/奇遇发牌与校验；用法：传冻结探索和目录神通；返回：是否允许，不检查初始携带牌或永久终奖。
bool mfDungeonCardRewardAllowed(const DungeonRun& Run, const SpellDef& Spell);
} // namespace nz
