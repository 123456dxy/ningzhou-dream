#include "core/interface/forge_api.h"
#include "core/interface/views.h"
#include "core/crafting/forge_rules.h"
#include "core/content/forge_content.h"
#include "core/content/artifact_appearances.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/artifact_status.h"
#include "core/artifact.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

namespace nz::api {
namespace {
using json::Value;
string mfParseForgeRequest(const Value& Input, crafting::ForgeRequest& Request) {
    if (!Input.isObject() || !Input["version"].isNumber() || Input["version"].asNumber() != 2)
        return "炼器版本无效。";
    if (!Input["materials"].isArray() || Input["materials"].asArray().size() > 10)
        return "投料必须是最多十格的材料列表。";
    for (const auto& Item : Input["materials"].asArray()) {
        if (!Item.isString() || Item.asString().size() > 128) return "投料标识无效。";
        Request.MaterialIds.push_back(Item.asString());
    }
    for (const auto* Key : {"main", "shape", "effect", "inscription"})
        if (!Input[Key].isString() || Input[Key].asString().size() > 128) return "炼器选择字段无效。";
    if (Input.asObject().contains("quoteToken") && !Input["quoteToken"].isString()) return "炼器报价标识无效。";
    Request.MainMaterialId = Input["main"].asString(); Request.ShapeId = Input["shape"].asString();
    Request.EffectId = Input["effect"].asString(); Request.InscriptionId = Input["inscription"].asString();
    return "";
}
Value mfForgeError(const string& Error) {
    Value Result(json::Object{}); Result.set("ok", false); Result.set("error", Error); return Result;
}
string mfForgeQuoteToken(const Game& State, const crafting::ForgeRequest& Request, const crafting::ForgeQuote& Quote) {
    Value Identity(json::Object{}); json::Array Materials;
    for (const auto& Id : Request.MaterialIds) Materials.emplace_back(Id);
    Identity.set("materials", std::move(Materials)); Identity.set("main", Request.MainMaterialId);
    Identity.set("shape", Request.ShapeId); Identity.set("effect", Request.EffectId); Identity.set("inscription", Request.InscriptionId);
    Identity.set("life", State.lifeId); Identity.set("days", State.clock.days); Identity.set("stage", State.player.stage);
    Identity.set("stones", State.player.stones); Identity.set("cost", Quote.CostSpiritStoneCount);
    Identity.set("duration", Quote.DurationDays); Identity.set("chance", Quote.SuccessProbability);
    Identity.set("energy", Quote.Energy); Identity.set("power", Quote.Product.EffectPower);
    json::Array Owned;
    for (const auto& Material : Quote.Materials) { Value Entry(json::Object{}); Entry.set("id", Material.id); Entry.set("owned", Material.owned); Owned.push_back(std::move(Entry)); }
    Identity.set("owned", std::move(Owned));
    std::uint64_t Hash = 14695981039346656037ULL;
    for (const unsigned char Byte : Identity.dump()) { Hash ^= Byte; Hash *= 1099511628211ULL; }
    return std::to_string(Hash);
}
Value mfForgeEffectView(const content::ForgeEffectDefinition& Effect) {
    Value Entry(json::Object{}); Entry.set("id", Effect.Id); Entry.set("name", Effect.Name);
    Entry.set("slot", Effect.Slot); Entry.set("category", Effect.Slot == "weapon" ? "attack" : Effect.Slot == "armor" ? "defense" : "support");
    Entry.set("description", Effect.Description); Entry.set("passive", Effect.Passive);
    json::Array Elements; for (const auto& Element : Effect.Elements) Elements.emplace_back(Element);
    Entry.set("elements", std::move(Elements)); return Entry;
}
}

json::Value mfForgeOptionsView(const Game& State) {
    Value Result(json::Object{}); Result.set("ok", true); Result.set("version", 2);
    json::Array Materials, Shapes, Effects, Inscriptions;
    for (const auto& Definition : content::mfForgeMaterials()) {
        const auto& Material = Definition.Material; Value Entry(json::Object{});
        Entry.set("id", Material.id); Entry.set("name", Material.name); Entry.set("grade", Material.grade);
        Entry.set("element", Material.element); Entry.set("polarity", Material.polarity);
        const auto Owned = State.player.arts.materials.find(Material.id);
        Entry.set("owned", Owned == State.player.arts.materials.end() ? 0 : Owned->second);
        Entry.set("family", Definition.FamilyId); Entry.set("familyName", Definition.FamilyName);
        Entry.set("part", Definition.PartId); Entry.set("partName", Definition.PartName);
        Entry.set("source", Definition.SourceNote); Entry.set("acquisitionPending", Definition.AcquisitionPending);
        Entry.set("energy", Definition.Energy); Entry.set("buyable", false); Entry.set("price", Material.price);
        json::Array Dimensions; for (const int Dimension : Definition.Dimensions) Dimensions.emplace_back(Dimension);
        Entry.set("dimensions", std::move(Dimensions)); Materials.push_back(std::move(Entry));
    }
    for (const auto& Shape : content::mfForgeShapes()) { Value Entry(json::Object{}); Entry.set("id", Shape.Id); Entry.set("name", Shape.Name); Entry.set("slot", Shape.Slot); Entry.set("category", Shape.Slot == "weapon" ? "attack" : Shape.Slot == "armor" ? "defense" : "support"); if (const auto* Appearance = content::mfFindArtifactAppearance(Shape.Id)) { Entry.set("appearanceId", string(Appearance->Id)); Entry.set("illustration", string(Appearance->Path)); } Shapes.push_back(std::move(Entry)); }
    for (const auto& Effect : content::mfForgeEffects()) Effects.push_back(mfForgeEffectView(Effect));
    for (const auto& Inscription : content::mfForgeInscriptions()) { Value Entry(json::Object{}); Entry.set("id", Inscription.Id); Entry.set("name", Inscription.Name); Entry.set("description", Inscription.Description); Inscriptions.push_back(std::move(Entry)); }
    Result.set("materials", std::move(Materials)); Result.set("shapes", std::move(Shapes));
    Result.set("effects", std::move(Effects)); Result.set("inscriptions", std::move(Inscriptions)); return Result;
}

json::Value mfForgePreviewView(const Game& State, const Value& Input) {
    crafting::ForgeRequest Request; const string Error = mfParseForgeRequest(Input, Request);
    if (!Error.empty()) return mfForgeError(Error);
    const auto Quote = crafting::mfQuoteForge(State, Request);
    const auto& Product = Quote.Product; Value Info(json::Object{}), Result(json::Object{});
    Info.set("valid", Quote.Valid); Info.set("enabled", Quote.Enabled); Info.set("reason", Quote.Reason);
    Info.set("selectedCount", Quote.MaterialCount); Info.set("grade", Quote.Grade); Info.set("energy", Quote.Energy);
    Info.set("requiredEnergy", Quote.RequiredEnergy);
    Info.set("dimensionScore", Quote.DimensionScore); Info.set("utilization", Quote.Utilization); Info.set("util", Quote.Utilization);
    Info.set("cost", Quote.CostSpiritStoneCount); Info.set("days", Quote.DurationDays); Info.set("chance", Quote.SuccessProbability);
    Info.set("mainElement", Product.element); Info.set("mainPolarity", Product.polarity); Info.set("shapeSlot", Product.slot);
    const auto* Inscription = content::mfFindForgeInscription(Request.InscriptionId);
    Info.set("inscriptionText", Inscription && !Request.InscriptionId.empty() ? Inscription->Description : "");
    const auto* Shape = content::mfFindForgeShape(Request.ShapeId);
    Info.set("shapeId", Request.ShapeId); Info.set("shapeName", Shape ? Shape->Name : "");
    if (const auto* Appearance = content::mfFindArtifactAppearance(Request.ShapeId)) {
        Info.set("appearanceId", string(Appearance->Id)); Info.set("illustration", string(Appearance->Path));
    }
    Info.set("effectText", mfArtifactEffectDescription(Product)); Info.set("quoteToken", mfForgeQuoteToken(State, Request, Quote));
    Gear PreviewProduct = Product;
    PreviewProduct.id = "forge-preview";
    Info.set("ability", view::artifactView(State.player, artifactAbility(PreviewProduct)));
    Value Passive(json::Object{}); Passive.set("text", mfArtifactPassiveDescription(Product)); Info.set("passive", std::move(Passive));
    json::Array Dimensions, Materials, Blockers, Effects;
    for (const int Dimension : Quote.Dimensions) Dimensions.emplace_back(Dimension);
    if (!Quote.Reason.empty()) Blockers.emplace_back(Quote.Reason);
    for (const auto& Material : Quote.Materials) {
        Value Entry(json::Object{}); Entry.set("id", Material.id); Entry.set("name", Material.name);
        Entry.set("required", Material.required); Entry.set("owned", Material.owned); Entry.set("missing", Material.missing);
        Materials.push_back(std::move(Entry));
        if (Material.missing > 0) Blockers.emplace_back(Material.name + "缺" + std::to_string(Material.missing) + "份。");
    }
    for (const auto& Effect : content::mfForgeEffects())
        if (content::mfForgeEffectAllowed(Effect.Id, Product.slot, Product.element)) Effects.push_back(mfForgeEffectView(Effect));
    Info.set("dimensions", std::move(Dimensions)); Info.set("materials", std::move(Materials));
    Info.set("blockers", std::move(Blockers)); Info.set("availableEffects", std::move(Effects));
    Result.set("ok", true); Result.set("info", std::move(Info)); return Result;
}

string mfBeginForgeRequest(Game& State, const Value& Input, int& DurationDays) {
    crafting::ForgeRequest Request; const string Error = mfParseForgeRequest(Input, Request);
    if (!Error.empty()) return Error;
    const auto Quote = crafting::mfQuoteForge(State, Request);
    if (!Input["quoteToken"].isString() || Input["quoteToken"].asString() != mfForgeQuoteToken(State, Request, Quote))
        return "炼器报价已变化，请重新预览后开炉。";
    const string BeginError = crafting::mfBeginForge(State, Request, Quote.CostSpiritStoneCount);
    if (BeginError.empty()) DurationDays = Quote.DurationDays;
    return BeginError;
}

string mfValidateArtifactTargets(const Battle& State, const Value& Input, vector<string>& Targets) {
    if (!Input["targets"].isArray() || Input["targets"].asArray().size() > 5) return "换牌目标必须是最多五张的手牌列表。";
    if (!Input["expectedTurn"].isNumber() || Input["expectedTurn"].asNumber() != State.turn ||
        !Input["expectedHand"].isArray() || Input["expectedHand"].asArray().size() != State.hand.size()) return "手牌已变化，请重新选择。";
    for (std::size_t Index = 0; Index < State.hand.size(); ++Index)
        if (!Input["expectedHand"][Index].isString() || Input["expectedHand"][Index].asString() != State.hand[Index]) return "手牌已变化，请重新选择。";
    std::set<string> Seen;
    for (const auto& Target : Input["targets"].asArray()) {
        if (!Target.isString() || !Seen.insert(Target.asString()).second ||
            std::find(State.hand.begin(), State.hand.end(), Target.asString()) == State.hand.end()) return "换牌目标含重复或失效的手牌。";
        Targets.push_back(Target.asString());
    }
    return "";
}

string mfValidateArtifactStatusSelection(const Player& Actor, const Battle& State, const Value& Input) {
    if (!Input.isObject() || !Input["id"].isString()) return "";
    const auto Ability = equippedArtifactAbility(Actor, Input["id"].asString());
    if (!Ability || !mfArtifactSelectsStatus(Ability->gear)) return "";
    if (Input.asObject().contains("targets")) return "此法器只能选择一种状态，不能选择手牌。";
    if (!Input["target"].isString() || Input["target"].asString().empty() || Input["target"].asString().size() > 128)
        return "请选择一种有效状态。";
    if (!Input["expectedTurn"].isNumber() || !std::isfinite(Input["expectedTurn"].asNumber()) ||
        std::floor(Input["expectedTurn"].asNumber()) != Input["expectedTurn"].asNumber() ||
        Input["expectedTurn"].asNumber() != State.turn)
        return "回合已变化，请重新选择状态。";
    if (!Input["expectedStatusToken"].isString() || Input["expectedStatusToken"].asString().empty() ||
        Input["expectedStatusToken"].asString().size() > 128)
        return "状态选择已过期，请重新选择。";
    const auto Quote = mfQuoteArtifactAction(Actor, State, Ability->gear, {}, true, Input["target"].asString());
    if (Input["expectedStatusToken"].asString() != Quote.StatusToken)
        return "状态已变化，请重新选择。";
    return Quote.Allowed ? "" : Quote.Error;
}
} // namespace nz::api
