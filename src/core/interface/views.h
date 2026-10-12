#pragma once
#include "core/json.h"
#include "core/model.h"

namespace nz {
struct SpellDef;
struct ArtifactAbility;
namespace shop { struct ActionQuote; }
}

namespace nz::api::view {
// Account-owned facts read by the API boundary. Views never bind a session,
// access saves, advance a clock, or consume randomness.
struct ViewContext { bool endingClaimed = false; };
json::Value state(const Game& game, const ViewContext& context = {});
// 用途：生成当前斗法双方状态与动作预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value battle(const Game& game);
// 用途：生成单件法器主动与被动效果预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value equipment(const Player& player, const Gear& gear);
// 用途：计算指定神通的实际费用数值与状态效果。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const SpellDef& spell，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value spellPreview(const Player& player, const SpellDef& spell);
// 用途：编码法宝主动神通与境界费用和真实预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const ArtifactAbility& ability，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value artifactView(const Player& player, const ArtifactAbility& ability);
// 用途：比较装备替换前后被动数值变化。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Player& player, const Gear& gear，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value artifactPassiveView(const Player& player, const Gear& gear);
// 用途：叠加路由状态阻塞并编码领域动作报价。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const std::string& path, const shop::ActionQuote& quote，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
json::Value actionQuote(const Game& game, const std::string& path, const shop::ActionQuote& quote);
// 用途：追加角色基础属性进阶门槛与丹毒状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendPlayerState(const Game& game, json::Value& state);
// 用途：追加功法神通悟道目录与实际状态。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendArtsState(const Game& game, json::Value& state);
// 用途：追加天下行情人口任务与地点数据。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendWorldState(const Game& game, json::Value& state);
// 用途：追加可见道友关系消息与交互预览。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendSocialState(const Game& game, json::Value& state);
// 用途：追加交易制作目录服药效果和物品词表。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEconomyState(const Game& game, json::Value& state);
// 用途：追加行囊法器及其动作和拆解报价。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEquipmentState(const Game& game, json::Value& state);
} // namespace nz::api::view
