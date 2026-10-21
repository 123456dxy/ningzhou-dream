#pragma once

#include "core/exploration/dungeon_card_preferences.h"
#include "core/json.h"
#include <string>

namespace nz {
struct Game;
namespace api {
// 用途：解析可选入场奖励偏好；流程：普通/绿瓶秘境请求共用；用法：传完整请求对象；返回：错误或空，缺字段默认全部，不修改游戏。
std::string mfReadDungeonCardRewardPreference(const json::Value& Input, DungeonCardRewardPreference& Preference);
// 用途：校验普通秘境入场协议后委托领域；流程：dungeon/enter；用法：传游戏与原请求；返回：错误或空，非法输入在入场前拒绝。
std::string mfExecuteDungeonEnter(Game& State, const json::Value& Input);
} // namespace api
} // namespace nz
