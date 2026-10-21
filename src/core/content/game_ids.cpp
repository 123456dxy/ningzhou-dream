#include "core/content/game_ids.h"

#include <array>
#include <stdexcept>

namespace nz::content {
namespace {
template<class Id> struct Entry { Id id; std::string_view stable, legacy; };
constexpr std::array daos = {
    Entry{DaoId::Metal, "metal", "金"}, Entry{DaoId::Wood, "wood", "木"},
    Entry{DaoId::Water, "water", "水"}, Entry{DaoId::Fire, "fire", "火"},
    Entry{DaoId::Earth, "earth", "土"}, Entry{DaoId::Mind, "mind", "神"},
    Entry{DaoId::Body, "body", "体"}, Entry{DaoId::Sword, "sword", "剑"},
    Entry{DaoId::Qi, "qi", "气"}, Entry{DaoId::Array, "array", "阵"},
    Entry{DaoId::Alchemy, "alchemy", "丹"}, Entry{DaoId::Artifact, "artifact", "器"}
};
constexpr std::array sects = {
    Entry{SectId::Jinhong, "jinhong", "jinhong"}, Entry{SectId::Xinghe, "xinghe", "xinghe"},
    Entry{SectId::Zhushan, "zhushan", "zhushan"}, Entry{SectId::Lihuo, "lihuo", "lihuo"},
    Entry{SectId::Huachen, "huachen", "huachen"}, Entry{SectId::Xuandao, "xuandao", "xuandao"}
};
constexpr std::array spells = {
    Entry{SpellKind::Attack, "attack", "attack"}, Entry{SpellKind::Heal, "heal", "heal"},
    Entry{SpellKind::Shield, "shield", "shield"}, Entry{SpellKind::Fire, "fire", "fire"},
    Entry{SpellKind::Sword, "sword", "sword"}, Entry{SpellKind::Drain, "drain", "drain"},
    Entry{SpellKind::Guard, "guard", "guard"}, Entry{SpellKind::Store, "store", "store"},
    Entry{SpellKind::Flow, "flow", "flow"}, Entry{SpellKind::Burst, "burst", "burst"},
    Entry{SpellKind::Draw, "draw", "draw"}, Entry{SpellKind::Foresee, "foresee", "foresee"},
    Entry{SpellKind::Redraw, "redraw", "redraw"}, Entry{SpellKind::Exhaust, "exhaust", "exhaust"},
    Entry{SpellKind::Empower, "empower", "empower"}, Entry{SpellKind::Ward, "ward", "ward"},
    Entry{SpellKind::FireBurst, "fire_burst", "fireBurst"}, Entry{SpellKind::Mana, "mana", "mana"},
    Entry{SpellKind::Charge, "charge", "charge"}, Entry{SpellKind::Water, "water", "water"},
    Entry{SpellKind::Heavy, "heavy", "heavy"},
    Entry{SpellKind::Wave, "wave", "wave"}, Entry{SpellKind::Poison, "poison", "poison"},
    Entry{SpellKind::PoisonBurst, "poison_burst", "poisonBurst"}, Entry{SpellKind::Float, "float", "float"},
    Entry{SpellKind::Human, "human", "human"}, Entry{SpellKind::Soul, "soul", "soul"},
    Entry{SpellKind::SoulBurst, "soul_burst", "soulBurst"}, Entry{SpellKind::Steal, "steal", "steal"},
    Entry{SpellKind::BodyBurst, "body_burst", "bodyBurst"},
    Entry{SpellKind::FireSiphon, "fire_siphon", "fireSiphon"},
    Entry{SpellKind::PoisonBloom, "poisonBloom", "poisonBloom"},
    Entry{SpellKind::PoisonSiphon, "poisonSiphon", "poisonSiphon"},
    Entry{SpellKind::DrawTo, "drawTo", "drawTo"}
};
constexpr std::array battles = {
    Entry{BattleKind::Spar, "spar", "spar"}, Entry{BattleKind::Wild, "wild", "wild"},
    Entry{BattleKind::Hunt, "hunt", "hunt"}, Entry{BattleKind::Tide, "tide", "tide"},
    Entry{BattleKind::Ambush, "ambush", "ambush"}, Entry{BattleKind::Kill, "kill", "kill"},
    Entry{BattleKind::Rob, "rob", "rob"}, Entry{BattleKind::Dungeon, "dungeon", "dungeon"},
    Entry{BattleKind::SectTrial, "sect_trial", "sectTrial"}, Entry{BattleKind::Tianji, "tianji", "tianji"},
    Entry{BattleKind::AscensionTournament, "ascension_tournament", "ascensionTournament"}
};
// 用途：按稳定标识或冻结旧值解析强类型枚举。
// 参与流程：内容标识解析的内部共用实现。
// 使用方式：传文本和对应枚举表，拒绝表外值。
// 返回值：匹配的枚举 optional；未知值为 nullopt。
template<class Id, std::size_t Size>
std::optional<Id> parse(std::string_view value, const std::array<Entry<Id>, Size>& entries) {
    for (const auto& entry : entries)
        if (value == entry.stable || value == entry.legacy) return entry.id;
    return std::nullopt;
}
// 用途：从枚举表读取稳定标识或兼容旧值。
// 参与流程：标识序列化内部实现。
// 使用方式：传枚举、对应表及是否取旧值。
// 返回值：表内字符串视图；非法枚举为空。
template<class Id, std::size_t Size>
std::string_view valueOf(Id id, const std::array<Entry<Id>, Size>& entries, bool legacy = false) {
    for (const auto& entry : entries) if (entry.id == id) return legacy ? entry.legacy : entry.stable;
    return {};
}
// 用途：在编译期提取枚举表中的全部 ID。
// 参与流程：所有标识枚举列表初始化。
// 使用方式：传固定大小的条目表。
// 返回值：保持条目顺序的枚举数组。
template<class Id, std::size_t Size>
constexpr auto idsOf(const std::array<Entry<Id>, Size>& entries) {
    std::array<Id, Size> ids{};
    for (std::size_t index = 0; index < Size; ++index) ids[index] = entries[index].id;
    return ids;
}
}

// 用途：提供十二大道的强类型集合。
// 参与流程：节点、元素和内容注册校验。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：按历史序号排列的 DaoId span。
std::span<const DaoId> allDaoIds() { static constexpr auto ids = idsOf(daos); return ids; }
// 用途：提供可识别宗门的强类型集合。
// 参与流程：宗门内容注册与遍历。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：六个 SectId 的 span，既有顺序保持不变。
std::span<const SectId> allSectIds() { static constexpr auto ids = idsOf(sects); return ids; }
// 用途：列出支持的神通结算类型。
// 参与流程：神通注册校验与类型遍历。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：所有 SpellKind 的 span。
std::span<const SpellKind> allSpellKinds() { static constexpr auto ids = idsOf(spells); return ids; }
// 用途：列出支持的战斗种类。
// 参与流程：战斗入口与存档值校验。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：所有 BattleKind 的 span。
std::span<const BattleKind> allBattleKinds() { static constexpr auto ids = idsOf(battles); return ids; }
// 用途：识别大道稳定 ID 或历史中文键。
// 参与流程：悟道、功法和元素规则入口。
// 使用方式：传内容键；展示名称不反向充当协议值。
// 返回值：匹配 DaoId；未知文本为 nullopt。
std::optional<DaoId> parseDaoId(std::string_view value) { return parse(value, daos); }
// 用途：解析宗门 ID。
// 参与流程：宗门加成、NPC 构筑和肖像选择。
// 使用方式：传稳定或兼容宗门值。
// 返回值：匹配 SectId；未知宗门为 nullopt。
std::optional<SectId> parseSectId(std::string_view value) { return parse(value, sects); }
// 用途：解析神通结算类型。
// 参与流程：卡牌效果、费用和说明分派。
// 使用方式：传稳定种类或冻结旧 kind 值。
// 返回值：匹配 SpellKind；未知类型为 nullopt。
std::optional<SpellKind> parseSpellKind(std::string_view value) { return parse(value, spells); }
// 用途：解析战斗场景种类。
// 参与流程：战斗生命周期与风险规则入口。
// 使用方式：传稳定种类或兼容旧值。
// 返回值：匹配 BattleKind；未知类型为 nullopt。
std::optional<BattleKind> parseBattleKind(std::string_view value) { return parse(value, battles); }
// 用途：解析法器元素并兼容神念、混元别名。
// 参与流程：法宝能力生成与炼器元素识别。
// 使用方式：传法器保存的元素文本。
// 返回值：对应 DaoId；未知元素为 nullopt。
std::optional<DaoId> parseArtifactElement(std::string_view value) {
    if (value == "神念") return DaoId::Mind;
    if (value == "混元") return DaoId::Qi;
    return parseDaoId(value);
}
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(DaoId id) { return valueOf(id, daos); }
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(SectId id) { return valueOf(id, sects); }
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(SpellKind id) { return valueOf(id, spells); }
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(BattleKind id) { return valueOf(id, battles); }
// 用途：输出冻结的十二大道存档键。
// 参与流程：经验、节点和旧功法数据兼容。
// 使用方式：传 DaoId，不使用可改的展示名称。
// 返回值：历史大道中文键；非法枚举为空。
std::string_view legacyDaoKey(DaoId id) { return valueOf(id, daos, true); }
// 用途：输出对应类型的冻结旧协议值。
// 参与流程：旧神通 kind 或战斗 kind 的兼容序列化。
// 使用方式：传 SpellKind 或 BattleKind，按专属表查询。
// 返回值：历史协议字符串视图；非法枚举为空。
std::string_view legacyValue(SpellKind id) { return valueOf(id, spells, true); }
// 用途：输出对应类型的冻结旧协议值。
// 参与流程：旧神通 kind 或战斗 kind 的兼容序列化。
// 使用方式：传 SpellKind 或 BattleKind，按专属表查询。
// 返回值：历史协议字符串视图；非法枚举为空。
std::string_view legacyValue(BattleKind id) { return valueOf(id, battles, true); }
// 用途：将大道转为历史十二系数组序号。
// 参与流程：悟道节点和元素系数索引。
// 使用方式：传 DaoId，索引顺序保持存档兼容。
// 返回值：0..11 的序号；无效 ID 为 -1。
int daoOrdinal(DaoId id) {
    for (std::size_t index = 0; index < daos.size(); ++index)
        if (daos[index].id == id) return static_cast<int>(index);
    return -1;
}
// 用途：读取宗门对应的五行主道。
// 参与流程：NPC 默认构筑与宗门归属规则。
// 使用方式：传有效 SectId；非法枚举抛 invalid_argument。
// 返回值：宗门主道 DaoId。
DaoId sectDao(SectId id) {
    switch (id) {
    case SectId::Jinhong: return DaoId::Metal;
    case SectId::Xinghe: return DaoId::Water;
    case SectId::Zhushan: return DaoId::Wood;
    case SectId::Lihuo: return DaoId::Fire;
    case SectId::Huachen: return DaoId::Earth;
    case SectId::Xuandao: return DaoId::Array;
    }
    throw std::invalid_argument("Invalid sect ID");
}
// 用途：判断两道是否构成五行相生。
// 参与流程：卡牌连击与相生预览。
// 使用方式：依次传上一张和当前神通大道。
// 返回值：木火土金水循环的相邻关系为真。
bool generates(DaoId previous, DaoId next) {
    switch (previous) {
    case DaoId::Wood: return next == DaoId::Fire;
    case DaoId::Fire: return next == DaoId::Earth;
    case DaoId::Earth: return next == DaoId::Metal;
    case DaoId::Metal: return next == DaoId::Water;
    case DaoId::Water: return next == DaoId::Wood;
    default: return false;
    }
}
} // namespace nz::content
