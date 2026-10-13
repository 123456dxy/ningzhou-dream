#include "core/crafting/herb_acquisition.h"
#include "core/content/herb_habitats.h"
#include "core/sim.h"

#include <algorithm>
#include <array>
#include <map>
#include <string_view>

namespace nz::crafting {
namespace {
// 稳定内容分类：妖丹只能经猎妖/战斗/指定秘境取得，不能成为按日可捡的草药。
constexpr std::array<std::string_view, 35> MonsterCoreIds{
    "8001","8002","8003","8004","8005","8006","8007","8008","8009","8010",
    "8011","8012","8013","8014","8015","8016","8017","8018","8019","8020",
    "8021","8022","8023","8024","8025","8590","8591","8592","8593","8594",
    "8595","8596","8597","8598","8599"};

// 用途：建立只由稳定目录定义决定的取得阶段缓存；流程：首次材料查询；返回：HerbId到阶段的常量表，不创建游戏或消费随机数。
const std::map<string, int>& mfAcquisitionStages() {
    static const std::map<string, int> Stages = [] {
        std::map<string, int> Result;
        for (const auto& Herb : data::HerbDef_TABLE()) {
            Result.emplace(Herb.id, Herb.grade <= 2 ? 0 : clampInt((Herb.grade - 1) * 3, 0, 12));
        }
        for (const auto& Recipe : data::RecipeDef_TABLE()) {
            int UsageStage = mfEarliestRecipeCraftStage(Recipe.id);
            if (UsageStage < 0) continue;
            if (const auto PreparationStage = mfBreakthroughPillStage(Recipe.id)) UsageStage = std::min(UsageStage, *PreparationStage);
            // ingredients 是历史名称存储格式，只在目录边界解析为稳定HerbId；后续规则不匹配展示文字。
            for (const auto& [Name, Count] : parseIngredients(Recipe.ingredients)) {
                const auto* Herb = findHerbByName(Name);
                if (!Herb || Count <= 0) continue;
                const auto Found = Result.find(Herb->id);
                if (Found != Result.end()) Found->second = std::min(Found->second, UsageStage);
            }
        }
        return Result;
    }();
    return Stages;
}
}

// 用途：推导最早可炼阶段；流程：材料难度缓存；返回：符合artTier和合格炉容量的第一个阶段，非法目录返回-1。
int mfEarliestRecipeCraftStage(const string& RecipeId) {
    const auto* Recipe = findRecipe(RecipeId);
    if (!Recipe || Recipe->grade < 1 || Recipe->grade > 6) return -1;
    int RequiredCount = 0;
    for (const auto& [Name, Count] : parseIngredients(Recipe->ingredients)) {
        if (!findHerbByName(Name) || Count <= 0) return -1;
        RequiredCount += Count;
    }
    Player Character;
    for (int Stage = 0; Stage <= 12; ++Stage) {
        Character.stage = Stage;
        const int Tier = artTier(Character);
        if (Recipe->grade > Tier) continue;
        const bool HasFurnace = std::any_of(data::FurnaceDef_TABLE().begin(), data::FurnaceDef_TABLE().end(), [&](const data::FurnaceDef& Furnace) {
            return Furnace.grade >= Recipe->grade && Furnace.grade <= Tier && Furnace.capacity >= RequiredCount;
        });
        if (HasFurnace) return Stage;
    }
    return -1;
}

// 用途：读取稳定药材取得阶段；流程：各来源共同调用；返回：阶段或非法ID的-1，无副作用。
int mfHerbAcquisitionStage(const string& HerbId) {
    const auto& Stages = mfAcquisitionStages();
    const auto Found = Stages.find(HerbId);
    return Found == Stages.end() ? -1 : Found->second;
}

// 用途：识别目录草药；流程：全部野采池过滤；返回：合法1~6品且非妖丹为真。
bool mfIsPlantHerb(const string& HerbId) {
    const auto* Herb = findHerbById(HerbId);
    return Herb && Herb->grade >= 1 && Herb->grade <= 6 &&
        std::find(MonsterCoreIds.begin(), MonsterCoreIds.end(), HerbId) == MonsterCoreIds.end();
}

// 用途：构建当前地方的真实可采池；流程：采药结算；返回：符合阶段与生境的草药，无匹配保留低品兜底，未知地点/境界为空。
vector<const data::HerbDef*> mfGatherableHerbs(const Player& Character, const string& LocationId) {
    vector<const data::HerbDef*> LocalHerbs;
    vector<const data::HerbDef*> OrdinaryHerbs;
    const auto* Location = findLoc(LocationId);
    if (!Location || Character.stage < 0 || Character.stage > 12) return {};
    const bool Wilderness = std::string_view(Location->type) == "wild";
    const auto& HabitatIds = content::mfHerbHabitatIds(LocationId);
    for (const auto& Herb : data::HerbDef_TABLE()) {
        const int Stage = mfHerbAcquisitionStage(Herb.id);
        if (Stage < 0 || Stage > Character.stage || !mfIsPlantHerb(Herb.id)) continue;
        if (Herb.grade <= 2) OrdinaryHerbs.push_back(&Herb);
        if (Wilderness && std::find(HabitatIds.begin(), HabitatIds.end(), Herb.id) != HabitatIds.end()) LocalHerbs.push_back(&Herb);
    }
    return LocalHerbs.empty() ? OrdinaryHerbs : LocalHerbs;
}
} // namespace nz::crafting
