#pragma once

#include "core/model.h"

namespace nz {
// 单状态报价沿用战斗的持久值；层数适配只用于显示与有限清除。
struct ArtifactStatusTarget {
    string Id, Name, Unit, Description;
    double Layers = 0, RemoveLayers = 0, RemainingLayers = 0;
};

[[nodiscard]] bool mfArtifactSelectsStatus(const Gear& Item);
[[nodiscard]] int mfArtifactStatusRemovalLimit(const Gear& Item);
[[nodiscard]] vector<ArtifactStatusTarget> mfArtifactStatusTargets(const Battle& State, const Gear& Item);
// 纯状态收据，用于拒绝选择后已发生变化的操作，不包含随机或外部资源。
[[nodiscard]] string mfArtifactStatusToken(const Battle& State, const Gear& Item);
// 调用方必须先通过最新动作报价；本函数只处理一种已存在的状态。
[[nodiscard]] string mfApplyArtifactStatusRemoval(Battle& State, const Gear& Item, const string& TargetId);
} // namespace nz
