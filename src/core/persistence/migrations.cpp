// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/crafting/cave_garden.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/economy.h"
#include "core/society.h"
#include "core/adventure.h"
#include "core/dungeon.h"
#include "core/sim.h"
#include "core/tide.h"
#include "core/content/npc_combat_profiles.h"
#include "core/combat/battle_scaling.h"
#include "core/foundation/state_limits.h"
#include <array>
#include <exception>
#include <set>
#include <algorithm>
#include <cmath>

namespace nz::persistence {

namespace {
// 用途：清退已取消的探索物资、合并化毒丹库存；仅旧版本加载时执行，不重放服丹或消耗随机数。
bool mfMigrateRetiredPills(map<string, int>& Inventory, bool NpcInventory = false) try {
    // 不允许先删除坏数量再让后续校验误判为合法；公共加载的原始形状校验也会先拒绝。
    for (const auto& [Id, Count] : Inventory) {
        static_cast<void>(Id);
        if (Count < 0 || Count > StateLimits::MaxPersistedInteger) return false;
    }
    const string Target = NpcInventory ? "potion:5420" : "5420";
    for (const char* Source : {"5519", "potion:5519", "5420"}) {
        if (Target == Source) continue;
        const auto Retired = Inventory.find(Source);
        if (Retired == Inventory.end()) continue;
        const auto Existing = Inventory.find(Target);
        const long long Total = static_cast<long long>(Existing == Inventory.end() ? 0 : Existing->second) + Retired->second;
        if (Total > StateLimits::MaxPersistedInteger) return false;
        Inventory[Target] = static_cast<int>(Total);
        Inventory.erase(Retired);
    }
    for (const char* Id : {"5318", "5319", "potion:5318", "potion:5319", "6323", "6324",
            "诱妖草", "霸王花", "herb:6323", "herb:6324", "herb:诱妖草", "herb:霸王花"}) Inventory.erase(Id);
    return true;
} catch (const std::exception&) {
    return false;
}

// 只核对即将被取消/替换的旧合同。取消不能把坏数值、错误类型或未付款合同清洗成有效存档。
bool mfValidRetiredBrewContract(const Value& Raw, const Job& Contract, int CurrentDay) {
    if (!Raw.isObject() || !Raw["destination"].isString() || !Raw["actionType"].isString()) return false;
    for (const char* Field : {"total", "remaining", "started"})
        if (!saveInteger(Raw[Field], 0, StateLimits::MaxPersistedInteger)) return false;
    for (const char* Field : {"cost", "fee", "gift", "batch", "craftOutput", "travelChecks", "travelAmbushes"})
        if (Raw.asObject().contains(Field) && !saveInteger(Raw[Field], 0, StateLimits::MaxPersistedInteger)) return false;
    for (const char* Field : {"gift", "batch"}) {
        if (!Raw.asObject().contains(Field)) continue;
        const int Count = Raw[Field].asInt();
        if (Count != 1 && Count != 3 && Count != 5 && Count != 10) return false;
    }
    for (const char* Field : {"chance", "breakthroughChance", "breakthroughFailureLoss"})
        if (Raw.asObject().contains(Field) && (!Raw[Field].isNumber() || !std::isfinite(Raw[Field].asNumber()) ||
            Raw[Field].asNumber() < 0 || Raw[Field].asNumber() > 1)) return false;
    if (Raw.asObject().contains("paid") && (!Raw["paid"].isBool() || !Raw["paid"].asBool())) return false;
    return Contract.paid && Contract.total > 0 && Contract.total <= 360000 &&
        Contract.remaining > 0 && Contract.remaining <= Contract.total &&
        Contract.started >= 0 && Contract.started <= CurrentDay && Contract.cost >= 0 && Contract.fee >= 0 &&
        (Contract.batch == 1 || Contract.batch == 3 || Contract.batch == 5 || Contract.batch == 10) &&
        Contract.gift == Contract.batch && Contract.craftOutput >= 1 && Contract.craftOutput <= 4 &&
        Contract.travelChecks >= 0 && Contract.travelChecks <= Contract.total - Contract.remaining &&
        Contract.travelAmbushes >= 0 && Contract.travelAmbushes <= 1;
}

bool mfMigrateRetiredBrew(const Value& Root, Game& State) {
    if (!State.job || State.job->actionType != "brew") return true;
    const auto& Contract = *State.job;
    if (Contract.destination != "5519" && Contract.destination != "5318" && Contract.destination != "5319") return true;
    if (!mfValidRetiredBrewContract(Root["job"], Contract, State.clock.days)) return false;
    try {
        if (Contract.destination == "5519") {
            State.job->destination = "5420";
            State.job->name = "炼制太乙降尘丹";
            return true;
        }
        // 固定历史合同：5318=诱妖草1+幻心草2+混元果1；5319=霸王花1+幻心草2+鸡冠草1。
        // 退役主药旧本地HerbDef.price均112；仅将这部分折灵石，现存辅材按实际份数退回。
        constexpr int RetiredMainHerbValue = 112;
        const int Batch = Contract.batch;
        auto& PlayerState = State.player;
        const long long StonesAfter = static_cast<long long>(PlayerState.stones) + Contract.cost +
            static_cast<long long>(RetiredMainHerbValue) * Batch;
        const long long HerbsAfter = static_cast<long long>(PlayerState.herbs) + 3LL * Batch;
        if (PlayerState.stones < 0 || PlayerState.herbs < 0 || StonesAfter > StateLimits::MaxPersistedInteger ||
            HerbsAfter > StateLimits::MaxPersistedInteger || State.eventSeq >= StateLimits::MaxPersistedInteger) return false;
        auto Herbs = PlayerState.arts.herbs;
        const std::array<std::pair<string, int>, 2> Refunds{{{"幻心草", 2},
            {Contract.destination == "5318" ? "混元果" : "鸡冠草", 1}}};
        for (const auto& [Name, PerBatch] : Refunds) {
            const auto Current = Herbs.find(Name);
            const long long Count = static_cast<long long>(Current == Herbs.end() ? 0 : Current->second) + PerBatch * Batch;
            if (Count < 0 || Count > StateLimits::MaxPersistedInteger) return false;
            Herbs[Name] = static_cast<int>(Count);
        }
        const auto* Furnace = findFurnace(PlayerState.arts.furnace);
        if (!Furnace || PlayerState.arts.durability < 0 || PlayerState.arts.durability > 40) return false;
        // 在途动作禁止换炉/改节点，故可重建开工时‘丹道4且高阶炉炼四阶丹免磨损’条件。
        const bool SpentDurability = !(artHas(PlayerState.arts, "丹", 4) && Furnace->grade > 4);
        const int DurabilityAfter = std::min(40, PlayerState.arts.durability + (SpentDurability ? Batch : 0));
        auto Logs = State.logs;
        LogEntry Notice;
        Notice.id = State.eventSeq + 1; Notice.year = State.year; Notice.age = PlayerState.age;
        Notice.days = State.clock.days; Notice.category = "self"; Notice.important = true;
        Notice.text = "退役丹方制作已取消：返还已付灵石与现存辅材，退役主药按每份112灵石折返；已用天数不退，不发成丹或经验。";
        Notice.delta = "灵石 +" + std::to_string(Contract.cost + RetiredMainHerbValue * Batch) +
            " · 药材 +" + std::to_string(3 * Batch);
        Logs.push_back(std::move(Notice));
        if (Logs.size() > 650) Logs.erase(Logs.begin());
        // 所有分配、数量与补偿先在本次加载的候选局部量核算，提交阶段仅swap/整数赋值。
        PlayerState.arts.herbs.swap(Herbs); State.logs.swap(Logs);
        PlayerState.stones = static_cast<int>(StonesAfter); PlayerState.herbs = static_cast<int>(HerbsAfter);
        PlayerState.arts.durability = DurabilityAfter; ++State.eventSeq;
        State.job.reset();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

// 用途：根据显式版本迁移玩家气血；NPC/副本仍为百分比，在实际动作互写处适配。
void mfMigrateHealthAndPills(const Value& Root, Game& State) {
    auto& PlayerState = State.player;
    const auto BindLegacyBreakthroughPill = [](const Value& Data, auto& PillState, int Stage) {
        if (Data.asObject().contains("breakthroughPillStage")) return;
        if (PillState.breakBuff == "stage" && (Stage == 2 || Stage == 5 || Stage == 8 || Stage == 11))
            PillState.BreakthroughPillStage = Stage;
        else { PillState.breakBuff.clear(); PillState.BreakthroughPillStage = -1; }
    };
    BindLegacyBreakthroughPill(Root["player"]["arts"], PlayerState.arts, PlayerState.stage);
    if (!Root["player"].asObject().contains("pillLifeBonus"))
        PlayerState.PillLifeBonus = clampInt(PlayerState.lifeBonus - PlayerState.build.lifeBonus, 0, 500);
    for (std::size_t Index = 0; Index < State.npcs.size(); ++Index) {
        auto& PersonState = State.npcs[Index];
        if (!Root["npcs"][Index].asObject().contains("pillLifeBonus"))
            PersonState.PillLifeBonus = clampInt(PersonState.lifeBonus, 0, 500);
        BindLegacyBreakthroughPill(Root["npcs"][Index]["pillState"], PersonState.pillState, PersonState.stage);
    }
    if (Root["healthRulesVersion"].asInt(1) >= 2) return;
    PlayerState.health = mfHealthFromPercent(PlayerState.health, mfPlayerHealthMaximum(PlayerState));
    int RetiredHerbs = 0;
    for (const char* Name : {"诱妖草", "霸王花"}) {
        const auto Entry = PlayerState.arts.herbs.find(Name);
        if (Entry != PlayerState.arts.herbs.end()) RetiredHerbs += std::min(PlayerState.herbs, Entry->second);
    }
    PlayerState.herbs = std::max(0, PlayerState.herbs - RetiredHerbs);
    if (!mfMigrateRetiredPills(PlayerState.arts.potions) || !mfMigrateRetiredPills(PlayerState.arts.herbs)) {
        PlayerState.stones = -1; return;
    }
    PlayerState.arts.repel = 0;
    for (auto& PersonState : State.npcs) {
        if (!mfMigrateRetiredPills(PersonState.bag, true)) { PlayerState.stones = -1; return; }
        PersonState.pillState.repel = 0;
    }
    if (!mfMigrateRetiredBrew(Root, State)) State.player.stones = -1;
    // 留影内部人物不是NPC；不改冻结battle.hp/max/intent，只迁移人物快照。
    if (PlayerState.arts.battle && PlayerState.arts.battle->shadowPlayer) {
        Player Shadow = *PlayerState.arts.battle->shadowPlayer;
        Shadow.health = mfHealthFromPercent(Shadow.health, mfPlayerHealthMaximum(Shadow));
        PlayerState.arts.battle->shadowPlayer = std::make_shared<const Player>(std::move(Shadow));
    }
}
// 用途：保留旧人物的当前小境修炼比例；流程：旧修为单位载入；用法：只传旧单位人物；返回：无，宽整数中间值防止扩十倍溢出，非法负值留给状态校验拒绝。
void mfMigratePersonCultivation(Person& PersonState) {
    if (PersonState.xp < 0) return;
    const int Requirement = realmOf(PersonState.stage).need;
    // 化神没有下一阶进度比例，保留终境既有记录，不把合法历史修为清零。
    if (Requirement == 0) return;
    const auto Scaled = static_cast<long long>(PersonState.xp) * CultivationUnits::Scale;
    PersonState.xp = static_cast<int>(std::min(static_cast<long long>(Requirement), Scaled));
}

// 用途：一次性扩大旧修为与未结算小数；流程：载入动作边界；用法：仅缺标记或旧单位的根对象调用；返回：无，不改时间、资源或RNG，也不重放丹药效果。
void mfMigrateCultivationUnits(const Value& Root, Game& State) {
    if (Root["cultivationUnitScale"].asInt(CultivationUnits::LegacyScale) == CultivationUnits::Scale) return;
    mfMigratePersonCultivation(State.player);
    for (auto& PersonState : State.npcs) mfMigratePersonCultivation(PersonState);
    const auto Carry = State.clock.carry.find("xp");
    if (Carry == State.clock.carry.end() || State.player.xp < 0 || realmOf(State.player.stage).need == 0) return;
    // 公共加载入口已拒绝非有限/负数/>1000余数；这里的扩展最多10000，不会溢出int。
    const double Expanded = Carry->second * CultivationUnits::Scale;
    const int Whole = jsFloor(Expanded + 1e-9);
    State.player.xp = static_cast<int>(std::min(static_cast<long long>(realmOf(State.player.stage).need),
        static_cast<long long>(State.player.xp) + Whole));
    Carry->second = std::max(0.0, Expanded - Whole);
}
} // namespace

void migrateLegacySave(const Value& root, Game& game) {
    mfMigrateCultivationUnits(root, game);
    if (root["healthRulesVersion"].asInt(1) < 2) {
        game.Garden.PendingHerbReturns.erase("诱妖草");
        game.Garden.PendingHerbReturns.erase("霸王花");
        for (auto& Plot : game.Garden.Plots)
            if (Plot.HerbId == "6323" || Plot.HerbId == "6324") {
                Plot.HerbId.clear(); Plot.Cohorts.clear();
            }
    }
    crafting::mfMigrateGardenRules(game);
    // 旧版本两个聚气神通共用 gather，实际执行总会选到持续聚气。
    // 带版本标记的新档保留基础聚气；旧档只迁移已有牌，不重置斗法。
    if (root["combatRulesVersion"].asInt(1) < 2) {
        auto migrateSpell = [](string& id) { if (id == "gather") id = "qiFlow"; };
        for (auto& id : game.player.arts.spells) migrateSpell(id);
        for (auto& id : game.player.arts.knownSpells) migrateSpell(id);
        if (game.player.arts.battle) {
            Battle& battle = *game.player.arts.battle;
            for (auto& card : battle.deck) migrateSpell(card.spell);
            for (auto& id : battle.enemySpells) migrateSpell(id);
            migrateSpell(battle.intentSpell);
            for (auto* cooldowns : {&battle.cooldowns, &battle.enemyCd}) {
                const auto old = cooldowns->find("gather");
                if (old != cooldowns->end()) {
                    (*cooldowns)["qiFlow"] = old->second;
                    cooldowns->erase(old);
                }
            }
            battle.maxMana = battleEnergy(game.player);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 3) {
        auto& arts = game.player.arts;
        const int legacySwordExp = static_cast<int>(std::min(10000LL, std::max(0LL, static_cast<long long>(game.player.sword) - 2) * 2));
        if (legacySwordExp > arts.exp["剑"]) artGain(game.player, "剑", legacySwordExp - arts.exp["剑"]);
        for (const auto& sp : allSpells()) if (sp.stage <= game.player.stage && std::find(arts.knownSpells.begin(), arts.knownSpells.end(), sp.id) == arts.knownSpells.end()) arts.knownSpells.push_back(sp.id);
        if (arts.battle) {
            auto& battle = *arts.battle;
            const auto& savedBattle = root["player"]["arts"]["battle"];
            if (savedBattle["enemyStage"].isNull()) {
                battle.enemyStage = battle.enemyCards.enabled ? battle.enemyCards.stage : game.player.stage;
                for (const auto& npc : game.npcs) if (npc.id == battle.npcId) { battle.enemyStage = npc.stage; battle.enemySect = npc.sect; battle.enemyGender = npc.gender; break; }
            }
            if (savedBattle["playerSpeed"].isNull()) battle.playerSpeed = artEscapeSpeed(game.player);
            if (savedBattle["enemySpeed"].isNull()) battle.enemySpeed = battle.enemyCards.enabled ? battle.enemyCards.speed : 20 + battle.enemyStage * 8;
            for (const char* spell : {"strike", "defend"}) {
                int copies = 0; for (const auto& c : battle.deck) if (c.spell == spell) ++copies;
                for (; copies < battleBasicCopies(); ++copies) {
                    string uid = "migration-" + string(spell) + "-" + std::to_string(copies);
                    while (std::any_of(battle.deck.begin(), battle.deck.end(), [&](const Card& c) { return c.uid == uid; })) uid += "-";
                    battle.deck.push_back({uid, spell}); battle.draw.push_back(uid);
                }
            }
        }
        std::set<string> migratedKnown;
        arts.knownSpells.erase(std::remove_if(arts.knownSpells.begin(), arts.knownSpells.end(),
            [&](const string& id) { return !migratedKnown.insert(id).second; }), arts.knownSpells.end());
    }
    if (root["combatRulesVersion"].asInt(1) < 4 && game.job && game.job->actionType == "locationService" && game.job->kind == "auction") {
        if (game.job->origin == "tianji") {
            const int refund = std::max(0, game.job->cost);
            game.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(game.player.stones) + refund));
            game.job.reset();
            logEvent(game, "self", "旧版天机珍品会已改为定向材料秘境情报，取消在途购买并退回" + std::to_string(refund) + "灵石。", true);
        } else if (game.job->origin == "dongshi") {
            const int refund = std::max(0, game.job->cost - 24);
            game.player.stones = static_cast<int>(std::min(1000000000LL, static_cast<long long>(game.player.stones) + refund));
            game.job->cost = 24;
            if (refund > 0) logEvent(game, "self", "伴月楼在途竞购调整为低阶材料交易，退回旧报价差额" + std::to_string(refund) + "灵石。", true);
        }
    }
    if (root["combatRulesVersion"].asInt(1) < 5) migrateDungeonRewards(game);
    mfMigrateHealthAndPills(root, game);
}

void completeLoadedSave(const Value& root, Game& game, bool detached) {
    // 历史版本漏存了结局；恢复摘要时不能再次发放奖励或重复落笔纪事。
    if (game.ended && !game.ending.has_value()) {
        Ending ending;
        ending.cause = "此世已结束"; ending.year = game.year; ending.age = game.player.age;
        ending.realm = realmName(game.player.stage);
        for (const auto& [id, value] : game.player.relations) {
            if (value >= 45) for (const auto& npc : game.npcs) if (npc.id == id) { ++ending.friends; break; }
        }
        ending.score = game.player.highestStage * 120 + game.player.renown * 4 +
                  game.player.insight + game.player.stones / 4;
        game.ending = std::move(ending);
    }

    // 读档后补齐可能缺失的初始化状态
    // A Tianji encounter is deliberately world-free. World initialization can
    // generate NPCs/dungeons, consume RNG, and rewrite a captured loadout.
    if (!detached) {
        ensureArts(game);
        ensureEconomy(game);
        ensureSociety(game);
        ensureAdventure(game);
        ensureDungeons(game);
        if (root["garden"].isNull()) crafting::mfInitializeGarden(game);
    }

    // 老版在途斗法没有保存任务来源，将能够确定的上下文迁移回来。
    if (game.player.arts.battle.has_value()) {
        Battle& battle = *game.player.arts.battle;
        const Value& saved = root["player"]["arts"]["battle"];
        if (battle.kind == "hunt" && saved["huntFee"].isNull()) {
            if (battle.enemy == "任务妖兽") battle.huntFee = 32 + (game.player.stage / 3) * 30;
            else if (battle.enemy == "兽潮先锋" || battle.enemy == "妖将" || battle.enemy == "妖王麾下")
                battle.kind = "tide";
        }
        if (battle.kind == "wild" && battle.advKind.empty() && saved["wildTask"].isNull()) {
            battle.wildTask = "legacy";
            for (const auto& task : data::WildTaskDef_TABLE())
                if (task.name == battle.enemy) { battle.wildTask = task.id; break; }
        }
    }

    if (!detached) {
        // 仅缺失构筑字段的旧NPC名册需要补模板；现代载入保持原样，包括在途对局。
        const auto& roster=root["npcs"].asArray();
        const bool legacyProfiles=std::any_of(roster.begin(),roster.end(),[](const Value& npc) {
            return npc.asObject().find("combatProfileId")==npc.asObject().end();
        });
        if (legacyProfiles) content::ensureChampionNpcs(game);
        // 老世界没有玄道宗时，在已授权读档初始化边界补山门和常态传承者。
        // 留影 detached 不补世界；已有六宗账簿的新档也不重赋人物身份。
        if (!game.npcs.empty() && !game.world.sects.contains("xuandao"))
            content::mfEnsureSectTransmissions(game);
        tide::ensureTide(game);
    }

}

} // namespace nz::persistence
