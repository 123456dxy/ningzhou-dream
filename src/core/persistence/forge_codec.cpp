#include "core/persistence/forge_codec.h"
#include "core/persistence/json_helpers.h"
#include "core/artifact.h"
#include "core/content/forge_content.h"
#include <cmath>
#include <set>

namespace nz::persistence {
namespace {
constexpr int MaximumRoundEffectPoints = 1000000;

bool mfIntegerInRange(const json::Value& Input, int Minimum, int Maximum) {
    return Input.isNumber() && std::isfinite(Input.asNumber()) && Input.asNumber() == std::floor(Input.asNumber()) &&
        Input.asNumber() >= Minimum && Input.asNumber() <= Maximum;
}
bool mfOptionalType(const json::Value& Input, const char* Key, json::Type Type) {
    return !Input.asObject().contains(Key) || Input[Key].type() == Type;
}
}

json::Value mfGearToJson(const Gear& Item) {
    json::Value Output(json::Object{});
    Output.set("id", Item.id); Output.set("name", Item.name); Output.set("slot", Item.slot);
    Output.set("element", Item.element); Output.set("polarity", Item.polarity); Output.set("grade", Item.grade);
    Output.set("bonus", Item.bonus); Output.set("inscribed", Item.inscribed); Output.set("price", Item.price);
    Output.set("craftCost", Item.craftCost); Output.set("crafted", Item.crafted);
    Output.set("origin", Item.Origin);
    Output.set("shape", Item.crafted && Item.shape.empty() ? artifactShape(Item) : Item.shape);
    json::Array Dimensions; for (const int Dimension : Item.dimensions) Dimensions.emplace_back(Dimension);
    Output.set("dimensions", std::move(Dimensions));
    if (Item.ForgeVersion != 0) {
        Output.set("forgeVersion", Item.ForgeVersion); Output.set("effectId", Item.EffectId);
        Output.set("inscriptionId", Item.InscriptionId); Output.set("effectPower", Item.EffectPower);
        Output.set("forgeEnergy", Item.ForgeEnergy); Output.set("sourceMaterials", mapToJson(Item.SourceMaterials));
    }
    return Output;
}

Gear mfGearFromJson(const json::Value& Input) {
    Gear Item;
    Item.id = Input["id"].asString(); Item.name = Input["name"].asString(); Item.slot = Input["slot"].asString();
    Item.element = Input["element"].asString(); Item.polarity = Input["polarity"].asString("天阳");
    Item.grade = Input["grade"].asInt(1); Item.bonus = Input["bonus"].asNumber();
    Item.inscribed = Input["inscribed"].asBool(); Item.price = Input["price"].asInt(); Item.craftCost = Input["craftCost"].asInt();
    Item.crafted = Input["crafted"].asBool(Item.id.rfind("forge-", 0) == 0 || (Item.id.rfind("gear-", 0) == 0 && Item.craftCost > 0));
    Item.Origin = Input["origin"].asString();
    Item.shape = Input["shape"].asString();
    if (Item.crafted && Item.shape.empty()) Item.shape = artifactShape(Item);
    for (const auto& Dimension : Input["dimensions"].asArray()) Item.dimensions.push_back(Dimension.asInt());
    Item.ForgeVersion = Input["forgeVersion"].asInt(); Item.EffectId = Input["effectId"].asString();
    Item.InscriptionId = Input["inscriptionId"].asString(); Item.EffectPower = Input["effectPower"].asNumber();
    Item.ForgeEnergy = Input["forgeEnergy"].asInt(); Item.SourceMaterials = jsonToMap<int>(Input["sourceMaterials"]);
    return Item;
}

bool mfForgeGearShapeValid(const json::Value& Input) {
    if (!Input.isObject()) return false;
    if (!mfOptionalType(Input, "origin", json::Type::String)) return false;
    for (const auto* Key : {"effectId", "inscriptionId"}) if (!mfOptionalType(Input, Key, json::Type::String)) return false;
    if (!mfOptionalType(Input, "crafted", json::Type::Bool) || !mfOptionalType(Input, "shape", json::Type::String) ||
        !mfOptionalType(Input, "sourceMaterials", json::Type::Object)) return false;
    if (Input.asObject().contains("forgeVersion") && !mfIntegerInRange(Input["forgeVersion"], 0, 2)) return false;
    const int Version = Input["forgeVersion"].asInt();
    if (Version == 1) return false;
    if (Version == 0) return !Input.asObject().contains("effectId") && !Input.asObject().contains("sourceMaterials") &&
        !Input.asObject().contains("effectPower") && !Input.asObject().contains("forgeEnergy") && !Input.asObject().contains("inscriptionId");
    if (!Input["effectId"].isString() || !Input["inscriptionId"].isString() || !Input["sourceMaterials"].isObject() ||
        !Input["effectPower"].isNumber() || !std::isfinite(Input["effectPower"].asNumber()) ||
        Input["effectPower"].asNumber() < 0.1 || Input["effectPower"].asNumber() > 3 ||
        !mfIntegerInRange(Input["forgeEnergy"], 1, 480) || !Input["dimensions"].isArray() || Input["dimensions"].asArray().size() != 5) return false;
    for (const auto& Dimension : Input["dimensions"].asArray()) if (!mfIntegerInRange(Dimension, 0, 500)) return false;
    for (const auto& [Id, Count] : Input["sourceMaterials"].asObject())
        if (!content::mfFindForgeMaterial(Id) || !mfIntegerInRange(Count, 1, 10)) return false;
    return mfForgeGearStateError(mfGearFromJson(Input)).empty();
}

string mfForgeGearStateError(const Gear& Item) {
    if (!Item.Origin.empty() && Item.Origin != "player" && Item.Origin != "event" && Item.Origin != "talent")
        return "法器来源无效";
    if (Item.ForgeVersion == 0) return Item.EffectId.empty() && Item.InscriptionId.empty() && Item.SourceMaterials.empty() &&
        Item.EffectPower == 0 && Item.ForgeEnergy == 0 ? "" : "旧法器混入新版字段";
    const auto* Shape = content::mfFindForgeShape(Item.shape);
    if (Item.ForgeVersion != 2 || !Item.crafted || Item.grade < 1 || Item.grade > 5 || !Shape || Shape->Slot != Item.slot ||
        !content::mfForgeEffectAllowed(Item.EffectId, Item.slot, Item.element) ||
        (!Item.InscriptionId.empty() && !content::mfFindForgeInscription(Item.InscriptionId)) ||
        !std::isfinite(Item.EffectPower) || Item.EffectPower < .1 || Item.EffectPower > 3 ||
        Item.ForgeEnergy < 1 || Item.ForgeEnergy > 480 || Item.bonus != 0 || Item.dimensions.size() != 5 ||
        (Item.polarity != "天阳" && Item.polarity != "地阴")) return "新版法器效果、器型或预算无效";
    int MaterialCount = 0;
    for (const auto& [Id, Count] : Item.SourceMaterials) {
        if (!content::mfFindForgeMaterial(Id) || Count < 1 || Count > 10) return "新版法器投料无效";
        MaterialCount += Count;
    }
    for (const int Dimension : Item.dimensions) if (Dimension < 0 || Dimension > 500) return "新版法器五维无效";
    return MaterialCount == 10 ? "" : "新版法器必须保存十份原料";
}

json::Value mfArtifactSnapshotToJson(const ArtifactCombatSnapshot& Snapshot) {
    json::Value Output(json::Object{});
    Output.set("version", Snapshot.Version); Output.set("waterCap", Snapshot.WaterCap); Output.set("swordQiCap", Snapshot.SwordQiCap);
    Output.set("soulCap", Snapshot.SoulCap); Output.set("chargeCap", Snapshot.ChargeCap); Output.set("sharpnessCap", Snapshot.SharpnessCap);
    Output.set("roundMana", Snapshot.RoundMana); Output.set("roundWater", Snapshot.RoundWater); Output.set("roundSwordQi", Snapshot.RoundSwordQi);
    Output.set("roundSoul", Snapshot.RoundSoul); Output.set("roundPoison", Snapshot.RoundPoison);
    Output.set("roundShield", Snapshot.RoundShield); Output.set("roundBurn", Snapshot.RoundBurn);
    Output.set("roundPoisonDamagePerLayer", Snapshot.RoundPoisonDamagePerLayer); return Output;
}

ArtifactCombatSnapshot mfArtifactSnapshotFromJson(const json::Value& Input) {
    ArtifactCombatSnapshot Snapshot;
    if (!Input.isObject()) return Snapshot;
    Snapshot.Version = Input["version"].asInt(); Snapshot.WaterCap = Input["waterCap"].asInt(6);
    Snapshot.SwordQiCap = Input["swordQiCap"].asInt(8); Snapshot.SoulCap = Input["soulCap"].asInt(6);
    Snapshot.ChargeCap = Input["chargeCap"].asInt(4); Snapshot.SharpnessCap = Input["sharpnessCap"].asNumber(.30);
    Snapshot.RoundMana = Input["roundMana"].asInt(); Snapshot.RoundWater = Input["roundWater"].asInt();
    Snapshot.RoundSwordQi = Input["roundSwordQi"].asInt(); Snapshot.RoundSoul = Input["roundSoul"].asInt();
    Snapshot.RoundPoison = Input["roundPoison"].asInt(); Snapshot.RoundShield = Input["roundShield"].asInt();
    Snapshot.RoundBurn = Input["roundBurn"].asInt();
    Snapshot.RoundPoisonDamagePerLayer = Input["roundPoisonDamagePerLayer"].asInt(); return Snapshot;
}

bool mfArtifactSnapshotStateValid(const ArtifactCombatSnapshot& Snapshot) {
    if (Snapshot.Version != 0 && Snapshot.Version != 2) return false;
    const auto InRange = [](int Value, int Low, int High) { return Value >= Low && Value <= High; };
    if (!InRange(Snapshot.WaterCap, 6, 16) || !InRange(Snapshot.SwordQiCap, 8, 18) || !InRange(Snapshot.SoulCap, 6, 16) ||
        !InRange(Snapshot.ChargeCap, 4, 14) || !std::isfinite(Snapshot.SharpnessCap) || Snapshot.SharpnessCap < .3 || Snapshot.SharpnessCap > .6 ||
        !InRange(Snapshot.RoundMana, 0, 10) || !InRange(Snapshot.RoundWater, 0, 10) || !InRange(Snapshot.RoundSwordQi, 0, 10) ||
        !InRange(Snapshot.RoundSoul, 0, 10) || !InRange(Snapshot.RoundPoison, 0, 30) ||
        !InRange(Snapshot.RoundShield, 0, MaximumRoundEffectPoints) ||
        !InRange(Snapshot.RoundBurn, 0, MaximumRoundEffectPoints) ||
        !InRange(Snapshot.RoundPoisonDamagePerLayer, 0, MaximumRoundEffectPoints)) return false;
    if (Snapshot.Version == 0) return mfArtifactSnapshotToJson(Snapshot).dump() == mfArtifactSnapshotToJson(ArtifactCombatSnapshot{}).dump();
    return true;
}

bool mfArtifactSnapshotShapeValid(const json::Value& Input) {
    if (Input.isNull()) return true;
    if (!Input.isObject() || Input.asObject().size() != 14) return false;
    for (const auto* Key : {"version", "waterCap", "swordQiCap", "soulCap", "chargeCap", "roundMana", "roundWater", "roundSwordQi", "roundSoul", "roundPoison"})
        if (!mfIntegerInRange(Input[Key], 0, 100)) return false;
    for (const auto* Key : {"roundShield", "roundBurn", "roundPoisonDamagePerLayer"})
        if (!mfIntegerInRange(Input[Key], 0, MaximumRoundEffectPoints)) return false;
    if (!Input["sharpnessCap"].isNumber()) return false;
    return mfArtifactSnapshotStateValid(mfArtifactSnapshotFromJson(Input));
}
} // namespace nz::persistence
