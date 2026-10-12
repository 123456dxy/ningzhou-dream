#pragma once

#include <optional>
#include <span>
#include <string_view>

namespace nz::content {

// Ordinals deliberately retain the historical twelve-dao node/bonus layout.
enum class DaoId { Metal, Wood, Water, Fire, Earth, Mind, Body, Sword, Qi, Array, Alchemy, Artifact };
enum class SectId { Jinhong, Xinghe, Zhushan, Lihuo, Huachen };
enum class SpellKind {
    Attack, Heal, Shield, Fire, Sword, Drain, Guard, Store, Flow, Burst,
    Draw, Foresee, Redraw, Exhaust, Empower, Ward, FireBurst, Mana, Charge, Heavy,
    Water, Wave, Poison, PoisonBurst, Float, Human, Soul, SoulBurst,
    Steal, BodyBurst
};
enum class BattleKind { Spar, Wild, Hunt, Tide, Ambush, Kill, Rob, Dungeon, SectTrial, Tianji };

// 用途：提供十二大道的强类型集合。
// 参与流程：节点、元素和内容注册校验。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：按历史序号排列的 DaoId span。
std::span<const DaoId> allDaoIds();
// 用途：提供可识别宗门的强类型集合。
// 参与流程：宗门内容注册与遍历。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：五个 SectId 的 span。
std::span<const SectId> allSectIds();
// 用途：列出支持的神通结算类型。
// 参与流程：神通注册校验与类型遍历。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：所有 SpellKind 的 span。
std::span<const SpellKind> allSpellKinds();
// 用途：列出支持的战斗种类。
// 参与流程：战斗入口与存档值校验。
// 使用方式：无参数，读取静态不可变视图。
// 返回值：所有 BattleKind 的 span。
std::span<const BattleKind> allBattleKinds();

// 用途：识别大道稳定 ID 或历史中文键。
// 参与流程：悟道、功法和元素规则入口。
// 使用方式：传内容键；展示名称不反向充当协议值。
// 返回值：匹配 DaoId；未知文本为 nullopt。
std::optional<DaoId> parseDaoId(std::string_view value);
// 用途：解析宗门 ID。
// 参与流程：宗门加成、NPC 构筑和肖像选择。
// 使用方式：传稳定或兼容宗门值。
// 返回值：匹配 SectId；未知宗门为 nullopt。
std::optional<SectId> parseSectId(std::string_view value);
// 用途：解析神通结算类型。
// 参与流程：卡牌效果、费用和说明分派。
// 使用方式：传稳定种类或冻结旧 kind 值。
// 返回值：匹配 SpellKind；未知类型为 nullopt。
std::optional<SpellKind> parseSpellKind(std::string_view value);
// 用途：解析战斗场景种类。
// 参与流程：战斗生命周期与风险规则入口。
// 使用方式：传稳定种类或兼容旧值。
// 返回值：匹配 BattleKind；未知类型为 nullopt。
std::optional<BattleKind> parseBattleKind(std::string_view value);
// Crafting has historically used 神念 and 混元 as element aliases.
// 用途：解析法器元素并兼容神念、混元别名。
// 参与流程：法宝能力生成与炼器元素识别。
// 使用方式：传法器保存的元素文本。
// 返回值：对应 DaoId；未知元素为 nullopt。
std::optional<DaoId> parseArtifactElement(std::string_view value);

// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(DaoId id);
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(SectId id);
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(SpellKind id);
// 用途：输出强类型内容标识的稳定机器值。
// 参与流程：内容协议和注册检查。
// 使用方式：传对应枚举，按其专属表查询。
// 返回值：稳定 ID 字符串视图；无效枚举为空。
std::string_view stableId(BattleKind id);
// Frozen compatibility values; these are never obtained from displayName().
// 用途：输出冻结的十二大道存档键。
// 参与流程：经验、节点和旧功法数据兼容。
// 使用方式：传 DaoId，不使用可改的展示名称。
// 返回值：历史大道中文键；非法枚举为空。
std::string_view legacyDaoKey(DaoId id);
// 用途：输出对应类型的冻结旧协议值。
// 参与流程：旧神通 kind 或战斗 kind 的兼容序列化。
// 使用方式：传 SpellKind 或 BattleKind，按专属表查询。
// 返回值：历史协议字符串视图；非法枚举为空。
std::string_view legacyValue(SpellKind id);
// 用途：输出对应类型的冻结旧协议值。
// 参与流程：旧神通 kind 或战斗 kind 的兼容序列化。
// 使用方式：传 SpellKind 或 BattleKind，按专属表查询。
// 返回值：历史协议字符串视图；非法枚举为空。
std::string_view legacyValue(BattleKind id);
// 用途：将大道转为历史十二系数组序号。
// 参与流程：悟道节点和元素系数索引。
// 使用方式：传 DaoId，索引顺序保持存档兼容。
// 返回值：0..11 的序号；无效 ID 为 -1。
int daoOrdinal(DaoId id);
// 用途：读取宗门对应的五行主道。
// 参与流程：NPC 默认构筑与宗门归属规则。
// 使用方式：传有效 SectId；非法枚举抛 invalid_argument。
// 返回值：宗门主道 DaoId。
DaoId sectDao(SectId id);
// 用途：判断两道是否构成五行相生。
// 参与流程：卡牌连击与相生预览。
// 使用方式：依次传上一张和当前神通大道。
// 返回值：木火土金水循环的相邻关系为真。
bool generates(DaoId previous, DaoId next);

} // namespace nz::content
