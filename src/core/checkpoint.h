#pragma once

#include "core/model.h"

namespace nz {

// 会话入口显式启用；普通 createGame 保持无副作用，便于测试与构建预览。
// 用途：启用并初始化十年自动存档。
// 参与流程：十年自动存档、手动存档与死亡恢复。
// 使用方式：传入Game& game，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void checkpointEnable(Game& game);
// 每满十年在下一安全时刻更新恢复点，返回是否生成了新快照。
// 用途：在跨十年且满足状态时更新自动存档。
// 参与流程：十年自动存档、手动存档与死亡恢复。
// 使用方式：传入Game& game，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool checkpointTick(Game& game);
// 已成功写入手动存档后更新恢复点；合法手动档位仅为 1 至 3。
// 用途：记录已验证的手动存档检查点。
// 参与流程：十年自动存档、手动存档与死亡恢复。
// 使用方式：传入Game& game, int slot，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void checkpointManual(Game& game, int slot);
// 还原最近存档，保留自动存档周期和恢复计数，不再次触发死亡结算。
// 用途：校验检查点并恢复意外死亡前的角色。
// 参与流程：十年自动存档、手动存档与死亡恢复。
// 使用方式：传入Game& game, const string& reason，按本模块入口调用。
// 返回值：操作或条件是否成立；失败条件在调用前校验。
bool checkpointRestore(Game& game, const string& reason);

} // namespace nz
