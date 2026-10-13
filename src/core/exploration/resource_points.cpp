#include "core/resource_points.h"

#include <array>

namespace nz::ResourcePoints {

// 用途：按部署请求返回关闭或未就绪能力。
// 参与流程：应用启动及模式只读查询。
// 使用方式：传配置快照；就绪闸门固定关闭，不接受角色或网络请求覆盖。
// 返回值：不可用且禁止动作的值快照；无外部状态、分配和IO副作用。
Capability mfCapability(const Config& ConfigSnapshot) noexcept {
    static_assert(!GameplayReady, "资源点完成正式规则与验收后，必须整体替换骨架能力契约。");
    return {ConfigSnapshot.Enabled, false, false,
            ConfigSnapshot.Enabled ? Availability::Unavailable : Availability::Disabled};
}

// 用途：集中转换资源点能力状态的机器标识。
// 参与流程：模式视图与内部验证。
// 使用方式：传状态枚举；非法枚举不降级为合法成功值。
// 返回值：静态字符串视图；无分配、阻塞和游戏副作用。
std::string_view mfStatusId(Availability Status) noexcept {
    switch (Status) {
    case Availability::Disabled:
        return "disabled";
    case Availability::Unavailable:
        return "unavailable";
    }
    return "invalid";
}

// 用途：集中提供城池、宗门后山与青石灵脉的研发预案。
// 参与流程：设计文档和目录完整性测试，不参加世界模拟。
// 使用方式：无参数；每个 ID 都指向静态存储，可重复或并行读取。
// 返回值：十八项不可变定义；仅描述锚点与类别，不生成所有权或奖励。
std::span<const PointDefinition> mfDevelopmentCatalog() noexcept {
    static constexpr std::array<PointDefinition, 18> Catalog{{
        {PointId("wuling.trade_right"), "wuling", "武陵城·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("yifeng.trade_right"), "yifeng", "逸风城·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("yunxi.trade_right"), "yunxi", "云汐城·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("tianxing.trade_right"), "tianxing", "天星城·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("guangling.trade_right"), "guangling", "广陵城·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("dongshi.trade_right"), "dongshi", "东石谷·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("yongan.trade_right"), "yongan", "永安镇·经营权", PointKind::CityFranchise,
         OwnerKind::TownCouncil, ""},
        {PointId("fenglei.herb_field"), "fenglei", "风雷谷·药田", PointKind::HerbField,
         OwnerKind::Sect, "xinghe"},
        {PointId("lianshi.herb_field"), "lianshi", "连石山·药田", PointKind::HerbField,
         OwnerKind::Sect, "jinhong"},
        {PointId("fanghu.herb_field"), "fanghu", "方壶山·药田", PointKind::HerbField,
         OwnerKind::Sect, "zhushan"},
        {PointId("wojiao.herb_field"), "wojiao", "沃焦山·药田", PointKind::HerbField,
         OwnerKind::Sect, "lihuo"},
        {PointId("zhengyang.herb_field"), "zhengyang", "正阳山·药田", PointKind::HerbField,
         OwnerKind::Sect, "huachen"},
        {PointId("fenglei.ore_mine"), "fenglei", "风雷谷·灵矿", PointKind::OreMine,
         OwnerKind::Sect, "xinghe"},
        {PointId("lianshi.ore_mine"), "lianshi", "连石山·灵矿", PointKind::OreMine,
         OwnerKind::Sect, "jinhong"},
        {PointId("fanghu.ore_mine"), "fanghu", "方壶山·灵矿", PointKind::OreMine,
         OwnerKind::Sect, "zhushan"},
        {PointId("wojiao.ore_mine"), "wojiao", "沃焦山·灵矿", PointKind::OreMine,
         OwnerKind::Sect, "lihuo"},
        {PointId("zhengyang.ore_mine"), "zhengyang", "正阳山·灵矿", PointKind::OreMine,
         OwnerKind::Sect, "huachen"},
        {PointId("qingshi.spirit_vein"), "qingshi", "青石灵脉·经营权", PointKind::SpiritVein,
         OwnerKind::Sect, "", true},
    }};
    return Catalog;
}

// 用途：按受控目录严格识别资源点协议值。
// 参与流程：内部输入校验与目录往返测试。
// 使用方式：传任意文本，仅比较稳定 ID，不按展示名或前缀匹配。
// 返回值：指向静态词表的强类型 ID；未知值为 nullopt，输入缓冲区不被保留。
std::optional<PointId> mfParsePointId(std::string_view StableText) noexcept {
    for (const auto& Definition : mfDevelopmentCatalog()) {
        if (Definition.Id.StableValue() == StableText) return Definition.Id;
    }
    return std::nullopt;
}

// 用途：为尚未完成的资源点玩法保持空的玩家可见集合。
// 参与流程：后续地图模块的只读能力边界，当前没有前端调用。
// 使用方式：传稳定只读游戏与配置；骨架不访问或初始化游戏。
// 返回值：空 span，不泄露开发预案为可游玩的地点。
std::span<const PointDefinition> mfVisiblePoints(const Game& ActiveGame,
                                                 const Config& ConfigSnapshot) noexcept {
    (void)ActiveGame;
    (void)ConfigSnapshot;
    return {};
}

// 用途：在任何游戏状态下拒绝资源点骨架的所有动作。
// 参与流程：内部闸门与零副作用测试，尚未注册对外动作。
// 使用方式：传游戏、配置和请求；拒绝不扣费、不建任务、不抽随机数或写日志。
// 返回值：关闭为 Disabled，请求启用为 Unavailable；没有成功或部分提交路径。
Availability mfTryAction(Game& ActiveGame, const Config& ConfigSnapshot,
                         const ActionRequest& Request) noexcept {
    (void)ActiveGame;
    (void)Request;
    return mfCapability(ConfigSnapshot).Status;
}

} // namespace nz::ResourcePoints
