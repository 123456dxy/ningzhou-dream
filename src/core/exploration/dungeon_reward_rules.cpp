#include "core/exploration/dungeon_reward_rules.h"
#include "core/model.h"
#include "core/crafting/herb_acquisition.h"
#include "core/content/green_bottle_content.h"
#include "data/reference_tables.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace nz {
namespace {
constexpr int MaximumEntryStage = 12;
constexpr int StagesPerMajorRealm = 3;
constexpr std::array<int,5> CurrencyScales{1,3,9,27,81};
constexpr int BaseMainStones = 250;
constexpr int BaseBattleStones = 12;
constexpr int BaseEliteBonusStones = 18;
constexpr int BaseBossBonusStones = 50;
constexpr int BaseCacheStones = 15;
constexpr int SeaMainBonusPercent = 40;
constexpr int TianmoMainBonusPercent = 72;
constexpr int LegacyStonesPerMinorStage = 80;
constexpr int LegacySeaMainBonusStones = 100;
constexpr int LegacyTianmoMainBonusStones = 180;

// 用途：拒绝未知秘境类型，不将伪造类型降级为普通；流程：纯规则与冻结校验；返回：是否属于真实目录。
bool mfHasDungeonType(std::string_view Type) {
    return Type == "wild" || Type == "intel" || Type == "sea" || Type == "tianmo" || Type == "bottle";
}
}

// 用途：提供以入口大境界为唯一档位来源的货币与材料规则；流程：新冻结/真实结算/预览。
// 使用：合法stage/type；返回：完整规则表，非法输入抛invalid_argument，无活动状态副作用。
DungeonRewardRules mfDungeonRewardRulesForStage(int EntryStage, std::string_view Type) {
    if (EntryStage < 0 || EntryStage > MaximumEntryStage || !mfHasDungeonType(Type))
        throw std::invalid_argument("秘境奖励入口境界或类型无效。");
    const int Major = EntryStage / StagesPerMajorRealm;
    DungeonRewardRules Rules;
    Rules.CurrencyScale = CurrencyScales[Major]; Rules.MaterialGrade = Major + 1;
    Rules.MainStones = BaseMainStones * Rules.CurrencyScale;
    const int BonusPercent = Type == "sea" ? SeaMainBonusPercent : Type == "tianmo" ? TianmoMainBonusPercent : 0;
    Rules.MainStones += Rules.MainStones * BonusPercent / 100;
    Rules.BattleStones = BaseBattleStones * Rules.CurrencyScale;
    Rules.EliteBonusStones = BaseEliteBonusStones * Rules.CurrencyScale;
    Rules.BossBonusStones = BaseBossBonusStones * Rules.CurrencyScale;
    Rules.CacheStones = BaseCacheStones * Rules.CurrencyScale;
    return Rules;
}

// 用途：同时接受既有线性承诺与新五档主奖；流程：持久化冻结奖励检查。
// 使用：传实际amount；返回：仅明确白名单金额为真，不重算写回name/desc或已领奖记录。
bool mfIsFrozenDungeonStoneReward(int EntryStage, std::string_view Type, int Amount) {
    if (EntryStage < 0 || EntryStage > MaximumEntryStage || !mfHasDungeonType(Type) || Amount < 0) return false;
    const int Legacy = BaseMainStones + EntryStage * LegacyStonesPerMinorStage +
        (Type == "sea" ? LegacySeaMainBonusStones : Type == "tianmo" ? LegacyTianmoMainBonusStones : 0);
    return Amount == Legacy || Amount == mfDungeonRewardRulesForStage(EntryStage, Type).MainStones;
}

// 用途：从冻结入口读取奖励境界；流程：沿途收益和外部事件接线。
// 使用：不以人物或run.stage作为缺失兜底；返回：合法入口stage，不存在/异常时-1。
int mfDungeonEntryRewardStage(const Game& State, const DungeonRun& Run) {
    const auto Site = std::find_if(State.dungeons.sites.begin(), State.dungeons.sites.end(),
        [&](const DungeonSite& Entry) { return Entry.id == Run.siteId; });
    return Site == State.dungeons.sites.end() || Site->stage < 0 || Site->stage > MaximumEntryStage ? -1 : Site->stage;
}

// 用途：按入口同阶和真实取得门槛生成草药池；流程：全部普通材料掉落共用。
// 使用：合法stage；返回：最高可用同阶草药，缺阶向下降，不把更高阶或妖丹充作可采草药。
std::vector<const data::HerbDef*> mfDungeonHerbRewardPool(int EntryStage) {
    const int PreferredGrade = mfDungeonRewardRulesForStage(EntryStage).MaterialGrade;
    for (int Grade = PreferredGrade; Grade >= 1; --Grade) {
        std::vector<const data::HerbDef*> Pool;
        for (const auto& Herb : data::HerbDef_TABLE()) {
            const int AcquisitionStage = crafting::mfHerbAcquisitionStage(Herb.id);
            if (Herb.grade == Grade && crafting::mfIsPlantHerb(Herb.id) && AcquisitionStage >= 0 && AcquisitionStage <= EntryStage)
                Pool.push_back(&Herb);
        }
        if (!Pool.empty()) return Pool;
    }
    return {};
}

// 用途：按入口同阶生成普通矿材池；流程：全部普通材料掉落共用。
// 使用：合法stage；返回：最高可用且不高于适阶的池，瓶专属灵材与六阶材料不混入。
std::vector<const data::MaterialDef*> mfDungeonMaterialRewardPool(int EntryStage) {
    const int PreferredGrade = mfDungeonRewardRulesForStage(EntryStage).MaterialGrade;
    for (int Grade = PreferredGrade; Grade >= 1; --Grade) {
        std::vector<const data::MaterialDef*> Pool;
        for (const auto& Material : data::MaterialDef_TABLE())
            if (Material.grade == Grade && !content::mfFindGreenBottleMaterial(Material.id)) Pool.push_back(&Material);
        if (!Pool.empty()) return Pool;
    }
    return {};
}
} // namespace nz
