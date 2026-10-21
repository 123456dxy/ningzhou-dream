#include "core/exploration/beast_loot.h"

#include "core/content/forge_content.h"
#include "core/content/game_ids.h"
#include "core/foundation/state_limits.h"
#include "core/sim.h"

#include <cstdint>
#include <string_view>

namespace nz::exploration {
namespace {
constexpr std::array BeastElements{"金", "木", "水", "火", "土", "剑", "神", "气"};
constexpr std::array PartIds{"skin", "bone", "claw", "eye", "heart"};
constexpr int MaximumEnemyStage = 12;
constexpr int MaximumDungeonMaterialCount = 100000;

std::string_view mfBeastMaterialElementId(content::DaoId Dao) {
    switch (Dao) {
    case content::DaoId::Metal: return "metal";
    case content::DaoId::Wood: return "wood";
    case content::DaoId::Water: return "water";
    case content::DaoId::Fire: return "fire";
    case content::DaoId::Earth: return "earth";
    case content::DaoId::Sword: return "sword";
    case content::DaoId::Mind: return "spirit";
    case content::DaoId::Qi:
    case content::DaoId::Body: return "mixed";
    default: return {};
    }
}

bool mfIsBeastKillKind(std::string_view Kind) {
    const auto Parsed = content::parseBattleKind(Kind);
    return Parsed == content::BattleKind::Wild || Parsed == content::BattleKind::Hunt ||
        Parsed == content::BattleKind::Tide || Parsed == content::BattleKind::Dungeon ||
        Parsed == content::BattleKind::Ambush || Parsed == content::BattleKind::Kill;
}

// 用途：用已有随机快照与明确分隔的冻结业务ID派生局部阴阳；不调用活动随机数。
std::uint32_t mfBeastLootHash(const Game& State, const Battle& Context) {
    std::uint32_t Hash = 2166136261u ^ State.seed ^ State.rng;
    const auto Append = [&](std::string_view Text) {
        for (const unsigned char Byte : Text) Hash = (Hash ^ Byte) * 16777619u;
        Hash = (Hash ^ 255u) * 16777619u;
    };
    Append(Context.kind); Append(Context.enemy); Append(Context.enemyDao);
    Append(Context.npcId); Append(Context.dungeonSite); Append(Context.dungeonNode);
    Append(Context.tideEventId); Append(std::to_string(Context.enemyStage));
    Append(std::to_string(Context.advSeq));
    return mixSeed(Hash);
}
} // namespace

std::optional<BeastMaterialLoot> mfQuoteBeastMaterialLoot(const Game& State, const Battle& Context) {
    if (!Context.won || Context.retreat || Context.shadowPlayer || !Context.enemyBeast ||
        Context.enemyStage < 0 || Context.enemyStage > MaximumEnemyStage || !mfIsBeastKillKind(Context.kind)) return std::nullopt;
    const auto Dao = content::parseDaoId(Context.enemyDao);
    if (!Dao) return std::nullopt;
    const auto ElementId = mfBeastMaterialElementId(*Dao);
    if (ElementId.empty()) return std::nullopt;
    BeastMaterialLoot Loot;
    Loot.Grade = Context.enemyStage / 3 + 1;
    const bool IsYang = (mfBeastLootHash(State, Context) & 1u) != 0;
    const string PolarityId = IsYang ? "yang" : "yin";
    Loot.Polarity = IsYang ? "天阳" : "地阴";
    Loot.Element = *Dao == content::DaoId::Body ? "气" : string(content::legacyDaoKey(*Dao));
    for (std::size_t PartIndex = 0; PartIndex < PartIds.size(); ++PartIndex) {
        const string Id = "fm-" + std::to_string(Loot.Grade) + "-" + string(ElementId) + "-" + PolarityId + "-" + PartIds[PartIndex];
        const auto* Definition = content::mfFindForgeMaterial(Id);
        if (!Definition || Definition->Source != content::ForgeMaterialSource::BeastDrop || Definition->AcquisitionPending)
            return std::nullopt;
        Loot.MaterialIds[PartIndex] = Id;
    }
    return Loot;
}

string mfRollBeastMaterialElement(Game& State) {
    return BeastElements[static_cast<std::size_t>(randIntOf(State, 0, static_cast<int>(BeastElements.size()) - 1))];
}

int mfAwardBeastMaterialLoot(Game& State, const Battle& Context) {
    const auto Loot = mfQuoteBeastMaterialLoot(State, Context);
    if (!Loot) return 0;
    const bool IsDungeon = !Context.dungeonSite.empty();
    if (IsDungeon && (!State.dungeons.run || State.dungeons.run->phase != "battle" ||
        State.dungeons.run->siteId != Context.dungeonSite || State.dungeons.run->current != Context.dungeonNode)) return 0;
    auto& Inventory = IsDungeon ? State.dungeons.run->materials : State.player.arts.materials;
    const int MaximumCount = IsDungeon ? MaximumDungeonMaterialCount : StateLimits::MaxPersistedInteger;
    auto Candidate = Inventory;
    for (const auto& Id : Loot->MaterialIds) {
        auto& Count = Candidate[Id];
        const auto Permanent = State.player.arts.materials.find(Id);
        const int PermanentCount = Permanent == State.player.arts.materials.end() ? 0 : Permanent->second;
        if (Count < 0 || Count >= MaximumCount || (IsDungeon && (PermanentCount < 0 ||
            static_cast<long long>(PermanentCount) + Count + 1 > StateLimits::MaxPersistedInteger))) {
            logEvent(State, "self", "妖兽五部位材料超过行囊容量，本次未能收取。", false);
            return 0;
        }
        ++Count;
    }
    const string Text = "击杀" + Context.enemy + "：获得" + std::to_string(Loot->Grade) + "阶" + Loot->Polarity + Loot->Element +
        "属性兽皮鳞、妖骨、牙爪、妖目、妖心各1份" + (IsDungeon ? "，已存入探索行囊，离境时带出。" : "。");
    Inventory.swap(Candidate);
    logEvent(State, "self", Text, true);
    return static_cast<int>(Loot->MaterialIds.size());
}
} // namespace nz::exploration
