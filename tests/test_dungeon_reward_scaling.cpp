#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/dungeon.h"
#include "core/exploration/dungeon_reward_rules.h"
#include "core/crafting/herb_acquisition.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace nztest {
namespace {
using namespace nz;
constexpr std::array<int,5> Stages{0,3,6,9,12};
constexpr std::array<int,5> MainStones{250,750,2250,6750,20250};
constexpr std::array<int,5> BattleStones{12,36,108,324,972};
constexpr std::array<int,5> EliteStones{18,54,162,486,1458};
constexpr std::array<int,5> BossStones{50,150,450,1350,4050};

// 用途：真实生成指定入口境界，再按需提升角色；流程：收益与低阶刷取回归。
// 使用：传入口/玩家阶段；返回：隔离内存Game，不访问服务、存档或检查点。
Game mfRewardFixture(int EntryStage, int PlayerStage = -1) {
    CreateOptions Options; Options.seed = 115981; Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.checkpoint = AutoCheckpoint{};
    State.player.traits.clear(); State.player.talent.clear(); State.player.arts.battle.reset();
    State.player.stage = State.player.highestStage = EntryStage;
    State.player.lifespan = realmOf(EntryStage).life; State.player.stones = 100000;
    State.player.health = 100; State.player.arts.speed = 500;
    State.dungeons = DungeonWorld{}; ensureDungeons(State);
    State.player.location = State.dungeons.sites.front().location;
    if (PlayerStage >= 0) {
        State.player.stage = State.player.highestStage = PlayerStage;
        State.player.lifespan = realmOf(PlayerStage).life;
    }
    return State;
}

// 用途：统计探索行囊中的普通材料份数；流程：普通/精英奖励增量检查；返回：药材矿材总份数。
int mfStashMaterialCount(const DungeonRun& Run) {
    int Count = 0;
    for (const auto& [Id, Amount] : Run.herbs) Count += Amount;
    for (const auto& [Id, Amount] : Run.materials) Count += Amount;
    return Count;
}

// 用途：按原动作边界跳过非最终奖励继续探索；流程：真实路线测试；返回：可以继续为真。
bool mfSkipNodeReward(Game& State) {
    if (!State.dungeons.run || State.dungeons.run->phase != "reward" || State.dungeons.run->cleared) return false;
    return dungeonClaim(State, "skip").empty();
}

// 用途：沿真实连线经过第二层精英并触发每个战斗收口；流程：奖励集成回归。
// 使用：显式赢家夹具验证结算，不作为自然战斗胜率；返回：通关且仍待选最终奖励为真。
bool mfFinishRewardRoute(Game& State, int ExpectedEntryStage, bool CheckCombatAmounts = true) {
    if (!State.dungeons.run) return false;
    const int Major = ExpectedEntryStage / 3;
    const auto Elite = std::find_if(State.dungeons.run->nodes.begin(), State.dungeons.run->nodes.end(),
        [](const auto& Node) { return Node.row == 2 && Node.type == "elite"; });
    if (Elite == State.dungeons.run->nodes.end()) return false;
    const int PreferredLane = Elite->lane;
    bool SawRegular = false, SawElite = false, SawBoss = false;
    for (int Guard = 0; Guard < 60 && State.dungeons.run; ++Guard) {
        auto& Run = *State.dungeons.run;
        if (Run.cleared) return Run.phase == "reward" && SawRegular && SawElite && SawBoss;
        if (Run.phase == "reward") { if (!mfSkipNodeReward(State)) return false; continue; }
        if (Run.phase != "route") return false;
        auto Node = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const auto& Entry) {
            return Entry.lane == PreferredLane && dungeonNodeAvailable(Run, Entry.id);
        });
        if (Node == Run.nodes.end()) Node = std::find_if(Run.nodes.begin(), Run.nodes.end(),
            [&](const auto& Entry) { return dungeonNodeAvailable(Run, Entry.id); });
        if (Node == Run.nodes.end()) return false;
        const string Type = Node->type, NodeId = Node->id;
        const int BeforeStones = Run.stones, BeforeMaterials = mfStashMaterialCount(Run), BeforeCores = Run.cores;
        if (!dungeonChoose(State, NodeId).empty()) return false;
        if (State.player.arts.battle) {
            State.player.arts.battle->hp = State.player.arts.battle->max;
            State.player.arts.battle->enemyHp = 0;
            battleFinish(State, true);
            if (!State.dungeons.run) return false;
            SawRegular = SawRegular || Type == "battle"; SawElite = SawElite || Type == "elite"; SawBoss = SawBoss || Type == "boss";
            if (CheckCombatAmounts) {
                const int Expected = BattleStones[Major] + (Type == "elite" ? EliteStones[Major] : Type == "boss" ? BossStones[Major] : 0);
                checkEq(State.dungeons.run->stones - BeforeStones, Expected, "真实" + Type + "灵石按入口大境界结算");
                if (Type != "boss") checkEq(mfStashMaterialCount(*State.dungeons.run) - BeforeMaterials,
                    Type == "elite" ? 2 : 1, "普通战一份/精英两份材料，随入口适阶");
                if (Type == "boss" && ExpectedEntryStage < 3)
                    checkEq(State.dungeons.run->cores, BeforeCores, "低阶首领不凭高境人物run.stage追加灵核");
            }
        }
    }
    return false;
}

// 用途：将新生成奖励还原为更新前真实冻结文本；流程：旧承诺兼容测试；返回：无，仅改临时入口。
void mfUseLegacyFrozenReward(DungeonSite& Site) {
    const int Legacy = 250 + Site.stage * 80 + (Site.type == "sea" ? 100 : Site.type == "tianmo" ? 180 : 0);
    const int Current = mfDungeonRewardRulesForStage(Site.stage, Site.type).MainStones;
    for (auto& Reward : Site.bossRewards) {
        const string From = std::to_string(Current), To = std::to_string(Legacy);
        std::size_t Position = 0;
        while ((Position = Reward.desc.find(From, Position)) != string::npos) {
            Reward.desc.replace(Position, From.size(), To); Position += To.size();
        }
        if (Reward.kind == "stones") { Reward.amount = Legacy; Reward.name = "灵石宝藏·" + To; }
    }
}
}

// 用途：验证五档收益、适阶材料、真实战斗与旧冻结承诺；流程：秘境收益定向回归。
// 返回：无，harness记录；所有夹具为内存状态，不接触当前玩家。
void mfRunDungeonRewardScalingTests() {
    using namespace nz;
    std::printf("\n--- 秘境入口境界收益 ---\n");
    for (std::size_t Major = 0; Major < Stages.size(); ++Major) {
        const int Stage = Stages[Major]; const auto Rules = mfDungeonRewardRulesForStage(Stage);
        checkEq(Rules.MainStones, MainStones[Major], "五境主灵石奖励按三倍档递增");
        checkEq(Rules.BattleStones, BattleStones[Major], "五境普通战奖励按三倍档递增");
        checkEq(Rules.EliteBonusStones, EliteStones[Major], "五境精英额外奖励按三倍档递增");
        checkEq(Rules.BossBonusStones, BossStones[Major], "五境首领额外奖励按三倍档递增");
        checkEq(Rules.MaterialGrade, Major + 1, "沿途普通材料匹配一至五阶");
        checkEq(Rules.CacheStones, 15 * Rules.CurrencyScale, "宝箱等货币档位共用同一个倍率");
        checkEq(mfDungeonRewardRulesForStage(Stage, "sea").MainStones, MainStones[Major] * 140 / 100, "海域主奖额外四成");
        checkEq(mfDungeonRewardRulesForStage(Stage, "tianmo").MainStones, MainStones[Major] * 172 / 100, "魔眼主奖额外七成二");
        const auto Herbs = mfDungeonHerbRewardPool(Stage);
        const auto Materials = mfDungeonMaterialRewardPool(Stage);
        check(!Herbs.empty() && !Materials.empty(), "每档具有实际可掉落的适阶草药/矿材");
        for (const auto* Herb : Herbs) check(Herb->grade == Rules.MaterialGrade && crafting::mfIsPlantHerb(Herb->id) &&
            crafting::mfHerbAcquisitionStage(Herb->id) <= Stage, "草药池同阶且不绕过真实用途/采集门槛");
        for (const auto* Material : Materials) checkEq(Material->grade, Rules.MaterialGrade, "矿材池匹配入口品阶，不混入六阶");
        Game State = mfRewardFixture(Stage); const string SiteId = State.dungeons.sites.front().id;
        const auto& Frozen = State.dungeons.sites.front().bossRewards;
        for (const auto& Reward : Frozen) {
            check(Reward.desc.find(std::to_string(Rules.MainStones)) != string::npos, "新冻结主奖与传承折算文案使用同一实际金额");
            if (Reward.kind == "stones") checkEq(Reward.amount, MainStones[Major], "新生成入口冻结正确主灵石奖励");
        }
        check(dungeonEnter(State, SiteId).empty(), "进入真实生成的适境秘境");
        check(mfFinishRewardRoute(State, Stage), "逐层真实普通/精英/首领收口到最终奖励");
        if (State.dungeons.run) {
            for (const auto& [Name, Count] : State.dungeons.run->herbs) checkEq(findHerbByName(Name)->grade, Major + 1, "真实沿途草药不再固定一二阶");
            for (const auto& [Id, Count] : State.dungeons.run->materials) {
                const auto Item = std::find_if(data::MaterialDef_TABLE().begin(), data::MaterialDef_TABLE().end(), [&](const auto& Entry) { return Entry.id == Id; });
                check(Item != data::MaterialDef_TABLE().end() && Item->grade == static_cast<int>(Major + 1), "真实沿途矿材不再固定一二阶");
            }
            check(dungeonStateError(State).empty(), "新适阶材料与通关状态通过持久化校验");
        }
    }
    {
        Game State = mfRewardFixture(0, 12); const string SiteId = State.dungeons.sites.front().id;
        check(dungeonEnter(State, SiteId).empty(), "高境角色可真实进入低阶入口");
        checkEq(mfDungeonEntryRewardStage(State, *State.dungeons.run), 0, "奖励读入口境界，忽略人物run.stage十二");
        check(mfFinishRewardRoute(State, 0), "高境打低阶仍只结算炼气战利品");
        if (State.dungeons.run) {
            check(dungeonStateError(State).empty(), "高境进低阶生成的适阶收益仍可保存");
        }
        Game Sweep = mfRewardFixture(0, 12); const string SweepId = Sweep.dungeons.sites.front().id;
        check(mfSweepDungeon(Sweep, SweepId).empty(), "高境扫荡低阶入口成功");
        check(Sweep.dungeons.run && Sweep.dungeons.run->stones == 0, "扫荡不获得按人物境界虚增的沿途灵石");
        check(dungeonClaim(Sweep, "boss-stones").empty() && Sweep.dungeons.run->stones == 250, "扫荡仅领取原入口炼气主奖250");
    }
    {
        Game State = mfRewardFixture(6); auto& Site = State.dungeons.sites.front(); mfUseLegacyFrozenReward(Site);
        const string SiteId = Site.id;
        check(dungeonStateError(State).empty(), "旧线性公式冻结主奖仍合法");
        check(dungeonEnter(State, SiteId).empty(), "旧冻结入口可正常进入");
        State.dungeons.run->materials[data::MaterialDef_TABLE().front().id] = 1;
        check(dungeonStateError(State).empty(), "旧行囊低阶材料在新规则下继续有效");
        const string Saved = saveToJson(State); const auto RngBefore = State.rng;
        for (int Repeat = 0; Repeat < 4; ++Repeat) {
            mfDungeonRewardRulesForStage(6); mfDungeonHerbRewardPool(6); mfDungeonMaterialRewardPool(6);
            mfDungeonEntryRewardStage(State, *State.dungeons.run); dungeonStateError(State);
        }
        checkStr(saveToJson(State), Saved, "收益和材料目录查询不改变已冻结内容或状态");
        checkEq(State.rng, RngBefore, "收益纯查询不消费共享RNG");
        Game Restored; string Error;
        check(loadDetachedFromJson(Saved, Restored, &Error), "旧冻结主奖与低阶loot可以存读档：" + Error);
        if (Restored.dungeons.run) checkStr(saveToJson(Restored), Saved, "读档不将旧主奖金额或文案暗改为新表");
        for (const auto& Reward : State.dungeons.run->bossRewards)
            if (Reward.kind == "stones") checkEq(Reward.amount, 730, "旧金丹主奖承诺保留730，不抬成2250");
    }
    {
        Game State = mfRewardFixture(0, 12); const string SiteId = State.dungeons.sites.front().id;
        check(dungeonEnter(State, SiteId).empty(), "建立旧高境探索低阶入口的兼容夹具");
        const auto Material = std::find_if(data::MaterialDef_TABLE().begin(), data::MaterialDef_TABLE().end(), [](const auto& Entry) { return Entry.grade == 2; });
        if (State.dungeons.run && Material != data::MaterialDef_TABLE().end()) {
            State.dungeons.run->materials[Material->id] = 1;
            check(dungeonStateError(State).empty(), "旧规则已拿到的二阶材料不会因入口低阶被更新删掉");
        }
    }
    {
        Game State = mfRewardFixture(12); State.player.location = "tianji";
        const auto Options = dungeonIntelOptions(State);
        const auto Target = std::find_if(Options.begin(), Options.end(), [](const auto& Entry) { return Entry.enabled && Entry.kind == "herb" && Entry.grade == 6; });
        check(Target != Options.end(), "六阶药材仍通过真实指定情报提供");
        if (Target != Options.end()) {
            check(dungeonIntel(State, Target->kind, Target->id).empty(), "真实购买六阶定向秘境");
            const auto Site = State.dungeons.sites.back(); State.player.location = Site.location;
            check(dungeonEnter(State, Site.id).empty() && mfFinishRewardRoute(State, 12, false), "真实通关保留六阶指定材料机制");
            if (State.dungeons.run) {
                const auto Found = State.dungeons.run->herbs.find(Site.targetName);
                check(State.dungeons.run->targetGranted && Found != State.dungeons.run->herbs.end() && Found->second == 10,
                    "六阶目标恰十份，不被普通五阶掉落上限截断");
                check(dungeonStateError(State).empty(), "六阶定向目标与普通适阶材料共存仍合法");
            }
        }
    }
    for (const int Stage : {-1, 13}) {
        bool Rejected = false; try { mfDungeonRewardRulesForStage(Stage); } catch (const std::invalid_argument&) { Rejected = true; }
        check(Rejected, "非法奖励境界明确拒绝");
    }
    for (int Stage = 0; Stage <= 12; ++Stage) for (const string Type : {"wild", "intel", "sea", "tianmo", "bottle"}) {
        const int Legacy = 250 + Stage * 80 + (Type == "sea" ? 100 : Type == "tianmo" ? 180 : 0);
        const int Current = mfDungeonRewardRulesForStage(Stage, Type).MainStones;
        check(mfIsFrozenDungeonStoneReward(Stage, Type, Legacy) && mfIsFrozenDungeonStoneReward(Stage, Type, Current), "新旧冻结主奖白名单均接受");
        check(!mfIsFrozenDungeonStoneReward(Stage, Type, -1) && !mfIsFrozenDungeonStoneReward(Stage, Type, Current + 1), "负数和非公式冻结主奖拒绝");
    }
}
} // namespace nztest
