#pragma once

#include <string_view>
#include "core/arts.h"

namespace nz::content {
// 用途：集中提供八种进阶完整构筑；流程：斗法试验、流派推荐与 NPC 传承指引。
// 用法：读取静态目录；六宗直接引用完整 NPC 模板，剑与神识为玩家配装建议。
// 返回：拥有自身字符串的不可变 BuildInfo 列表；不修改游戏、时钟、RNG 或存档。
const vector<BuildInfo>& mfAdvancedBuilds();
// 用途：按既有稳定 buildId 查进阶构筑；流程：测试开局和传承展示。
// 用法：传如 metal-discard；返回：借用静态目录的指针，未知 ID 为 null。
const BuildInfo* mfFindAdvancedBuild(std::string_view BuildId);
} // namespace nz::content
