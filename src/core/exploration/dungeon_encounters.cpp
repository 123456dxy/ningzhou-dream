#include "core/exploration/dungeon_encounters.h"
#include "core/content/game_ids.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace nz {
namespace {
constexpr int RegularHealthPercent = 65;
constexpr int EliteHealthPercent = 90;
constexpr int BossHealthPercent = 115;
constexpr int MaximumStage = 12;
constexpr int BeastSpecialActionStage = 3;
constexpr std::string_view RegularNodeType = "battle";
constexpr std::string_view EliteNodeType = "elite";
constexpr std::string_view BossNodeType = "boss";

// 所属探索模块；只拥有静态只读文字及强类型风格，动作执行委托现有妖兽计划器。
struct BeastDefinition {
    DungeonEncounterKind Kind;
    std::string_view Id, Name;
    content::DaoId Style;
    std::string_view LowRealmHint, SpecialHint;
};

constexpr std::array<BeastDefinition, 6> BeastCatalog{{
    {DungeonEncounterKind::StoneGuard, "stone-guard", "岩甲守卫", content::DaoId::Earth,
     "炼气时以扑击应战，每第四轮凝甲；尚未解锁筑基本命轮。",
     "本命轮偏重凝甲，护盾可以抵挡直伤；多段攻击与凝甲仍共用本轮妖力预算。"},
    {DungeonEncounterKind::FlameBeast, "flame-beast", "焰息异兽", content::DaoId::Fire,
     "炼气时第三轮重爪爆发、第四轮凝甲；此境界尚无灼烧吐息。",
     "本命轮烈焰吐息，施加持续三轮的灼烧；不要把全部防御只留给普通扑击。"},
    {DungeonEncounterKind::VenomBeast, "venom-beast", "瘴毒异兽", content::DaoId::Wood,
     "炼气时第三轮回元噬击、第四轮凝甲；此境界尚无中毒吐息。",
     "本命轮喷吐毒息，造成持续三轮的中毒；拖延会承受额外持续伤害。"},
    {DungeonEncounterKind::MindBeast, "mind-beast", "摄魂异兽", content::DaoId::Mind,
     "炼气时第三轮撕咬穿盾40%、第四轮凝甲；此境界尚无扰灵咆哮。",
     "本命轮摄魂咆哮，附加扰灵削弱后续直接攻击；扰灵不是直接扣除灵力。"},
    {DungeonEncounterKind::RendingBeast, "rending-beast", "迅爪掠食者", content::DaoId::Metal,
     "炼气时以扑击应战，每第四轮凝甲；此境界尚无本命穿甲撕咬。",
     "本命轮穿甲撕咬，25%的伤害越过护盾；仍可用减伤压低来伤。"},
    {DungeonEncounterKind::ChargingBeast, "charging-beast", "蓄势猛兽", content::DaoId::Body,
     "炼气时以扑击应战，每第四轮凝甲；此境界尚未解锁蓄势与本命重爪。",
     "本命轮使用重爪；第四轮蓄势后，下一次攻击增强，仍受整轮伤害上限约束。"}
}};

// 所属探索模块；宗门由统一内容ID映射真实普通NPC构筑，不伪造完整传承身份。
struct SectDefinition {
    content::SectId Sect;
    std::string_view Hint;
};

constexpr std::array<SectDefinition, 6> SectCatalog{{
    {content::SectId::Jinhong, "金系经营弃牌、护盾与重击，留意弃牌后积蓄的爆发。"},
    {content::SectId::Xinghe, "水系先经营水势再以浪击兑现，兼有护盾与补牌。"},
    {content::SectId::Zhushan, "木系施毒与回复并行，久战会承受毒池压力。"},
    {content::SectId::Lihuo, "火系叠焰后引爆，受伤且有灼烧池时可能转为治疗。"},
    {content::SectId::Huachen, "土系先凝盾蓄势再兑现重击，注意护盾与蓄势的联动。"},
    {content::SectId::Xuandao, "五行剑按土金水木火衔接，相生带来返灵与补牌。"}
}};

// 用途：以两个稳定业务ID形成无歧义的固定散列；流程：路线预告与实际入场共同调用。
// 使用方式：传入口与节点ID；返回值：固定32位数，不读取平台hash、时钟或活动RNG。
std::uint32_t mfEncounterHash(std::string_view SiteId, std::string_view NodeId) {
    constexpr std::uint32_t OffsetBasis = 2166136261u;
    constexpr std::uint32_t Prime = 16777619u;
    std::uint32_t Hash = OffsetBasis;
    for (const unsigned char Byte : SiteId) Hash = (Hash ^ Byte) * Prime;
    Hash = (Hash ^ 255u) * Prime;
    for (const unsigned char Byte : NodeId) Hash = (Hash ^ Byte) * Prime;
    return Hash;
}

// 用途：识别既有秘境的三类战斗节点；流程：规格边界校验；用法：传协议type，不传显示名。
// 返回值：仅battle/elite/boss为真，不把未知节点降级为普通战斗。
bool mfIsCombatNode(std::string_view Type) {
    return Type == RegularNodeType || Type == EliteNodeType || Type == BossNodeType;
}
} // namespace

// 用途：以固定入口/节点选择可预判对手；流程：路线展示与真正开战共用规格。
// 使用方式：传活动快照与节点；返回值：启用的报价或明确禁用原因，全程没有状态/RNG副作用。
DungeonEncounter mfDescribeDungeonEncounter(const Game& State, const DungeonRun& Run, const DungeonNode& Node) {
    DungeonEncounter Result;
    if (Run.siteId.empty() || Node.id.empty()) {
        Result.Reason = "秘境入口或节点编号为空。";
        return Result;
    }
    if (!mfIsCombatNode(Node.type)) {
        Result.Reason = "此节点不是斗法节点。";
        return Result;
    }
    const auto Site = std::find_if(State.dungeons.sites.begin(), State.dungeons.sites.end(),
        [&](const DungeonSite& Entry) { return Entry.id == Run.siteId; });
    if (Site == State.dungeons.sites.end()) {
        Result.Reason = "秘境冻结入口不存在，无法确定对手难度。";
        return Result;
    }
    if (Site->stage < 0 || Site->stage > MaximumStage) {
        Result.Reason = "秘境入口境界无效，无法生成对手。";
        return Result;
    }
    auto& Enemy = Result.Enemy;
    Enemy.kind = content::legacyValue(content::BattleKind::Dungeon);
    Enemy.dungeonSite = Run.siteId;
    Enemy.dungeonNode = Node.id;
    Enemy.stage = std::max(0, Site->stage - (Node.type == RegularNodeType ? 1 : 0));
    Enemy.health = Node.type == BossNodeType ? BossHealthPercent :
        Node.type == EliteNodeType ? EliteHealthPercent : RegularHealthPercent;
    const auto Selection = mfEncounterHash(Run.siteId, Node.id);
    Result.Hint = "实际难度：" + realmName(Enemy.stage) + "。";
    if (Node.type == EliteNodeType) {
        const auto& Definition = SectCatalog[Selection % SectCatalog.size()];
        Enemy.sect = content::stableId(Definition.Sect);
        Enemy.style = content::legacyDaoKey(content::sectDao(Definition.Sect));
        const auto* Sect = findSect(Enemy.sect);
        Result.Id = "sect-disciple:" + Enemy.sect;
        Result.Kind = DungeonEncounterKind::SectDisciple;
        Result.Name = realmName(Enemy.stage) + "·" + (Sect ? std::string(Sect->name) : Enemy.sect) + "试炼弟子";
        Result.Hint += Definition.Hint;
        Result.Hint += "采用该宗门普通构筑，实际神通、补牌与连招受境界、手牌和灵力限制；并非完整传承高手。";
    } else {
        const auto& Definition = BeastCatalog[Selection % BeastCatalog.size()];
        Result.Id = Definition.Id;
        Result.Kind = Definition.Kind;
        Enemy.beast = true;
        Enemy.style = content::legacyDaoKey(Definition.Style);
        Result.Name = realmName(Enemy.stage) + "·" + (Node.type == BossNodeType ? "镇境·" : "") + std::string(Definition.Name);
        Result.Hint += Enemy.stage < BeastSpecialActionStage ? Definition.LowRealmHint : Definition.SpecialHint;
        if (Enemy.stage >= BeastSpecialActionStage)
            Result.Hint += "本命动作通常出现在四轮循环的第二、三轮，仍受该境界妖力与行动次数限制；先看当轮意图再出牌。";
    }
    if (Node.type == BossNodeType) Result.Hint += "终点首领气血更充足，击败后从已冻结的永久奖励中选择一项。";
    Enemy.name = Result.Name;
    Result.Enabled = true;
    return Result;
}
} // namespace nz
