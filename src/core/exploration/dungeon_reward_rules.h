#pragma once
#include <string_view>
#include <vector>

namespace nz {
struct Game;
struct DungeonRun;
namespace data { struct HerbDef; struct MaterialDef; }

// 入口大境界的统一收益表；精英/首领Bonus在普通战收益之上追加，主奖另行手选。
struct DungeonRewardRules {
    int CurrencyScale = 1, MaterialGrade = 1;
    int MainStones = 0, BattleStones = 0, EliteBonusStones = 0, BossBonusStones = 0, CacheStones = 0;
};

// 用途：集中五境界灵石与材料档位；流程：新冻结主奖、沿途收益及相关费用预览。
// 使用：传合法入口stage 0..12与真实秘境type；返回：纯规则表，非法参数抛invalid_argument。
DungeonRewardRules mfDungeonRewardRulesForStage(int EntryStage, std::string_view Type = "wild");
// 用途：保留更新前已经冻结的主灵石奖；流程：入口/存档校验。
// 使用：传入口stage/type及实际冻结amount；返回：仅旧线性值或当前新表值为真，不重写奖励。
bool mfIsFrozenDungeonStoneReward(int EntryStage, std::string_view Type, int Amount);
// 用途：统一按入口而非人物境界结算；流程：沿途奖励和怪物/事件规则对接。
// 使用：传只读游戏与run；返回：现有site的合法stage，不存在或无效时-1，不推进RNG。
int mfDungeonEntryRewardStage(const Game& State, const DungeonRun& Run);
// 用途：列出适阶且符合真实取得用途的草药奖励池；流程：战斗/宝箱/风险材料共用。
// 使用：传合法入口stage；返回：最高可用且不高于适阶的同阶草药，缺阶时向下降档，纯查询。
std::vector<const data::HerbDef*> mfDungeonHerbRewardPool(int EntryStage);
// 用途：列出入口适阶普通炼器材料；流程：沿途材料奖励共用。
// 使用：传合法入口stage；返回：同阶普通材料，缺阶向下降档，不含瓶专属灵材或六阶材料。
std::vector<const data::MaterialDef*> mfDungeonMaterialRewardPool(int EntryStage);
} // namespace nz
