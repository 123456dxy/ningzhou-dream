#include "harness.h"

#include "core/arts.h"
#include "core/adventure.h"
#include "core/content/herb_habitats.h"
#include "core/crafting/herb_acquisition.h"
#include "core/dungeon.h"
#include "core/economy.h"
#include "core/location_services.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/source_limits.h"

#include <algorithm>
#include <array>

namespace nztest {
namespace {
// 用途：创建不关联现役存档的材料夹具；流程：真实情报与采集测试；返回：指定境界天机阁Game值。
nz::Game mfHerbSourceGame(int Stage) {
    nz::CreateOptions Options; Options.seed = 3561; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    State.player.stage = State.player.highestStage = Stage;
    State.player.lifespan = nz::realmOf(Stage).life + State.player.lifeBonus;
    State.player.location = "tianji"; State.player.stones = 100000;
    return State;
}
}

// 用途：锁定低阶丹方与突破准备的材料可得性；流程：目录/真实采购/入场/读档；返回：无，失败计入测试框架。
void mfRunHerbAcquisitionTests() {
    using namespace nz;
    std::printf("\n--- 丹方材料取得境界与草药分类 ---\n");
    const Game Base = mfHerbSourceGame(2);
    const auto Options = dungeonIntelOptions(Base);
    const std::array<std::pair<string, int>, 6> Expected{
        std::pair<string, int>{"6208", 2}, {"6202", 2}, {"6211", 2}, {"6201", 2}, {"6205", 0}, {"6301", 2}};
    for (const auto& [HerbId, Stage] : Expected) {
        const auto Option = std::find_if(Options.begin(), Options.end(), [&](const DungeonIntelOption& Entry) {
            return Entry.kind == "herb" && Entry.id == HerbId;
        });
        check(Option != Options.end(), "低阶丹方的高品材料保留天机阁来源：" + HerbId);
        if (Option == Options.end()) continue;
        checkEq(Option->stage, Stage, "材料难度不晚于真实早期丹方用途：" + HerbId);
        Game State = Base;
        check(dungeonIntel(State, "herb", HerbId).empty(), "真实购买低阶丹方定向情报");
        const auto Site = std::find_if(State.dungeons.sites.begin(), State.dungeons.sites.end(), [&](const DungeonSite& Entry) {
            return Entry.type == "intel" && Entry.targetId == HerbId;
        });
        check(Site != State.dungeons.sites.end(), "情报创建真实指定药材入口");
        if (Site == State.dungeons.sites.end()) continue;
        const string SiteId = Site->id;
        State.player.location = Site->location;
        check(dungeonEnter(State, SiteId).empty(), "炼气后期可以进入筑基丹及低阶丹方材料秘境");
        check(dungeonStateError(State).empty(), "降低难度的指定药材探索状态仍合法");
        Game Restored; string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "低阶材料目标探索可真实保存读回：" + Error);
    }
    for (const auto& Option : Options) if (Option.kind == "material") {
        checkEq(Option.stage, std::min(12, (Option.grade - 1) * 3), "高阶炼器材料的探索难度不受丹方平衡影响");
    }
    {
        bool CatalogValid = true;
        bool IngredientsReady = true;
        bool BreakthroughIngredientsReady = true;
        for (const auto& Recipe : data::RecipeDef_TABLE()) {
            const int BrewStage = crafting::mfEarliestRecipeCraftStage(Recipe.id);
            CatalogValid = CatalogValid && BrewStage >= 0 && BrewStage <= 12;
            for (const auto& [Name, Count] : parseIngredients(Recipe.ingredients)) {
                const auto* Herb = findHerbByName(Name);
                const int Stage = Herb ? crafting::mfHerbAcquisitionStage(Herb->id) : -1;
                IngredientsReady = IngredientsReady && Count > 0 && Stage >= 0 && Stage <= BrewStage;
                if (const auto PreparationStage = mfBreakthroughPillStage(Recipe.id)) {
                    BreakthroughIngredientsReady = BreakthroughIngredientsReady && Stage >= 0 && Stage <= *PreparationStage;
                }
            }
        }
        checkEq(data::RecipeDef_TABLE().size(), 123, "完整123丹方目录参与用途阶段审计");
        check(CatalogValid, "全目录丹方均有满足品阶与容量的合法开炉阶段");
        check(IngredientsReady, "全目录药材最低获取阶段不晚于真实最早开炉阶段");
        check(BreakthroughIngredientsReady, "筑基/凝丹/结婴/化神材料在稳定ID准备门槛前可取得");
        checkEq(crafting::mfHerbAcquisitionStage("no-herb"), -1, "未知药材ID明确拒绝");
        checkEq(crafting::mfEarliestRecipeCraftStage("no-recipe"), -1, "未知丹方ID明确拒绝");
        check(!crafting::mfIsPlantHerb("no-herb"), "未知药材不是可采草药");
        checkEq(crafting::mfHerbAcquisitionStage("6501"), 11, "六品丹用途材料保持元婴后期门槛，不全放炼气");
        checkEq(crafting::mfHerbAcquisitionStage("6502"), 12, "未被早期丹方使用的六品材料保持原化神难度");
        check(!ordinaryHerbError(Base.player, 3).empty(), "用途难度降低不放开普通坊市三品药材直购");
    }
    {
        const string Before = saveToJson(Base);
        bool HabitatValid = true;
        bool NoMonsterCores = true;
        bool NoCityHighHerbs = true;
        Player Character = Base.player;
        Character.stage = 12;
        for (const auto& Location : LOCATIONS()) {
            const auto Pool = crafting::mfGatherableHerbs(Character, Location.id);
            const auto& Habitat = content::mfHerbHabitatIds(Location.id);
            for (const auto& HerbId : Habitat) HabitatValid = HabitatValid && findHerbById(HerbId) && crafting::mfIsPlantHerb(HerbId);
            for (const auto* Herb : Pool) {
                NoMonsterCores = NoMonsterCores && crafting::mfIsPlantHerb(Herb->id);
                if (string(Location.type) != "wild") NoCityHighHerbs = NoCityHighHerbs && Herb->grade <= 2;
                if (Herb->grade > 2) HabitatValid = HabitatValid && std::find(Habitat.begin(), Habitat.end(), Herb->id) != Habitat.end();
            }
        }
        check(HabitatValid, "运行采药只按显式地点/药材ID生境，不以地点显示名匹配");
        check(NoMonsterCores, "任何境界任何地点的采集池都不包含妖丹");
        check(NoCityHighHerbs, "城市/山门采药不会兜底生成异地高品草药");
        const auto Lianshi = crafting::mfGatherableHerbs(Base.player, "lianshi");
        check(std::any_of(Lianshi.begin(), Lianshi.end(), [](const auto* Herb) { return Herb->id == "6208"; }), "炼气后期连石山生境确有筑基丹玄冰花");
        const auto Fenglei = crafting::mfGatherableHerbs(Base.player, "fenglei");
        check(std::any_of(Fenglei.begin(), Fenglei.end(), [](const auto* Herb) { return Herb->id == "6211"; }), "炼气后期风雷谷生境确有筑基丹幻心草");
        check(crafting::mfGatherableHerbs(Base.player, "unknown-location").empty(), "未知地点不能生成材料池");
        check(saveToJson(Base) == Before, "重复材料用途与生境查询不推进日期/RNG或写状态");
        for (const string LocationId : {"lianshi", "wuling", "sea"}) {
            Game State = Base;
            State.player.location = LocationId;
            State.player.arts.herbs.clear(); State.player.herbs = 0;
            State.world.herbs = 100000;
            if (LocationId != "wuling") {
                Job Task; Task.kind = "gather"; Task.origin = LocationId; Task.name = "本地采药";
                for (int ActionIndex = 0; ActionIndex < 100; ++ActionIndex) locationServiceComplete(State, Task);
            } else {
                // 真实API建立月计划不写origin，必须从当前合法地点获取采集池。
                Job Task; Task.actionType = "plan"; Task.plan = "gather"; Task.total = 30;
                monthComplete(State, Task);
                check(!State.player.arts.herbs.empty(), "真实API形状的无origin月计划仍发具体药材，不只增加泛用总量");
            }
            adventureHarvestHerbs(State, 99);
            bool AllAllowed = true;
            bool CollectedHigh = false;
            const auto Pool = crafting::mfGatherableHerbs(State.player, LocationId);
            for (const auto& [Name, Count] : State.player.arts.herbs) {
                const auto* Herb = findHerbByName(Name);
                AllAllowed = AllAllowed && Herb && crafting::mfIsPlantHerb(Herb->id) && Count > 0 &&
                    std::any_of(Pool.begin(), Pool.end(), [&](const auto* Entry) { return Entry->id == Herb->id; });
                CollectedHigh = CollectedHigh || (Herb && Herb->grade > 2);
            }
            check(AllAllowed, LocationId + "真实地点/月计划/奇遇结算只发权威池中的草药");
            if (LocationId == "lianshi") check(CollectedHigh, "真实野外采集能获得提前解锁且符合本地生境的高品药材");
            if (LocationId == "wuling") check(!CollectedHigh, "真实城中采药保持一二品草药供应");
        }
    }
    {
        const std::array<int, 6> CraftStages{0, 0, 2, 5, 8, 11};
        for (int Grade = 1; Grade <= 6; ++Grade) {
            const auto Recipe = std::find_if(data::RecipeDef_TABLE().begin(), data::RecipeDef_TABLE().end(), [&](const data::RecipeDef& Entry) { return Entry.grade == Grade; });
            if (Recipe == data::RecipeDef_TABLE().end()) continue;
            const auto BrewCount = artBrewInfo(Base.player, Recipe->id).count;
            const auto Furnace = std::find_if(data::FurnaceDef_TABLE().begin(), data::FurnaceDef_TABLE().end(), [&](const data::FurnaceDef& Entry) {
                return Entry.grade >= Grade && Entry.capacity >= BrewCount;
            });
            check(Furnace != data::FurnaceDef_TABLE().end(), "各品丹方存在满足容量的炉具");
            if (Furnace == data::FurnaceDef_TABLE().end()) continue;
            Game State = mfHerbSourceGame(CraftStages[Grade - 1]);
            State.player.arts.furnace.clear();
            const string Before = saveToJson(State);
            const auto FurnaceOptions = shop::buyableFurnaces(State);
            check(std::find(FurnaceOptions.begin(), FurnaceOptions.end(), &*Furnace) != FurnaceOptions.end(), "开炉最早境界的丹炉购买目录包含同品炉具");
            check(saveToJson(State) == Before, "丹炉购买目录纯查询不扣款或改炉具");
            const int Stones = State.player.stones;
            check(shop::buyFurnace(State, Furnace->id).ok, "开炉最早境界可真实购买同品丹炉");
            checkEq(State.player.stones, Stones - Furnace->price, "丹炉解锁修复不改既有购买价格");
            for (const auto& [Name, Count] : parseIngredients(Recipe->ingredients)) State.player.arts.herbs[Name] += Count;
            State.player.herbs = 100;
            check(ecoActionError(State, "brew", Recipe->id).empty(), "最早用途境界配齐材料后通过真实开炉资格");
            ecoBegin(State, "brew", Recipe->id);
            check(State.job && State.job->kind == "brew", "最早用途境界可以建立同品真实炼丹工作");
            if (CraftStages[Grade - 1] > 0) {
                Game Below = mfHerbSourceGame(CraftStages[Grade - 1] - 1);
                Below.player.arts.furnace.clear();
                const string Unchanged = saveToJson(Below);
                check(!shop::buyFurnace(Below, Furnace->id).ok, "低于统一开炉品阶仍不能购入高品丹炉");
                check(saveToJson(Below) == Unchanged, "境界不足拒购不部分扣款或更换丹炉");
            }
            Game Poor = mfHerbSourceGame(CraftStages[Grade - 1]);
            Poor.player.arts.furnace.clear(); Poor.player.stones = Furnace->price - 1;
            const string Unchanged = saveToJson(Poor);
            check(!shop::buyFurnace(Poor, Furnace->id).ok && saveToJson(Poor) == Unchanged, "丹炉资金不足拒购仍保持原子性");
        }
    }
}
} // namespace nztest
