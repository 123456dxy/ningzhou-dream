#include "core/crafting/forge_rules.h"
#include "core/content/forge_content.h"
#include "core/crafting/craft_timing.h"
#include "core/sim.h"
#include "core/talents.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace nz::crafting {
namespace {
constexpr int ForgePortionCount = 10;
constexpr int ForgeRuleVersion = 2;

// 用途：读取真实库存但不插入键；流程：纯报价。
int mfOwnedMaterial(const Player& PlayerState, const string& MaterialId) {
    const auto Found = PlayerState.arts.materials.find(MaterialId);
    return Found == PlayerState.arts.materials.end() ? 0 : std::max(0, Found->second);
}

// 用途：核验与其他耗时动作相同的状态边界；流程：新版炼器报价与开工。
string mfForgeBusyReason(const Game& GameState) {
    if (GameState.ended || GameState.player.health <= 0) return "此世已结束或伤势过重。";
    if (GameState.pending) return "请先处理当前事件。";
    if (GameState.player.arts.battle) return "请先完成斗法。";
    if (GameState.dungeons.run) return "请先离开当前秘境。";
    if (GameState.job) return "请先完成或中止当前安排。";
    return {};
}

} // namespace

ForgeQuote mfQuoteForge(const Game& GameState, const ForgeRequest& Request) {
    ForgeQuote Quote;
    static_cast<ForgeRecipeResult&>(Quote) = mfEvaluateForgeRecipe(Request);
    for (const auto& [MaterialId, Required] : Quote.Product.SourceMaterials) {
        const auto* Material = content::mfFindForgeMaterial(MaterialId);
        ForgeMaterialCost Cost;
        Cost.id = MaterialId;
        Cost.name = Material->Material.name;
        Cost.required = Required;
        Cost.owned = mfOwnedMaterial(GameState.player, MaterialId);
        Cost.missing = std::max(0, Required - Cost.owned);
        Quote.Materials.push_back(std::move(Cost));
    }
    if (!Quote.Valid || Quote.Grade == 0) return Quote;
    Quote.CostSpiritStoneCount = 15 + 18 * Quote.Grade + (Request.InscriptionId.empty() ? 0 : 10 * Quote.Grade);
    Quote.DurationDays = mfCraftDurationDays(GameState.player, CraftKind::Forging, Quote.Grade);
    const auto ArtifactExperience = GameState.player.arts.exp.find("器");
    const int ArtifactLevel = artLevel(ArtifactExperience == GameState.player.arts.exp.end() ? 0 : ArtifactExperience->second);
    Quote.SuccessProbability = std::clamp(0.65 + ArtifactLevel * 0.05 + talentMods(GameState.player).forge +
        (artHas(GameState.player.arts, "器", 1) ? 0.1 : 0.0) - Quote.Grade * 0.05, 0.1, 0.95);
    Quote.Product.craftCost = Quote.CostSpiritStoneCount;
    if (Quote.Reason.empty()) Quote.Reason = mfForgeBusyReason(GameState);
    if (Quote.Reason.empty() && (Quote.Grade > 1 + GameState.player.stage / 3 ||
        (Quote.ContainsSixthGradeMaterial && GameState.player.stage < 12)))
        Quote.Reason = "境界不足以炼制此品阶法器。";
    if (Quote.Reason.empty() && Quote.Grade > 1 + ArtifactLevel)
        Quote.Reason = "器道不足以炼制此品阶法器。";
    if (Quote.Reason.empty() && GameState.player.stones < Quote.CostSpiritStoneCount)
        Quote.Reason = "铸炼灵石不足。";
    if (Quote.Reason.empty()) {
        for (const auto& Material : Quote.Materials) {
            if (Material.missing <= 0) continue;
            Quote.Reason = "炼器材料不足：" + Material.name + "需" + std::to_string(Material.required) +
                "份，持有" + std::to_string(Material.owned) + "份。";
            break;
        }
    }
    Quote.Enabled = Quote.Reason.empty();
    return Quote;
}

string mfBeginForge(Game& GameState, const ForgeRequest& Request, int ExpectedCost) {
    const auto Quote = mfQuoteForge(GameState, Request);
    if (!Quote.Enabled) return Quote.Reason;
    if (ExpectedCost < -1 || (ExpectedCost >= 0 && ExpectedCost != Quote.CostSpiritStoneCount))
        return "炼器报价已变化，请重新确认费用。";

    Game Candidate = GameState;
    Job Contract;
    Contract.actionType = "forge";
    Contract.kind = "forge";
    Contract.destination = "forge-v2";
    Contract.name = "铸炼" + Quote.Product.name;
    Contract.total = Quote.DurationDays;
    Contract.remaining = Quote.DurationDays;
    Contract.started = Candidate.clock.days;
    Contract.cost = Quote.CostSpiritStoneCount;
    Contract.paid = true;
    Contract.chance = Quote.SuccessProbability;
    Contract.ForgeProduct = Quote.Product;
    Candidate.player.stones -= Quote.CostSpiritStoneCount;
    for (const auto& Material : Quote.Materials)
        Candidate.player.arts.materials.at(Material.id) -= Material.required;
    Candidate.job = std::move(Contract);
    logEvent(Candidate, "self", "开始" + Candidate.job->name + "，用时" + std::to_string(Quote.DurationDays) + "天。" );
    GameState = std::move(Candidate);
    return {};
}

bool mfCompleteForge(Game& GameState, const Job& Contract) {
    if (!Contract.ForgeProduct) return false;
    const auto& FrozenProduct = *Contract.ForgeProduct;
    // 存档边界另有完整校验；领域入口仍拒绝明显损坏合同，避免掉回旧三料字符串解析。
    const auto* Shape = content::mfFindForgeShape(FrozenProduct.shape);
    int MaterialCount = 0;
    for (const auto& [MaterialId, Count] : FrozenProduct.SourceMaterials) {
        if (!content::mfFindForgeMaterial(MaterialId) || Count < 1 || Count > ForgePortionCount) return true;
        MaterialCount += Count;
    }
    if (FrozenProduct.ForgeVersion != ForgeRuleVersion || !Shape || FrozenProduct.slot != Shape->Slot ||
        MaterialCount != ForgePortionCount || !content::mfForgeEffectAllowed(FrozenProduct.EffectId, FrozenProduct.slot, FrozenProduct.element)) return true;

    artGain(GameState.player, "器", 4);
    const bool Success = randomOf(GameState) < Contract.chance;
    if (Success) {
        Gear Product = FrozenProduct;
        Product.id = "forge-" + std::to_string(GameState.clock.days) + "-" + std::to_string(GameState.economy.seq++);
        GameState.player.arts.gear.push_back(std::move(Product));
        ++GameState.player.arts.progress.forge;
    }
    logEvent(GameState, "self", Contract.name + (Success ? "成功" : "失败，投入资材耗尽") +
        "，用时" + std::to_string(Contract.total) + "天。" );
    return true;
}
} // namespace nz::crafting
