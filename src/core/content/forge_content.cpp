#include "core/content/forge_content.h"
#include "core/content/green_bottle_content.h"
#include <algorithm>

namespace nz::content {
namespace {
struct ForgeFamilyDefinition {
    const char* Id;
    const char* Name;
    std::array<int, 5> Dimensions;
};
constexpr std::array<ForgeFamilyDefinition, 5> Families{{
    {"hide", "皮鳞", {100, 75, 50, 25, 0}},
    {"metal", "金属", {75, 50, 25, 0, 100}},
    {"stone", "石", {50, 25, 0, 100, 75}},
    {"spirit", "灵物", {25, 0, 100, 75, 50}},
    {"bone", "牙骨", {0, 100, 75, 50, 25}}
}};
constexpr std::array MaterialEnergy{1, 3, 9, 18, 30, 48};

// 用途：明确登记旧材料的新材类；流程：新目录兼容；不影响旧三料公式。
std::size_t mfLegacyFamilyIndex(std::string_view MaterialId) {
    if (MaterialId == "dragon") return 0;
    if (MaterialId == "jade" || MaterialId == "crystal" || MaterialId == "black" ||
        MaterialId == "bottle-spirit-5" || MaterialId == "bottle-spirit-6") return 2;
    if (MaterialId == "wood" || MaterialId == "soul" || MaterialId == "bottle-spirit-4") return 3;
    return 1;
}

// 用途：输出旧物品描述兼容字段；流程：静态目录构造；不参与规则结算。
string mfDimensionText(const std::array<int, 5>& Dimensions) {
    string Text;
    for (const int Dimension : Dimensions) {
        if (!Text.empty()) Text += ',';
        Text += std::to_string(Dimension);
    }
    return Text;
}

// 用途：包装已有材料并保留ID、阶位与参考价；流程：新目录兼容。
ForgeMaterialDefinition mfLegacyMaterial(const data::MaterialDef& Material, string SourceNote) {
    const auto& Family = Families[mfLegacyFamilyIndex(Material.id)];
    ForgeMaterialDefinition Definition;
    Definition.Material = Material;
    Definition.FamilyId = Family.Id;
    Definition.FamilyName = Family.Name;
    Definition.PartId = "legacy";
    Definition.PartName = "既有灵材";
    Definition.Dimensions = Family.Dimensions;
    Definition.Energy = MaterialEnergy[static_cast<std::size_t>(std::clamp(Material.grade, 1, 6) - 1)];
    Definition.SourceNote = std::move(SourceNote);
    Definition.Source = Material.id.starts_with("bottle-spirit-") ? ForgeMaterialSource::GreenBottle
        : Material.grade <= 2 ? ForgeMaterialSource::OrdinaryMarket : ForgeMaterialSource::Pending;
    return Definition;
}
} // namespace

std::span<const ForgeMaterialDefinition> mfForgeMaterials() {
    static const vector<ForgeMaterialDefinition> Materials = [] {
        vector<ForgeMaterialDefinition> Definitions;
        Definitions.reserve(575);
        for (const auto& Material : data::MaterialDef_TABLE())
            Definitions.push_back(mfLegacyMaterial(Material, "沿用现有材料来源；本轮未新增获取渠道。"));
        for (const auto& Definition : mfGreenBottleMaterials())
            Definitions.push_back(mfLegacyMaterial(Definition.Material, "绿瓶专属秘境；沿用现有获取规则。"));
        constexpr std::array ElementIds{"metal", "wood", "water", "fire", "earth", "sword", "spirit", "mixed"};
        constexpr std::array ElementNames{"金", "木", "水", "火", "土", "剑", "神", "气"};
        constexpr std::array GradeNames{"一阶", "二阶", "三阶", "四阶", "五阶"};
        constexpr std::array PolarityIds{"yang", "yin"};
        constexpr std::array PolarityNames{"天阳", "地阴"};
        constexpr std::array PartIds{"skin", "bone", "claw", "eye", "heart", "metal", "stone"};
        constexpr std::array PartNames{"兽皮鳞", "妖骨", "牙爪", "妖目", "妖心", "灵金", "灵石矿"};
        constexpr std::array<std::size_t, 7> PartFamilies{0, 4, 4, 3, 3, 1, 2};
        for (int Grade = 1; Grade <= 5; ++Grade) {
            for (std::size_t ElementIndex = 0; ElementIndex < ElementIds.size(); ++ElementIndex) {
                for (std::size_t PolarityIndex = 0; PolarityIndex < PolarityIds.size(); ++PolarityIndex) {
                    for (std::size_t PartIndex = 0; PartIndex < PartIds.size(); ++PartIndex) {
                        const auto& Family = Families[PartFamilies[PartIndex]];
                        ForgeMaterialDefinition Definition;
                        Definition.Material.id = "fm-" + std::to_string(Grade) + "-" + ElementIds[ElementIndex] + "-" +
                            PolarityIds[PolarityIndex] + "-" + PartIds[PartIndex];
                        Definition.Material.name = string(GradeNames[static_cast<std::size_t>(Grade - 1)]) +
                            PolarityNames[PolarityIndex] + ElementNames[ElementIndex] + PartNames[PartIndex];
                        Definition.Material.grade = Grade;
                        Definition.Material.element = ElementNames[ElementIndex];
                        Definition.Material.polarity = PolarityNames[PolarityIndex];
                        Definition.Material.price = 8 * Grade * Grade;
                        Definition.Material.dimensions = mfDimensionText(Family.Dimensions);
                        Definition.FamilyId = Family.Id;
                        Definition.FamilyName = Family.Name;
                        Definition.PartId = PartIds[PartIndex];
                        Definition.PartName = PartNames[PartIndex];
                        Definition.Dimensions = Family.Dimensions;
                        Definition.Energy = MaterialEnergy[static_cast<std::size_t>(Grade - 1)];
                        Definition.AcquisitionPending = false;
                        Definition.Source = PartIndex < 5 ? ForgeMaterialSource::BeastDrop
                            : Grade <= 2 ? ForgeMaterialSource::OrdinaryMarket : ForgeMaterialSource::TianjiSteward;
                        Definition.SourceNote = PartIndex < 5 ? "击杀对应属性、品阶的妖兽，五种部位各获1份；阴阳随妖兽而定，不在商店出售。"
                            : Grade <= 2 ? "普通坊市无限供应；二阶需筑基。" : "五城天机阁执事固定价无限供应。";
                        Definitions.push_back(std::move(Definition));
                    }
                }
            }
        }
        return Definitions;
    }();
    return Materials;
}

const ForgeMaterialDefinition* mfFindForgeMaterial(std::string_view MaterialId) {
    for (const auto& Definition : mfForgeMaterials()) if (Definition.Material.id == MaterialId) return &Definition;
    return nullptr;
}

int mfForgeMaterialEnergy(int Grade) {
    if (Grade < 1 || Grade > static_cast<int>(MaterialEnergy.size())) return 0;
    return MaterialEnergy[static_cast<std::size_t>(Grade - 1)];
}

std::span<const ForgeShapeDefinition> mfForgeShapes() {
    static const std::array<ForgeShapeDefinition, 11> Shapes{{
        {"sword", "剑", "weapon"}, {"needle", "飞针", "weapon"}, {"seal", "印", "weapon"},
        {"ruler", "尺", "weapon"}, {"box", "匣", "weapon"},
        {"armor", "甲胄", "armor"}, {"robe", "法袍", "armor"}, {"bell", "钟", "armor"},
        {"pearl", "珠", "accessory"}, {"ring", "环", "accessory"}, {"token", "令", "accessory"}
    }};
    return Shapes;
}
const ForgeShapeDefinition* mfFindForgeShape(std::string_view ShapeId) {
    for (const auto& Definition : mfForgeShapes()) if (Definition.Id == ShapeId) return &Definition;
    return nullptr;
}

std::span<const ForgeEffectDefinition> mfForgeEffects() {
    static const vector<ForgeEffectDefinition> Effects{
        {"draw", "引念", "weapon", "主动抽取神通；基础张数随法器品阶增加。", {}, false},
        {"redraw", "换念", "weapon", "主动选择手牌，先弃置，再抽回实际弃置的等量张数；手牌不足也可使用。", {}, false},
        {"damage", "破阵", "weapon", "主动造成对应属性伤害。", {}, false},
        {"sharpness_burst", "锐势斩", "weapon", "主动消耗锐势，造成金道重击。", {"金"}, false},
        {"fire_apply", "积焰", "weapon", "主动追加敌方灼烧伤害池。", {"火"}, false},
        {"fire_burst", "灼击", "weapon", "按敌方当前灼烧量造成伤害，保留灼烧与持续回合。", {"火"}, false},
        {"poison_apply", "施毒", "weapon", "主动施加中毒。", {"木"}, false},
        {"poison_burst", "毒袭", "weapon", "按敌方当前毒层造成伤害，保留毒，毒本场持续。", {"木"}, false},
        {"water_gain", "聚浪", "weapon", "主动获得水势。", {"水"}, false},
        {"water_burst", "叠浪击", "weapon", "主动消耗水势，造成水道伤害。", {"水"}, false},
        {"sword_gain", "积气", "weapon", "主动积累剑气。", {"剑"}, false},
        {"sword_burst", "归锋", "weapon", "主动消耗剑气，形成终结剑招。", {"剑"}, false},
        {"soul_gain", "震识", "weapon", "主动施加震荡。", {"神"}, false},
        {"soul_burst", "神识冲击", "weapon", "主动兑现震荡，造成神识伤害。", {"神"}, false},
        {"charge_gain", "积势", "weapon", "主动积累蓄势。", {"土"}, false},
        {"charge_burst", "镇岳", "weapon", "主动消耗蓄势，造成厚土重击。", {"土"}, false},
        {"shield", "护体", "armor", "主动获得护盾。", {}, false},
        {"guard", "守势", "armor", "主动获得短时守势。", {}, false},
        {"cleanse", "清浊", "armor", "主动净化自身可移除的负面状态。", {}, false},
        {"dispel", "破护", "armor", "主动驱散敌方可移除的临时增益。", {}, false},
        {"turn_mana", "回灵", "accessory", "每个自身回合开始时回复灵力。", {"神", "气", "金", "木", "水", "火", "土"}, true},
        {"turn_shield", "绵护", "accessory", "每个自身回合开始时获得少量护盾。", {}, true},
        {"turn_water", "潮生", "accessory", "每个自身回合开始时获得水势。", {"水"}, true},
        {"turn_sword", "养锋", "accessory", "每个自身回合开始时获得剑气。", {"剑"}, true},
        {"turn_soul", "摄识", "accessory", "每个自身回合开始时施加震荡。", {"神"}, true},
        {"turn_poison", "毒生", "accessory", "每个自身回合开始时施加中毒。", {"木"}, true},
        {"turn_fire", "续焰", "accessory", "每个自身回合开始时追加灼烧伤害池。", {"火"}, true},
        {"cap_water", "容浪", "accessory", "提高本场水势层数上限，不直接补满水势。", {"水"}, true},
        {"cap_sword", "藏锋", "accessory", "提高本场剑气层数上限，不直接补满剑气。", {"剑"}, true},
        {"cap_soul", "神识拓境", "accessory", "提高本场震荡层数上限，不直接获得震荡。", {"神"}, true},
        {"cap_charge", "厚载", "accessory", "提高本场蓄势层数上限，不直接补满蓄势。", {"土"}, true},
        {"cap_sharpness", "锐势拓境", "accessory", "提高本场锐势增伤比例上限，不把比例作为层数。", {"金"}, true}
    };
    return Effects;
}
const ForgeEffectDefinition* mfFindForgeEffect(std::string_view EffectId) {
    for (const auto& Definition : mfForgeEffects()) if (Definition.Id == EffectId) return &Definition;
    return nullptr;
}
bool mfForgeEffectAllowed(std::string_view EffectId, std::string_view Slot, std::string_view Element) {
    const auto* Definition = mfFindForgeEffect(EffectId);
    if (!Definition || Definition->Slot != Slot) return false;
    return Definition->Elements.empty() || std::find(Definition->Elements.begin(), Definition->Elements.end(), Element) != Definition->Elements.end();
}
std::span<const ForgeInscriptionDefinition> mfForgeInscriptions() {
    static const std::array<ForgeInscriptionDefinition, 5> Inscriptions{{
        {"", "无灵纹", "只采用主效果，不增加代价。"},
        {"azure", "青龙", "主动使用间隔延长一轮；抽换牌减费，其他主效果增强。"},
        {"white", "白虎", "主动使用额外支付4%真实最大气血，降低灵力费用并增强非抽换效果；不能致死。"},
        {"vermilion", "朱雀", "气血不高于50%时减费并增强非抽换效果。"},
        {"black", "玄武", "主动使用后承受一轮悬灵，换取减费并增强非抽换效果。"}
    }};
    return Inscriptions;
}
const ForgeInscriptionDefinition* mfFindForgeInscription(std::string_view InscriptionId) {
    for (const auto& Definition : mfForgeInscriptions()) if (Definition.Id == InscriptionId) return &Definition;
    return nullptr;
}
} // namespace nz::content
