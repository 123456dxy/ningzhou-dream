#include "core/crafting/green_bottle.h"
#include "core/crafting/garden_rules.h"
#include "core/crafting/cave_garden.h"
#include "core/content/green_bottle_content.h"
#include "core/dungeon.h"
#include "core/foundation/state_limits.h"
#include "core/sim.h"
#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace nz::crafting {
// 用途：校验永久瓶阶；流程：保存/报价；返回：错误或空，未持有天赋不得拥有额外品阶。
std::string mfGreenBottleStateError(const Player& Owner) {
    if (Owner.arts.GreenBottleGrade < 0 || Owner.arts.GreenBottleGrade > 6) return "小绿瓶品阶必须为0至6。";
    if (Owner.arts.GreenBottleGrade > 0 && !hasTalent(Owner, "greenBottle")) return "未持有小绿瓶却包含升级品阶。";
    return {};
}
// 用途：统一保证专属通关材料可以完整入库；流程：入口/兑现/持久化校验；返回：错误或空，先做减法边界判断避免加法溢出。
std::string mfGreenBottleMaterialCapacityError(const Player& Owner, std::string_view MaterialId, int RewardCount) {
    if (!content::mfFindGreenBottleMaterial(MaterialId) || RewardCount < 1 || RewardCount > StateLimits::MaxPersistedInteger)
        return "小绿瓶专属灵材奖励无效。";
    const auto Found = Owner.arts.materials.find(std::string(MaterialId));
    const int Owned = Found == Owner.arts.materials.end() ? 0 : Found->second;
    if (Owned < 0 || Owned > StateLimits::MaxPersistedInteger - RewardCount)
        return "专属灵材库存空间不足，无法完整带回通关材料。";
    return {};
}
// 用途：读取天赋持有者的永久附加日增；流程：报价/真实生长；返回：固定数值，非法阶位不给加成。
int mfGreenBottleDailyBonus(const Player& Owner) {
    if (!hasTalent(Owner, "greenBottle") || !mfGreenBottleStateError(Owner).empty()) return 0;
    return GreenBottleDailyUnits[Owner.arts.GreenBottleGrade];
}
// 用途：比较六品药材零进度的成熟周期；流程：升阶预览；返回：固定6项天数，与实际生长取同一配置。
std::array<int, 6> mfGreenBottleMaturityDays(int CaveTier, int BottleGrade) {
    if (CaveTier < 0 || CaveTier > 2 || BottleGrade < 0 || BottleGrade > 6) throw std::invalid_argument("洞府档位或绿瓶阶位无效。 ");
    const int Daily = GardenNaturalDailyUnits[CaveTier] + GreenBottleDailyUnits[BottleGrade];
    std::array<int, 6> Days{};
    for (std::size_t Index = 0; Index < Days.size(); ++Index) Days[Index] = (GardenMaturityUnits[Index] + Daily - 1) / Daily;
    return Days;
}
// 用途：统一逐阶升级报价；流程：界面/真实命令；返回：完整价格与专属材料缺量，不读旧储备当可消费余额。
GreenBottleUpgradeQuote mfQuoteGreenBottleUpgrade(const Game& State) {
    GreenBottleUpgradeQuote Quote; Quote.OwnedStones = State.player.stones;
    const int Current = State.player.arts.GreenBottleGrade;
    Quote.Reason = mfGreenBottleStateError(State.player);
    if (Quote.Reason.empty() && !hasTalent(State.player, "greenBottle")) Quote.Reason = "未持有小绿瓶。";
    if (Quote.Reason.empty() && Current >= 6) Quote.Reason = "小绿瓶已经恢复至六阶。";
    if (Quote.Reason.empty()) Quote.Reason = mfGardenAccessError(State);
    if (Quote.Reason.empty() && (State.eventSeq < 0 || State.eventSeq >= StateLimits::MaxPersistedInteger))
        Quote.Reason = "纪事编号已达支持范围，无法提交升级。";
    if (Current >= 0 && Current < 6) {
        Quote.TargetGrade = Current + 1; Quote.StoneCost = GreenBottleUpgradeStones[Current];
        if (const auto* Definition = content::mfGreenBottleMaterialForGrade(Quote.TargetGrade)) {
            GreenBottleMaterialQuote Material;
            Material.Id = Definition->Material.id; Material.Name = Definition->Material.name;
            Material.Required = GreenBottleUpgradeMaterialCounts[Quote.TargetGrade - 4];
            const auto Found = State.player.arts.materials.find(Material.Id);
            Material.Owned = Found == State.player.arts.materials.end() ? 0 : Found->second;
            Material.Missing = std::max(0, Material.Required - Material.Owned);
            Quote.Materials.push_back(std::move(Material));
        }
        if (Quote.Reason.empty() && State.player.stones < Quote.StoneCost) Quote.Reason = "恢复灵性所需灵石不足。";
        if (Quote.Reason.empty() && !Quote.Materials.empty() && Quote.Materials.front().Missing > 0) Quote.Reason = "缺少小绿瓶专属秘境灵材。";
    }
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：按最新报价永久升一级；流程：洞府真实动作；返回：错误或空，版本不符/资源不足不扣费。
std::string mfUpgradeGreenBottle(Game& State, int ExpectedGrade) {
    if (ExpectedGrade != State.player.arts.GreenBottleGrade) return "小绿瓶阶位已变化，请重新查看报价。";
    const auto Quote = mfQuoteGreenBottleUpgrade(State); if (!Quote.Enabled) return Quote.Reason;
    Game Candidate = State;
    Candidate.player.stones -= Quote.StoneCost;
    for (const auto& Material : Quote.Materials) Candidate.player.arts.materials[Material.Id] -= Material.Required;
    Candidate.player.arts.GreenBottleGrade = Quote.TargetGrade;
    logEvent(Candidate, "self", "投入大量灵石，小绿瓶恢复至" + std::to_string(Quote.TargetGrade) + "阶灵性；灵田永久附加日增" + std::to_string(GreenBottleDailyUnits[Quote.TargetGrade]) + "点。", true);
    // 升瓶只写材料、灵石、瓶阶和纪事；不移动含map可抛构造的整份Game/optional。
    static_assert(noexcept(State.player.arts.materials.swap(Candidate.player.arts.materials)));
    static_assert(noexcept(State.logs.swap(Candidate.logs)));
    State.player.arts.materials.swap(Candidate.player.arts.materials);
    State.logs.swap(Candidate.logs);
    State.player.stones = Candidate.player.stones;
    State.player.arts.GreenBottleGrade = Candidate.player.arts.GreenBottleGrade;
    State.eventSeq = Candidate.eventSeq;
    return {};
}
// 用途：按当前瓶阶与真实洞府管理资格报价秘境；流程：专属入口/执行；返回：不收费、不生成入口的快照。
GreenBottleDungeonQuote mfQuoteGreenBottleDungeon(const Game& State, int Grade) {
    GreenBottleDungeonQuote Quote; Quote.Grade = Grade;
    const auto* Definition = content::mfGreenBottleMaterialForGrade(Grade);
    if (!Definition) { Quote.Reason = "未知的小绿瓶灵材秘境。"; return Quote; }
    Quote.Name = Definition->DungeonName; Quote.MaterialId = Definition->Material.id;
    Quote.MaterialName = Definition->Material.name; Quote.MinimumBottleGrade = Definition->MinimumBottleGrade;
    Quote.Reason = mfGreenBottleStateError(State.player);
    if (Quote.Reason.empty() && !hasTalent(State.player, "greenBottle")) Quote.Reason = "未持有小绿瓶。";
    if (Quote.Reason.empty() && State.player.arts.GreenBottleGrade < Quote.MinimumBottleGrade) Quote.Reason = "小绿瓶至少需要" + std::to_string(Quote.MinimumBottleGrade) + "阶才能定位此秘境。";
    if (Quote.Reason.empty()) Quote.Reason = mfGardenAccessError(State);
    if (Quote.Reason.empty() && (State.eventSeq < 0 || State.eventSeq >= StateLimits::MaxPersistedInteger))
        Quote.Reason = "纪事编号已达支持范围，无法进入专属秘境。";
    if (Quote.Reason.empty()) Quote.Reason = mfGreenBottleMaterialCapacityError(State.player, Quote.MaterialId, Quote.RewardCount);
    Quote.Enabled = Quote.Reason.empty(); return Quote;
}
// 用途：列出真正解锁的专属秘境；流程：洞府纯查询；返回：无天赋为空，六阶保留六阶入口。
std::vector<GreenBottleDungeonQuote> mfGreenBottleDungeonOptions(const Game& State) {
    std::vector<GreenBottleDungeonQuote> Options;
    if (!hasTalent(State.player, "greenBottle")) return Options;
    for (const auto& Definition : content::mfGreenBottleMaterials())
        if (State.player.arts.GreenBottleGrade >= Definition.MinimumBottleGrade)
            Options.push_back(mfQuoteGreenBottleDungeon(State, Definition.Material.grade));
    return Options;
}
// 用途：依同一报价进入新一轮真实秘境；流程：洞府入口命令；返回：错误或空，忙碌和过期阶位不建site。
std::string mfEnterGreenBottleDungeon(Game& State, int Grade, int ExpectedGrade) {
    if (ExpectedGrade != State.player.arts.GreenBottleGrade) return "小绿瓶阶位已变化，请刷新入口。";
    const auto Quote = mfQuoteGreenBottleDungeon(State, Grade); if (!Quote.Enabled) return Quote.Reason;
    return mfOpenBottleMaterialDungeon(State, Grade);
}
}
