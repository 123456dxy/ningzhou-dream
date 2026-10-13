#include "harness.h"

#include <array>
#include <set>
#include <string>

#include "core/resource_points.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
namespace {

// 用途：核验资源点拒绝路径不改变任何已序列化游戏状态。
// 参与流程：关闭模块与误请求启用的隔离回归。
// 使用方式：传合法游戏、应用配置快照、动作和请求标签；不访问文件。
// 返回值：无；失败记录到现有测试计数，包含时钟、随机数与日志断言。
void mfCheckRejectedAction(nz::Game& ActiveGame, const nz::ResourcePoints::Config& ConfigSnapshot,
                           const nz::ResourcePoints::ActionRequest& Request,
                           const std::string& Label) {
    const auto BeforeSave = nz::saveToJson(ActiveGame);
    const auto BeforeRandomState = ActiveGame.rng;
    const auto BeforeDays = ActiveGame.clock.days;
    const auto BeforeLogCount = ActiveGame.logs.size();
    const auto Result = nz::ResourcePoints::mfTryAction(ActiveGame, ConfigSnapshot, Request);
    const auto ExpectedStatus = ConfigSnapshot.Enabled ? nz::ResourcePoints::Availability::Unavailable
                                                       : nz::ResourcePoints::Availability::Disabled;
    check(Result == ExpectedStatus, Label + "明确拒绝而非伪造成功");
    checkStr(nz::saveToJson(ActiveGame), BeforeSave, Label + "整个存档状态完全不变");
    checkEq(ActiveGame.rng, BeforeRandomState, Label + "不消耗共享随机数");
    checkEq(ActiveGame.clock.days, BeforeDays, Label + "不推进游戏日历");
    checkEq(ActiveGame.logs.size(), BeforeLogCount, Label + "不写活动纪事");
}

} // namespace

// 用途：核验资源点骨架的双闸门、稳定目录与零副作用契约。
// 参与流程：统一核心自测；由测试总入口调用。
// 使用方式：创建隔离角色，反复查询及尝试动作，不读写玩家存档。
// 返回值：无；失败记录到现有测试计数。
void mfRunResourcePointTests() {
    using namespace nz::ResourcePoints;
    std::printf("\n--- 资源点默认关闭与未就绪隔离 ---\n");

    const Config DefaultConfig;
    const auto DefaultCapability = mfCapability(DefaultConfig);
    check(!DefaultConfig.Enabled, "资源点部署配置默认关闭");
    check(!GameplayReady, "资源点骨架保持未就绪硬闸门");
    check(!DefaultCapability.Requested && !DefaultCapability.Available &&
              !DefaultCapability.MutationsAllowed && DefaultCapability.Status == Availability::Disabled,
          "默认能力查询明确关闭且禁止所有动作");
    checkStr(std::string(mfStatusId(DefaultCapability.Status)), "disabled", "关闭状态使用稳定机器标识");

    const Config RequestedConfig{true};
    const auto RequestedCapability = mfCapability(RequestedConfig);
    check(RequestedCapability.Requested && !RequestedCapability.Available &&
              !RequestedCapability.MutationsAllowed && RequestedCapability.Status == Availability::Unavailable,
          "误请求启用仍被就绪闸门拒绝");
    checkStr(std::string(mfStatusId(RequestedCapability.Status)), "unavailable", "未就绪状态不会冒充可用");

    const auto Catalog = mfDevelopmentCatalog();
    std::set<std::string> UniqueIds;
    std::set<PointKind> PlannedKinds;
    for (const auto& Definition : Catalog) {
        check(UniqueIds.insert(std::string(Definition.Id.StableValue())).second,
              "开发目录资源点 ID 唯一");
        check(nz::findLoc(std::string(Definition.LocationId)) != nullptr, "资源点锚定真实宁州地点");
        check(!Definition.DisplayName.empty(), "开发目录具备集中中文展示名");
        const auto Parsed = mfParsePointId(Definition.Id.StableValue());
        check(Parsed && Parsed->StableValue() == Definition.Id.StableValue(), "资源点强类型 ID 往返一致");
        PlannedKinds.insert(Definition.Kind);
    }
    checkEq(PlannedKinds.size(), 4, "预案覆盖城池经营、药田、矿脉与灵脉四类");
    check(!mfParsePointId("").has_value(), "空资源点标识明确拒绝");
    check(!mfParsePointId("wuling").has_value(), "地点 ID 不能冒充资源点 ID");
    check(!mfParsePointId("武陵城·经营权").has_value(), "中文展示名不能冒充协议 ID");
    check(!mfParsePointId("unknown.point").has_value(), "未知资源点标识明确拒绝");

    nz::CreateOptions Options;
    Options.name = "资源点隔离测试者";
    Options.seed = 313373;
    auto ActiveGame = nz::createGame(Options);
    const auto BeforeQueries = nz::saveToJson(ActiveGame);
    for (const auto& ConfigSnapshot : std::array<Config, 2>{DefaultConfig, RequestedConfig}) {
        for (int QueryCount = 0; QueryCount < 12; ++QueryCount) {
            check(mfVisiblePoints(ActiveGame, ConfigSnapshot).empty(), "关闭及未就绪时没有玩家资源点入口");
            const auto CapabilitySnapshot = mfCapability(ConfigSnapshot);
            check(!CapabilitySnapshot.Available && !CapabilitySnapshot.MutationsAllowed,
                  "反复查询不解锁骨架功能");
        }
    }
    checkStr(nz::saveToJson(ActiveGame), BeforeQueries, "重复纯查询不初始化状态或改变世界及随机序列");

    const auto KnownPoint = mfParsePointId("wuling.trade_right");
    check(KnownPoint.has_value(), "测试动作使用目录内受控 ID");
    const std::array<ActionKind, 5> Actions{ActionKind::Claim, ActionKind::Challenge, ActionKind::Defend,
                                           ActionKind::Collect, ActionKind::Abandon};
    for (const auto& ConfigSnapshot : std::array<Config, 2>{DefaultConfig, RequestedConfig}) {
        for (const auto Action : Actions) {
            mfCheckRejectedAction(ActiveGame, ConfigSnapshot, ActionRequest{Action, KnownPoint},
                                  ConfigSnapshot.Enabled ? "未就绪资源点动作" : "关闭资源点动作");
        }
        mfCheckRejectedAction(ActiveGame, ConfigSnapshot, ActionRequest{ActionKind::Claim, std::nullopt},
                              "缺少资源点的动作");
        mfCheckRejectedAction(ActiveGame, ConfigSnapshot,
                              ActionRequest{static_cast<ActionKind>(999), KnownPoint}, "未知动作类别");
    }

    ActiveGame.ended = true;
    mfCheckRejectedAction(ActiveGame, DefaultConfig, ActionRequest{ActionKind::Collect, KnownPoint},
                          "已结算人生中的关闭动作");
    checkStr(std::string(mfStatusId(static_cast<Availability>(999))), "invalid", "非法状态枚举不会映射成成功");
}

} // namespace nztest
