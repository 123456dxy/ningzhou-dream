#pragma once

#include "core/model.h"

namespace nz {

// 会话入口显式启用；普通 createGame 保持无副作用，便于测试与构建预览。
void checkpointEnable(Game& game);
// 每满十年在下一安全时刻更新恢复点，返回是否生成了新快照。
bool checkpointTick(Game& game);
// 已成功写入手动存档后更新恢复点；合法手动档位仅为 1 至 3。
void checkpointManual(Game& game, int slot);
// 还原最近存档，保留自动存档周期和恢复计数，不再次触发死亡结算。
bool checkpointRestore(Game& game, const string& reason);

} // namespace nz
