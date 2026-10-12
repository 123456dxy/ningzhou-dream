#include "core/interface/routes.h"

namespace nz::api {
namespace {
constexpr RouteInfo routes[] = {
    {RouteId::Achievements, "/api/achievements", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Advance, "/api/advance", RouteEffect::Mutation, false, false, false, true},
    {RouteId::ArtsAux, "/api/arts/aux", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsFocus, "/api/arts/focus", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsGear, "/api/arts/gear", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsLearn, "/api/arts/learn", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsMain, "/api/arts/main", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsMeditate, "/api/arts/meditate", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsNode, "/api/arts/node", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsNodes, "/api/arts/nodes", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::ArtsSpell, "/api/arts/spell", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ArtsSpells, "/api/arts/spells", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::BattleTurn, "/api/battle/turn", RouteEffect::Mutation, true, true, true, true},
    {RouteId::Bond, "/api/bond", RouteEffect::Mutation, false, false, false, false},
    {RouteId::BookBuy, "/api/book-buy", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Breakthrough, "/api/breakthrough", RouteEffect::Mutation, false, false, false, false},
    {RouteId::BuildCheck, "/api/build-check", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::BuildOptions, "/api/build-options", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Buy, "/api/buy", RouteEffect::Mutation, false, false, false, false},
    {RouteId::CancelJob, "/api/cancel-job", RouteEffect::Mutation, false, false, false, true},
    {RouteId::CaveMove, "/api/cave/move", RouteEffect::Mutation, false, false, false, false},
    {RouteId::CaveUpgrade, "/api/cave/upgrade", RouteEffect::Mutation, false, false, false, false},
    {RouteId::CheckpointRestore, "/api/checkpoint/restore", RouteEffect::Persistence, false, false, false, false},
    {RouteId::Choose, "/api/choose", RouteEffect::Mutation, false, false, true, true},
    {RouteId::Conflict, "/api/conflict", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ConflictPreview, "/api/conflict/preview", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Craft, "/api/craft", RouteEffect::Mutation, false, false, false, false},
    {RouteId::CraftMaterialsQuote, "/api/craft/materials/quote", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::CraftMaterialsBuy, "/api/craft/materials/buy", RouteEffect::Mutation, false, false, false, false},
    {RouteId::DungeonChoose, "/api/dungeon/choose", RouteEffect::Mutation, true, false, false, false},
    {RouteId::DungeonClaim, "/api/dungeon/claim", RouteEffect::Mutation, true, false, false, false},
    {RouteId::DungeonEnter, "/api/dungeon/enter", RouteEffect::Mutation, true, false, false, false},
    {RouteId::DungeonIntel, "/api/dungeon/intel", RouteEffect::Mutation, true, false, false, false},
    {RouteId::DungeonLeave, "/api/dungeon/leave", RouteEffect::Mutation, true, false, false, false},
    {RouteId::Eco, "/api/eco", RouteEffect::Mutation, false, false, false, false},
    {RouteId::EcoOptions, "/api/eco-options", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Equip, "/api/equip", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Expedition, "/api/expedition", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ExportGame, "/api/export", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Forge, "/api/forge", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ForgeOptions, "/api/forge-options", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::ForgePreview, "/api/forge-preview", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::FurnaceBuy, "/api/furnace/buy", RouteEffect::Mutation, false, false, false, false},
    {RouteId::FurnaceRepair, "/api/furnace/repair", RouteEffect::Mutation, false, false, false, false},
    {RouteId::GearInscribe, "/api/gear/inscribe", RouteEffect::Mutation, false, false, false, false},
    {RouteId::GearSalvage, "/api/gear/salvage", RouteEffect::Mutation, false, false, false, false},
    {RouteId::GearUnequip, "/api/gear/unequip", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Gift, "/api/gift", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Greet, "/api/greet", RouteEffect::Mutation, false, false, false, false},
    {RouteId::ImportGame, "/api/import", RouteEffect::ReplaceGame, false, false, false, false},
    {RouteId::Invite, "/api/invite", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Join, "/api/join", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Legacy, "/api/legacy", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::LegacyDraft, "/api/legacy/draft", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::LegacySettle, "/api/legacy/settle", RouteEffect::Persistence, false, false, false, false},
    {RouteId::Library, "/api/library", RouteEffect::Mutation, false, false, false, false},
    {RouteId::LibrarySpell, "/api/library/spell", RouteEffect::Mutation, false, false, false, false},
    {RouteId::LoadGame, "/api/load", RouteEffect::ReplaceGame, false, false, false, false},
    {RouteId::LocationService, "/api/location-service", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Message, "/api/message", RouteEffect::Mutation, false, false, false, false},
    {RouteId::NewGame, "/api/new", RouteEffect::ReplaceGame, false, false, false, false},
    {RouteId::NpcBag, "/api/npc-bag", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::NpcLearn, "/api/npc-learn", RouteEffect::Mutation, false, false, false, false},
    {RouteId::NpcTake, "/api/npc-take", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Plan, "/api/plan", RouteEffect::Mutation, false, false, false, true},
    {RouteId::Quest, "/api/quest", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Roster, "/api/roster", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Save, "/api/save", RouteEffect::Persistence, false, false, false, false},
    {RouteId::Sects, "/api/sects", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Sell, "/api/sell", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Slots, "/api/slots", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Social, "/api/social", RouteEffect::Mutation, false, false, false, false},
    {RouteId::SocialActions, "/api/social-actions", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::State, "/api/state", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::Stipend, "/api/stipend", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Story, "/api/story", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Supply, "/api/supply", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Task, "/api/task", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Tide, "/api/tide", RouteEffect::ReadOnly, false, false, false, false},
    {RouteId::TideFight, "/api/tide/fight", RouteEffect::Mutation, false, false, false, false},
    {RouteId::TideHunker, "/api/tide/hunker", RouteEffect::Mutation, false, false, false, false},
    {RouteId::TradeStock, "/api/trade-stock", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Travel, "/api/travel", RouteEffect::Mutation, false, false, false, false},
    {RouteId::UsePill, "/api/use-pill", RouteEffect::Mutation, false, false, false, false},
    {RouteId::Wild, "/api/wild", RouteEffect::Mutation, false, false, false, false},
};
constexpr RouteInfo unknown{RouteId::Unknown, "", RouteEffect::Mutation};
} // namespace

// 用途：公开稳定HTTP路由元数据表。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：无参数，读取或初始化当前模块状态。
// 返回值：对应目录或计算结果列表。
std::span<const RouteInfo> routeTable() { return routes; }
// 用途：按HTTP路径查只读或动作访问策略。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入std::string_view path，按本模块入口调用。
// 返回值：const RouteInfo&结果，包含说明所述状态或计算数据。
const RouteInfo& lookupRoute(std::string_view path) {
    for (const auto& route : routes) if (path == route.path) return route;
    return unknown;
}
// 用途：由枚举查询稳定HTTP路径。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入RouteId id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const char* routePath(RouteId id) {
    for (const auto& route : routes) if (id == route.id) return route.path;
    return unknown.path;
}
} // namespace nz::api
