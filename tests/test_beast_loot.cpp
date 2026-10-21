#include "harness.h"

#include "core/arts.h"
#include "core/adventure.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/content/forge_content.h"
#include "core/crafting/material_sources.h"
#include "core/dungeon.h"
#include "core/economy.h"
#include "core/exploration/beast_loot.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"

#include <algorithm>
#include <array>
#include <set>

namespace nztest {
namespace {
using namespace nz;

Game mfBeastLootGame(int PlayerStage = 12) {
    CreateOptions Options;
    Options.seed = 18763;
    Game State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.player.arts.battle.reset();
    State.player.stage = State.player.highestStage = PlayerStage;
    State.player.lifespan = realmOf(PlayerStage).life;
    State.player.health = mfPlayerHealthMaximum(State.player);
    State.player.stones = 100000;
    State.player.location = "wuling";
    State.player.arts.materials.clear();
    return State;
}

Battle mfLootBattle(int EnemyStage = 0, const string& Element = "火") {
    Battle Result;
    Result.enemy = "五部位妖兽"; Result.kind = "wild";
    Result.enemyStage = EnemyStage; Result.enemyDao = Result.style = Element;
    Result.enemyBeast = true; Result.won = true; Result.enemyHp = 0;
    return Result;
}

void mfFinishLootBattle(Game& State, const string& Kind = "wild", const string& Element = "火", int EnemyStage = 0) {
    EnemySpec Enemy;
    Enemy.name = "五部位妖兽"; Enemy.kind = Kind; Enemy.style = Element;
    Enemy.stage = EnemyStage; Enemy.beast = true; Enemy.speed = 0;
    battleStart(State, Enemy);
    if (!State.player.arts.battle) return;
    State.player.arts.battle->enemyHp = 0;
    battleFinish(State, true);
}

int mfPartCount(const map<string, int>& Inventory) {
    int Total = 0;
    for (const auto& [Id, Count] : Inventory) {
        const auto* Definition = content::mfFindForgeMaterial(Id);
        if (Definition && Definition->Source == content::ForgeMaterialSource::BeastDrop) Total += Count;
    }
    return Total;
}

// 用途：保留版本1真实路线的固定快照，验证高境玩家探索旧低境入口后的材料存读档。
// 使用：种子18763、wild-1-1、入场境界6；不重写生产路线生成算法，也不消费活动RNG。
Game mfLegacyBeastDungeonGame() {
    auto State = mfBeastLootGame(0);
    State.dungeons = DungeonWorld{};
    ensureDungeons(State);
    const auto Site = State.dungeons.sites.front();
    State.player.stage = State.player.highestStage = 6;
    State.player.lifespan = realmOf(6).life;
    State.player.health = mfPlayerHealthMaximum(State.player);
    State.player.location = Site.location;
    dungeonEnter(State, Site.id);
    if (!State.dungeons.run) return State;
    auto& Run = *State.dungeons.run;
    Run.LayoutVersion = 1;
    Run.nodes.clear();
    struct LegacyNodeFixture { int Row, Lane; const char* Type; vector<string> Next; };
    const vector<LegacyNodeFixture> Snapshot{
        {0, 0, "battle", {"floor-1-0", "floor-1-1"}},
        {0, 1, "battle", {"floor-1-1", "floor-1-2"}},
        {0, 2, "battle", {"floor-1-2", "floor-1-1"}},
        {1, 0, "event", {"floor-2-0", "floor-2-1"}},
        {1, 1, "chest", {"floor-2-1", "floor-2-0"}},
        {1, 2, "battle", {"floor-2-2", "floor-2-1"}},
        {2, 0, "battle", {"floor-3-0", "floor-3-1"}},
        {2, 1, "elite", {"floor-3-1", "floor-3-0"}},
        {2, 2, "battle", {"floor-3-2", "floor-3-1"}},
        {3, 0, "merchant", {"floor-4-0", "floor-4-1"}},
        {3, 1, "battle", {"floor-4-1"}},
        {3, 2, "chest", {"floor-4-2", "floor-4-1"}},
        {4, 0, "camp", {"floor-5-0", "floor-5-1"}},
        {4, 1, "camp", {"floor-5-1", "floor-5-2"}},
        {4, 2, "camp", {"floor-5-2", "floor-5-1"}},
        {5, 0, "event", {"floor-6-0"}},
        {5, 1, "event", {"floor-6-1", "floor-6-0"}},
        {5, 2, "battle", {"floor-6-2", "floor-6-1"}},
        {6, 0, "elite", {"floor-7-0", "floor-7-1"}},
        {6, 1, "event", {"floor-7-1", "floor-7-2"}},
        {6, 2, "merchant", {"floor-7-2", "floor-7-1"}},
        {7, 0, "merchant", {"floor-8-0"}},
        {7, 1, "event", {"floor-8-1", "floor-8-0"}},
        {7, 2, "battle", {"floor-8-2", "floor-8-1"}},
        {8, 0, "chest", {"floor-9-0", "floor-9-1"}},
        {8, 1, "chest", {"floor-9-1", "floor-9-2"}},
        {8, 2, "elite", {"floor-9-2", "floor-9-1"}},
        {9, 0, "camp", {"floor-10-1"}},
        {9, 1, "camp", {"floor-10-1"}},
        {9, 2, "camp", {"floor-10-1"}},
        {10, 1, "boss", {}}
    };
    for (const auto& Entry : Snapshot) {
        DungeonNode Node;
        Node.id = "floor-" + std::to_string(Entry.Row) + "-" + std::to_string(Entry.Lane);
        Node.row = Entry.Row; Node.lane = Entry.Lane; Node.type = Entry.Type;
        Node.name = "旧路线节点"; Node.text = "旧路线固定快照"; Node.next = Entry.Next;
        Run.nodes.push_back(std::move(Node));
    }
    return State;
}
}

void mfRunBeastLootTests() {
    std::printf("\n--- 妖兽五部位材料真实掉落 ---\n");
    constexpr std::array Elements{"金", "木", "水", "火", "土", "剑", "神", "气"};
    auto QuoteState = mfBeastLootGame();
    for (const auto* Element : Elements) {
        for (int Stage = 0; Stage <= 12; ++Stage) {
            const auto Context = mfLootBattle(Stage, Element);
            const auto Before = saveToJson(QuoteState);
            const auto Loot = exploration::mfQuoteBeastMaterialLoot(QuoteState, Context);
            check(Loot.has_value(), "真实妖兽对应属性和品阶有五部位报价");
            if (!Loot) continue;
            checkEq(Loot->MaterialIds.size(), 5, "每只妖兽恰好掉五种部位，非同物品五份");
            checkEq(Loot->Grade, Stage / 3 + 1, "掉落品阶取敌方实际境界");
            std::set<string> Parts;
            bool Matches = true;
            for (const auto& Id : Loot->MaterialIds) {
                const auto* Definition = content::mfFindForgeMaterial(Id);
                Matches &= Definition && !Definition->AcquisitionPending &&
                    Definition->Source == content::ForgeMaterialSource::BeastDrop &&
                    Definition->Material.grade == Loot->Grade && Definition->Material.element == Element &&
                    Definition->Material.polarity == Loot->Polarity;
                if (Definition) Parts.insert(Definition->PartId);
            }
            check(Matches && Parts == std::set<string>{"skin", "bone", "claw", "eye", "heart"},
                "五部位同品阶同属性同阴阳，不混入矿材");
            checkStr(saveToJson(QuoteState), Before, "重复报价不改人物、RNG、日历或行囊");
            check(exploration::mfQuoteBeastMaterialLoot(QuoteState, Context)->MaterialIds == Loot->MaterialIds,
                "同一冻结战斗的阴阳与部位稳定");
        }
    }
    {
        auto State = mfBeastLootGame();
        const auto Context = mfLootBattle();
        std::set<string> Polarities;
        for (std::uint32_t RandomSnapshot = 0; RandomSnapshot < 32; ++RandomSnapshot) {
            State.rng = RandomSnapshot;
            if (const auto Loot = exploration::mfQuoteBeastMaterialLoot(State, Context)) Polarities.insert(Loot->Polarity);
        }
        checkEq(Polarities.size(), 2, "重复同名同阶妖兽仍可随战斗随机快照获得阴阳两种");
        const auto Loot = exploration::mfQuoteBeastMaterialLoot(State, Context);
        if (Loot) {
            State.player.arts.materials[Loot->MaterialIds.front()] = 1000000000;
            const auto Before = State.player.arts.materials;
            checkEq(exploration::mfAwardBeastMaterialLoot(State, Context), 0, "任一部位满容量时整组无法入袋");
            check(State.player.arts.materials == Before, "容量不足不部分发放其他四部位");
        }
    }
    {
        auto State = mfBeastLootGame();
        auto Context = mfLootBattle(3, "体");
        const auto Loot = exploration::mfQuoteBeastMaterialLoot(State, Context);
        check(Loot && Loot->Element == "气", "纯体道妖兽明确使用气/混元材料");
        for (const string Invalid : {"未知", "丹", "器", "阵"}) {
            Context.enemyDao = Context.style = Invalid;
            check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "未知或非妖兽材属性拒绝掉落");
        }
        Context = mfLootBattle();
        Context.enemyStage = -1;
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "负境界不生成材料");
        Context.enemyStage = 13;
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "超出目录境界不向上夹成合法材料");
        for (const string Kind : {"spar", "rob", "tianji", "sectTrial", "ascensionTournament"}) {
            Context = mfLootBattle(); Context.kind = Kind;
            check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "非击杀战不生成妖兽部位");
        }
        Context = mfLootBattle(); Context.enemyBeast = false;
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "人类不掉妖兽部位");
        Context = mfLootBattle(); Context.won = false;
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "战败不掉材料");
        Context = mfLootBattle(); Context.retreat = true;
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "撤离不掉材料");
        Context = mfLootBattle(); Context.shadowPlayer = std::make_shared<const Player>();
        check(!exploration::mfQuoteBeastMaterialLoot(State, Context), "留影不掉永久部位材料");
    }
    {
        auto State = mfBeastLootGame();
        mfFinishLootBattle(State, "wild", "水", 0);
        checkEq(mfPartCount(State.player.arts.materials), 5, "真实胜利出口发五部位各一份");
        bool OnlyEnemyGrade = true;
        for (const auto& [Id, Count] : State.player.arts.materials) {
            const auto* Definition = content::mfFindForgeMaterial(Id);
            OnlyEnemyGrade &= Definition && Definition->Material.grade == 1 && Definition->Material.element == "水" && Count == 1;
        }
        check(OnlyEnemyGrade, "化神玩家击杀炼气妖兽仍只得一阶水材");
        const auto Once = State.player.arts.materials;
        battleFinish(State, true);
        check(State.player.arts.materials == Once, "重复战斗收尾不会重复发放");
        Game Restored; string Error;
        check(loadDetachedFromJson(saveToJson(State), Restored, &Error), "五部位背包可存读档");
        check(Restored.player.arts.materials == Once, "五部位种类与数量持久化不丢失");
        for (const auto& [Id, Count] : Once)
            check(!crafting::mfCanBuyOrdinaryForgeMaterial(State.player, Id), "部位来源开放后仍不能坊市购买");
    }
    for (const string Outcome : {"human", "loss", "escape", "spar"}) {
        auto State = mfBeastLootGame();
        EnemySpec Enemy;
        Enemy.name = "无部位奖励对手"; Enemy.stage = 0; Enemy.style = "木"; Enemy.speed = 0;
        Enemy.kind = Outcome == "spar" ? "spar" : "wild";
        Enemy.beast = Outcome != "human";
        battleStart(State, Enemy);
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = Outcome == "loss" || Outcome == "escape" ? 10 : 0;
            battleFinish(State, Outcome != "loss" && Outcome != "escape", Outcome == "escape");
        }
        checkEq(mfPartCount(State.player.arts.materials), 0, "真实人类/失败/撤离/切磋收尾不发妖兽部位");
    }
    {
        auto State = mfBeastLootGame();
        Job Contract;
        Contract.actionType = "hunt"; Contract.name = "猎妖委托"; Contract.fee = 100;
        ecoComplete(State, Contract);
        check(State.player.arts.battle && State.player.arts.battle->enemyBeast, "实际猎妖任务生成真妖兽");
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = 0;
            battleFinish(State, true);
        }
        checkEq(mfPartCount(State.player.arts.materials), 5, "真实猎妖任务发适阶五部位");
        checkEq(State.player.arts.materials.size(), 5, "猎妖不再附带旧随机矿物");
        checkEq(State.economy.huntCounts, 1, "更换材料不丢失猎妖胜场");
    }
    {
        auto State = mfBeastLootGame(3);
        Job Contract;
        Contract.kind = "beast"; Contract.name = "驱妖悬赏"; Contract.sect = "水"; Contract.fee = 62;
        societyWildStart(State, Contract);
        check(State.player.arts.battle && State.player.arts.battle->enemyBeast &&
            State.player.arts.battle->enemyDao == "水", "真实驱妖悬赏冻结妖兽身份与任务属性");
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = 0;
            battleFinish(State, true);
        }
        checkEq(mfPartCount(State.player.arts.materials), 5, "驱妖悬赏真实战斗收尾发五部位");
        checkEq(State.player.arts.materials.size(), 5, "驱妖悬赏不再附送旧随机矿材");
        checkEq(State.bonds.wildWins, 1, "驱妖部位结算仍保留悬赏胜场");
    }
    {
        auto State = mfBeastLootGame(3);
        adventureEvent(State, "beast");
        check(State.pending.has_value(), "药径妖兽生成真实奇遇抉择");
        adventureChoose(State, "fight");
        check(State.player.arts.battle && State.player.arts.battle->enemyBeast &&
            State.player.arts.battle->advKind == "beast", "真实奇遇迎战携带妖兽身份与冻结奖励上下文");
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = 0;
            battleFinish(State, true);
        }
        checkEq(mfPartCount(State.player.arts.materials), 5, "药径妖兽真实获胜发五部位");
        checkEq(State.player.arts.materials.size(), 5, "奇遇妖兽不再附送旧矿材");
    }
    {
        auto State = mfBeastLootGame(3);
        State.dungeons = DungeonWorld{};
        ensureDungeons(State);
        const auto Site = State.dungeons.sites.front();
        State.player.location = Site.location;
        check(dungeonEnter(State, Site.id).empty(), "可自然进入隔离材料秘境");
        string NodeId;
        if (State.dungeons.run) {
            for (const auto& Node : State.dungeons.run->nodes)
                if (Node.type == "battle" && dungeonNodeAvailable(*State.dungeons.run, Node.id)) { NodeId = Node.id; break; }
        }
        check(!NodeId.empty() && dungeonChoose(State, NodeId).empty() && State.player.arts.battle,
            "沿真实路线进入妖兽战");
        Battle Completed;
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = 0;
            Completed = *State.player.arts.battle;
            Completed.won = true;
            battleFinish(State, true);
        }
        check(State.dungeons.run && mfPartCount(State.dungeons.run->materials) == 5,
            "秘境妖兽部位先存探索行囊");
        checkEq(mfPartCount(State.player.arts.materials), 0, "未离境不提前永久发放");
        check(dungeonStateError(State).empty(), "新部位探索行囊通过白名单与适阶校验");
        if (State.dungeons.run) {
            const auto Once = State.dungeons.run->materials;
            dungeonBattleComplete(State, Completed, true);
            check(State.dungeons.run->materials == Once, "重复旧秘境战果回调不重复发放五部位");
            Game Invalid = State;
            Invalid.dungeons.run->materials["fm-5-water-yang-heart"] = 1;
            check(!dungeonStateError(Invalid).empty(), "非法超阶妖兽部位不能伪造成探索战利品");
            Invalid = State;
            Invalid.dungeons.run->materials["fm-1-water-yang-metal"] = 1;
            check(!dungeonStateError(Invalid).empty(), "白名单扩展不连带开放新矿材为秘境行囊产出");
        }
        Game Restored; string Error;
        check(loadDetachedFromJson(saveToJson(State), Restored, &Error), "携五部位的在途秘境可读档");
        if (Restored.dungeons.run) {
            check(dungeonLeave(Restored).empty(), "读档后可带材料主动离境");
            checkEq(mfPartCount(Restored.player.arts.materials), 5, "离境一次兑现五部位");
            const auto MaterialsOnce = Restored.player.arts.materials;
            check(!dungeonLeave(Restored).empty() && Restored.player.arts.materials == MaterialsOnce,
                "重复离境不能刷材料");
        }
        if (State.dungeons.run) {
            auto Escaped = State;
            Completed.retreat = true;
            Completed.won = false;
            Escaped.dungeons.run->phase = "battle";
            dungeonBattleComplete(Escaped, Completed, false);
            check(!Escaped.dungeons.run && mfPartCount(Escaped.player.arts.materials) == 0,
                "秘境败退沿用逐种折半，每部位仅1份时损失全部");
        }
    }
    {
        auto State = mfLegacyBeastDungeonGame();
        check(State.dungeons.run && State.dungeons.run->stage == 6 && State.dungeons.sites.front().stage == 0,
            "旧路线夹具保留金丹玩家进入炼气入口的真实上下文");
        checkStr(dungeonStateError(State), "", "版本1固定路线在冲突场景战前合法");
        check(dungeonChoose(State, "floor-0-0").empty() && State.player.arts.battle &&
            State.player.arts.battle->enemyStage == 5 && State.player.arts.battle->enemyBeast,
            "旧路线普通妖兽按冻结入场境界生成筑基后期对手");
        if (State.player.arts.battle) {
            State.player.arts.battle->enemyHp = 0;
            battleFinish(State, true);
        }
        check(State.dungeons.run && mfPartCount(State.dungeons.run->materials) == 5,
            "旧路线真实妖兽胜利获得五部位");
        bool OnlyEnemyGrade = State.dungeons.run.has_value();
        if (State.dungeons.run)
            for (const auto& [Id, Count] : State.dungeons.run->materials) {
                const auto* Definition = content::mfFindForgeMaterial(Id);
                OnlyEnemyGrade &= Definition && Definition->Material.grade == 2 && Count == 1;
            }
        check(OnlyEnemyGrade, "版本1炼气入口的实际筑基妖兽掉二阶而非一阶材料");
        checkStr(dungeonStateError(State), "", "旧路线实际敌阶部位不会被入口品阶误拒绝");
        Game Restored; string Error;
        check(loadDetachedFromJson(saveToJson(State), Restored, &Error), "版本1真实击杀后携二阶部位可读档：" + Error);
        if (State.dungeons.run) {
            check(Restored.dungeons.run && Restored.dungeons.run->materials == State.dungeons.run->materials,
                "旧路线五部位在读档后完整保留");
            auto Invalid = State;
            Invalid.dungeons.run->materials["fm-4-mixed-yang-heart"] = 1;
            check(!dungeonStateError(Invalid).empty(), "版本1仍拒绝高于本路线真实最高敌阶的部位");
            Error.clear();
            check(!loadDetachedFromJson(saveToJson(Invalid), Restored, &Error), "版本1超上限部位不能经存读档洗白");
        }
        auto Current = mfBeastLootGame(0);
        Current.dungeons = DungeonWorld{};
        ensureDungeons(Current);
        Current.player.stage = Current.player.highestStage = 6;
        Current.player.lifespan = realmOf(6).life;
        Current.player.health = mfPlayerHealthMaximum(Current.player);
        const auto Site = Current.dungeons.sites.front();
        Current.player.location = Site.location;
        check(dungeonEnter(Current, Site.id).empty(), "版本2高境进入低境用于限制回归");
        if (Current.dungeons.run) {
            Current.dungeons.run->materials["fm-2-mixed-yang-heart"] = 1;
            check(!dungeonStateError(Current).empty(), "版本2仍按入口品阶拒绝高境玩家伪造二阶部位");
        }
    }
}
} // namespace nztest
