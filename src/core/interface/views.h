#pragma once
#include "core/json.h"
#include "core/model.h"

namespace nz {
struct SpellDef;
struct ArtifactAbility;
namespace shop { struct ActionQuote; struct MarketSaleContext; }
namespace legacy { struct Archive; }
}

namespace nz::api::view {
// Account-owned facts read by the API boundary. Views never bind a session,
// access saves, advance a clock, or consume randomness.
struct ViewContext { bool endingClaimed = false; };
json::Value state(const Game& game, const ViewContext& context = {});
// 用途：展示成就真实条件、进度与传承奖励，保持既有成就协议字段。
// 流程：成就只读查询；Game 与 Archive 仅在调用期间借用，未知定义抛错。
// 返回值：拥有数据的 JSON；不修改人生、传承、时间或 RNG，也不读写存档。
json::Value mfAchievementsView(const Game& GameState, const legacy::Archive& ArchiveState);
// 用途：完整状态与独立查询共用兽潮视图；只读，不重投积分或库存。
json::Value tideState(const Game& game);
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
// 用途：同次完整状态响应复用有限收购上下文；无活动状态/时间/RNG副作用。
void mfAppendSocialState(const Game& GameState, json::Value& State, const shop::MarketSaleContext& MarketContext);
// 用途：追加交易制作目录服药效果和物品词表。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEconomyState(const Game& game, json::Value& state);
// 用途：同次目录响应复用单次预算；旧appendEconomyState保留为兼容薄适配。
void mfAppendEconomyState(const Game& GameState, json::Value& State, const shop::MarketSaleContext& MarketContext);
// 用途：追加行囊法器及其动作和拆解报价。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, json::Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEquipmentState(const Game& game, json::Value& state);
// 用途：同次装备响应复用单次预算；旧appendEquipmentState保留为兼容薄适配。
void mfAppendEquipmentState(const Game& GameState, json::Value& State, const shop::MarketSaleContext& MarketContext);
} // namespace nz::api::view
