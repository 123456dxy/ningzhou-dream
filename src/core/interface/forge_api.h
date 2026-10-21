#pragma once
#include "core/model.h"
#include "core/json.h"

namespace nz::api {
// 只读协议编码与严格输入翻译；规则由 crafting 拥有，不保存状态或推进时间。
json::Value mfForgeOptionsView(const Game& State);
json::Value mfForgePreviewView(const Game& State, const json::Value& Input);
string mfBeginForgeRequest(Game& State, const json::Value& Input, int& DurationDays);
string mfValidateArtifactTargets(const Battle& State, const json::Value& Input, vector<string>& Targets);
string mfValidateArtifactStatusSelection(const Player& Actor, const Battle& State, const json::Value& Input);
}
