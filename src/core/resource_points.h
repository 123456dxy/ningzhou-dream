#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace nz {
struct Game;
}

namespace nz::ResourcePoints {

// 模块：资源点；抽象：应用部署配置。拥有可复制值，无外部资源或游戏引用。
// 默认关闭是合法状态；仅应用入口组装，不从角色存档或前端请求启用。
struct Config {
    bool Enabled = false;
};

// 就绪闸门独立于部署开关；本次骨架没有占领、守卫或结算实现。
// 开始正式实现时必须连同下方契约和测试一并替换，不能只改部署参数。
inline constexpr bool GameplayReady = false;

enum class Availability { Disabled, Unavailable };
enum class PointKind { CityFranchise, HerbField, OreMine, SpiritVein };
enum class OwnerKind { Unclaimed, TownCouncil, Sect, Player };
enum class ActionKind { Claim, Challenge, Defend, Collect, Abandon };

// 模块：资源点；抽象：只读能力快照。拥有简单值，可跨线程复制。
// Requested 仅表示部署请求；Available 与 MutationsAllowed 为假时不开放入口。
struct Capability {
    bool Requested = false;
    bool Available = false;
    bool MutationsAllowed = false;
    Availability Status = Availability::Disabled;
};

struct PointDefinition;

// 模块：资源点；抽象：已经目录验证的强类型资源点 ID。
// 构造仅允许受控目录或解析入口；持有静态词表视图，无分配、IO或外部引用。
// 有效 ID 可复制且整个进程内有效，不能由地点 ID 或中文展示名隐式构造。
class PointId {
public:
    // 用途：取得资源点的稳定协议标识。
    // 参与流程：目录解析、调试与将来的协议边界。
    // 使用方式：调用有效 PointId；视图指向不可变静态目录，可并行读取。
    // 返回值：进程生命周期内有效的 ASCII 标识；无副作用、无异常。
    [[nodiscard]] constexpr std::string_view StableValue() const noexcept { return IdText; }

private:
    // 用途：仅为受控静态目录构造已知资源点 ID。
    // 参与流程：开发目录的编译期初始化。
    // 使用方式：仅友元目录函数可传静态 ASCII 文本，不接收外部输入。
    // 返回值：有效值对象；不分配资源、不保留调用方缓冲区。
    constexpr explicit PointId(std::string_view StableText) noexcept : IdText(StableText) {}

    std::string_view IdText;
    friend std::span<const PointDefinition> mfDevelopmentCatalog() noexcept;
};

// 模块：资源点；抽象：研发预案目录，不是玩家状态或可领取的奖励。
// 所有视图由不可变静态词表拥有；地点和门派键沿用既有公共目录协议。
// 同一地点可锚定不同资源点，资源点 ID 唯一；不包含未经确定的产出数值。
struct PointDefinition {
    PointId Id;
    std::string_view LocationId;
    std::string_view DisplayName;
    PointKind Kind;
    OwnerKind SuggestedOwner;
    std::string_view SuggestedSectId;
    bool HasLegacyOwnership = false;
};

// 模块：资源点；抽象：尚未开放的动作请求值，不持有游戏状态。
// Point 为空是合法的待校验请求；骨架阶段所有请求都拒绝，不建立任务。
struct ActionRequest {
    ActionKind Kind = ActionKind::Claim;
    std::optional<PointId> Point;
};

// 用途：区分部署关闭与请求开启但实现未就绪。
// 参与流程：应用启动配置与只读模式能力展示。
// 使用方式：传不可变配置快照；可并行、无阻塞、不保存参数引用。
// 返回值：拥有值的能力快照；当前始终不可用，无游戏、时间、RNG或IO副作用。
[[nodiscard]] Capability mfCapability(const Config& ConfigSnapshot) noexcept;

// 用途：输出能力状态的稳定机器标识，不按中文文案分支。
// 参与流程：应用只读能力视图和测试。
// 使用方式：传状态枚举，可并行调用；未知枚举明确返回 invalid。
// 返回值：静态 disabled/unavailable/invalid 文本；无分配、阻塞或副作用。
[[nodiscard]] std::string_view mfStatusId(Availability Status) noexcept;

// 用途：提供独立资源点的研发预案目录。
// 参与流程：内部设计验证与测试，禁止直接作为玩家地图入口。
// 使用方式：无需参数；静态只读目录可并行访问，不触及 Game。
// 返回值：进程生命周期有效的不可变 span；目录存在不表示玩法可用。
[[nodiscard]] std::span<const PointDefinition> mfDevelopmentCatalog() noexcept;

// 用途：把外部标识校验为目录内的强类型资源点 ID。
// 参与流程：内部请求边界与目录校验；当前不注册 HTTP 动作。
// 使用方式：传 ASCII 标识，仅调用期借用输入；可并行，无IO及分配。
// 返回值：目录内 PointId；空、未知或展示名为 nullopt，不保留输入视图。
[[nodiscard]] std::optional<PointId> mfParsePointId(std::string_view StableText) noexcept;

// 用途：查询玩家可以见到的资源点；骨架阶段不暴露任何入口。
// 参与流程：将来的地图能力边界，当前前端不调用。
// 使用方式：借用稳定只读游戏和配置快照；不初始化、保存或修改它们。
// 返回值：始终为空的 span；无时间、RNG、产出、日志、线程等待或IO副作用。
[[nodiscard]] std::span<const PointDefinition> mfVisiblePoints(const Game& ActiveGame,
                                                              const Config& ConfigSnapshot) noexcept;

// 用途：明确拒绝尚未开放的资源点动作，防止误启用发布半成品。
// 参与流程：内部动作闸门与关闭行为测试；当前没有 HTTP route 或地图按钮。
// 使用方式：传游戏、部署配置及动作；游戏仅调用期借用且完全不访问或修改。
// 返回值：Disabled 或 Unavailable，均表示未执行；可重复，无异常、阻塞或任何副作用。
[[nodiscard]] Availability mfTryAction(Game& ActiveGame, const Config& ConfigSnapshot,
                                        const ActionRequest& Request) noexcept;

} // namespace nz::ResourcePoints
