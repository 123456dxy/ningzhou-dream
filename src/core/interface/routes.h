#pragma once
#include <string_view>
#include <span>

namespace nz::api {
// Wire paths are centralized here. Unknown paths have mutation policy and
// continue through the same method/session/action guards before a 404.
enum class RouteEffect { ReadOnly, Mutation, ReplaceGame, Persistence };
enum class RouteId {
    Unknown,
    Achievements,
    Advance,
    ArtsAux,
    ArtsFocus,
    ArtsGear,
    ArtsLearn,
    ArtsMain,
    ArtsMeditate,
    ArtsNode,
    ArtsNodes,
    ArtsSpell,
    ArtsSpells,
    BattleTurn,
    Bond,
    BookBuy,
    Breakthrough,
    Zhuji,
    BuildCheck,
    BuildOptions,
    Buy,
    CancelJob,
    CaveMove,
    CaveUpgrade,
    CaveGardenPlant,
    CaveGardenHarvest,
    CaveGardenDeposit,
    CaveGardenWithdraw,
    CaveGardenClear,
    CaveGreenBottleUpgrade,
    CaveGreenBottleDungeon,
    CheckpointRestore,
    Choose,
    Conflict,
    ConflictPreview,
    Craft,
    CraftMaterialsQuote,
    CraftMaterialsBuy,
    DungeonChoose,
    DungeonClaim,
    DungeonEnter,
    DungeonIntel,
    DungeonLeave,
    Eco,
    EcoOptions,
    Equip,
    Expedition,
    ExportGame,
    Forge,
    ForgeOptions,
    ForgePreview,
    FurnaceBuy,
    FurnaceRepair,
    GearInscribe,
    GearSalvage,
    GearUnequip,
    Gift,
    Greet,
    ImportGame,
    Invite,
    Join,
    Legacy,
    LegacyDraft,
    LegacySettle,
    Library,
    LibrarySpell,
    LoadGame,
    LocationService,
    Message,
    NewGame,
    NpcBag,
    NpcLearn,
    NpcTake,
    Plan,
    Quest,
    Roster,
    Save,
    Sects,
    Sell,
    Slots,
    Social,
    SocialActions,
    State,
    Stipend,
    Story,
    Supply,
    Task,
    Tide,
    TideFight,
    TideMarketBuy,
    TideHunker,
    TradeStock,
    Travel,
    UsePill,
    Wild,
    DungeonSweep,
    ArtsLifebound,
    PillBatchQuote,
    PillBatchUse,
    AlchemyQuote,
    TianjiShop,
    TianjiShopQuote,
    TianjiShopBuy,
    AscensionTournament,
    AscensionTournamentEnter,
};
struct RouteInfo {
    RouteId id;
    const char* path;
    RouteEffect effect;
    bool duringDungeon = false;
    bool duringBattle = false;
    bool duringPending = false;
    bool duringJob = false;
};
// 用途：公开稳定HTTP路由元数据表。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应目录或计算结果列表。
std::span<const RouteInfo> routeTable();
// 用途：按HTTP路径查只读或动作访问策略。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入std::string_view path，按本模块入口调用。
// 返回值：const RouteInfo&结果，包含说明所述状态或计算数据。
const RouteInfo& lookupRoute(std::string_view path);
// 用途：由枚举查询稳定HTTP路径。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入RouteId id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const char* routePath(RouteId id);
} // namespace nz::api
