#include "core/content/text_table.h"
#include "core/combat/hand_rules.h"

#include <map>
#include <set>
#include <stdexcept>

namespace nz::content {
// 用途：获取内容 ID 的中文展示名。
// 参与流程：大道或宗门的界面文案。
// 使用方式：传 DaoId 或 SectId；非法枚举抛 invalid_argument。
// 返回值：对应的中文字符串视图，不用于保存协议。
std::string_view displayName(DaoId id) {
    switch (id) {
    case DaoId::Metal: return "金"; case DaoId::Wood: return "木";
    case DaoId::Water: return "水"; case DaoId::Fire: return "火";
    case DaoId::Earth: return "土"; case DaoId::Mind: return "神";
    case DaoId::Body: return "体"; case DaoId::Sword: return "剑";
    case DaoId::Qi: return "气"; case DaoId::Array: return "阵";
    case DaoId::Alchemy: return "丹"; case DaoId::Artifact: return "器";
    }
    throw std::invalid_argument("Invalid dao display ID");
}
// 用途：获取内容 ID 的中文展示名。
// 参与流程：大道或宗门的界面文案。
// 使用方式：传 DaoId 或 SectId；非法枚举抛 invalid_argument。
// 返回值：对应的中文字符串视图，不用于保存协议。
std::string_view displayName(SectId id) {
    switch (id) {
    case SectId::Jinhong: return "金虹剑派"; case SectId::Xinghe: return "星河剑派";
    case SectId::Zhushan: return "竹山宗"; case SectId::Lihuo: return "离火门";
    case SectId::Huachen: return "化尘教";
    case SectId::Xuandao: return "玄道宗";
    }
    throw std::invalid_argument("Invalid sect display ID");
}
// 用途：读取集中维护的规则文案模板。
// 参与流程：神通说明、战报和法宝描述。
// 使用方式：传有效 TextId；非法 ID 抛 invalid_argument。
// 返回值：不可变文案或包含具名占位符的模板视图。
std::string_view text(TextId id) {
    switch (id) {
    case TextId::StrikeDescription: return "固定普通招式，消耗1灵力造成直接伤害；不占神通槽位。";
    case TextId::DefendDescription: return "固定普通招式，消耗1灵力获得本回合12%气血护盾；不占神通槽位。";
    case TextId::DrawDescription: {
        static const std::string Description = "立即抽{count}张牌，手牌最多" +
            std::to_string(BattleCards::MaximumHandSize) + "张。";
        return Description;
    }
    case TextId::ForeseeDescription: {
        static const std::string Description = "下一回合多抽{count}张牌，手牌最多" +
            std::to_string(BattleCards::MaximumHandSize) + "张。";
        return Description;
    }
    case TextId::RedrawDescription: return "弃置其余手牌，重抽等量牌，再抽{count}张；弃牌可触发金系效果。";
    case TextId::ExhaustDescription: return "选择手牌、抽牌堆或弃牌堆中一张其他神通卡，仅将该卡移出本场，再抽{count}张。同神通其他副本保留，可选普通招式，下场恢复。";
    case TextId::EmpowerDescription: return "本场伤害加成+{percent}%，累计最多30%，不消耗气血。";
    case TextId::WardDescription: return "本场额外减伤+{percent}%，累计最多20%；总减伤最多35%。";
    case TextId::ManaDescription: return "立即恢复{count}灵力，不造成伤害。";
    case TextId::StoreDescription: return "封存剩余灵力至下回合，基础上限{count}；本回合灵力清零，气系功法增加封存上限。";
    case TextId::FlowDescription: return "本场每回合回复灵力+1，每次施放损失当前气血上限8%。";
    case TextId::BurstDescription: return "立即获得{count}灵力，本场此后每回合回复灵力−1。";
    case TextId::FloatDescription: return "立即恢复2灵力，随后两回合自然回复−1。";
    case TextId::HealDescription: return "回复气血，单次最高14%气血上限；丹系高阶效果可清除自身毒伤。";
    case TextId::ShieldDescription: return "获得本回合{percent}%气血护盾，护盾累计最多30%。";
    case TextId::GuardExtra: return "本回合额外减伤10%。";
    case TextId::ArrayExtra: return "同时回复1灵力。";
    case TextId::SwordDescription: return "剑系攻击并积攒剑气；每层剑气增伤7%，最多8层。连续剑牌可触发剑系功法。";
    case TextId::HumanDescription: return "剑系爆发，消耗全部剑气与当前8%气血，气血最低保留1。";
    case TextId::HeavyDescription: return "消耗全部重势，每层增伤12%；土系功法可追加护盾伤害。";
    case TextId::ChargeDescription: return "攻击并积攒1层重势，最多4层；重势强化重击。";
    case TextId::WaterDescription: return "攻击并积攒2层水势，最多6层，可立即衔接浪击。";
    case TextId::WaveDescription: return "消耗水势，每层至少增伤15%；水系功法可保留水势、返还灵力。";
    case TextId::PoisonDescription: return "攻击并施加{count}层毒，天然叠加，最多{cap}层；再次施毒刷新3回合，木系功法可触发毒疗。";
    case TextId::PoisonBurstDescription: return "消耗全部毒层，每层追加统一伤害基底（战力×0.3+18）的18%伤害；爆发受护盾和减伤影响。";
    case TextId::FireDescription: return "攻击并施加灼烧，天然叠加并刷新3回合；火系功法增加每次施加量，相生提高灼烧伤害。";
    case TextId::FireBurstDescription: return "攻击并引爆现有灼烧，追加剩余全部回合的灼烧伤害，然后清除燃烧。";
    case TextId::FireSiphonDescription: return "消耗敌方全部灼烧，将原本引爆的灼烧伤害等量转为自身气血，不造成伤害；受引爆成长影响，不受治疗加成影响，回复不超过缺失气血。";
    case TextId::SoulDescription: return "攻击并积攒2层震荡，最多6层。";
    case TextId::SoulBurstDescription: return "消耗全部震荡，每层增伤{bonus_percent}%；每层使该次伤害的{pierce_percent}%穿盾，最多{max_pierce_percent}%。穿盾部分仍受目标减伤。";
    case TextId::BodyBurstDescription: return "体魄强化爆发，消耗重势，每层增伤10%；施放前灵力至少3再增伤15%。";
    case TextId::DrainDescription: return "神识攻击，穿透全部护盾。";
    case TextId::StealDescription: return "攻击并回复1灵力，夺灵功法最多再增加1。";
    case TextId::AttackDescription: return "造成直接伤害，可衔接五行相生连击。";
    case TextId::CooldownDescription: return "冷却{count}回合。";
    case TextId::BattleRules: return "每回合抽5张神通卡。出牌不触发反击；结束回合才结算持续伤害、敌方行动与护盾。最多24回合。";
    case TextId::UnknownDao: return "未知大道。";
    case TextId::ArtifactActivate: return "{name}·催动";
    case TextId::ArtifactYinQiSword: return "地阴混元剑以较低直伤夺灵，命中后恢复灵力。";
    case TextId::ArtifactYinMetalSword: return "地阴金剑以较低直伤积攒蓄势，可配合重击爆发。";
    case TextId::ArtifactYinSword: return "地阴剑侧重叠加属性状态，直伤较低。";
    case TextId::ArtifactYangDirectSword: return "天阳剑释放直接属性伤害，可配合相生与增伤。";
    case TextId::ArtifactYangSword: return "天阳剑造成属性伤害，并触发对应大道的状态。";
    case TextId::ArtifactYinQiSeal: return "地阴混元印以较低直伤夺灵，命中后恢复灵力。";
    case TextId::ArtifactYinMetalSeal: return "地阴金印以较低直伤集中积攒蓄势。";
    case TextId::ArtifactYinSeal: return "地阴印以较低直伤集中施加属性状态。";
    case TextId::ArtifactYangSeal: return "天阳印释放属性爆发；火、木、水、神系会消耗累积状态，土系消耗蓄势。";
    case TextId::ArtifactYinArmor: return "获得本回合护盾与守势减伤。";
    case TextId::ArtifactYangArmor: return "获得本回合护盾，可配合蓄势、反震与盾击。";
    case TextId::ArtifactBell: return "吸纳灵气，恢复{count}灵力（不超过战斗上限）。";
    case TextId::ArtifactHeal: return "恢复气血，可触发治疗与清毒类功法。";
    case TextId::ArtifactWard: return "提高本场战斗减伤，法宝与神通的额外减伤共用20%上限。";
    case TextId::ArtifactEmpower: return "提高本场伤害，法宝与神通的伤害加成共用30%上限。";
    case TextId::ArtifactSuffix: return " 品阶与铭刻强化威力；冷却{count}个回合。装备后常驻斗法侧栏，无需抽牌、不占额外神通槽。";
    case TextId::AchievementBrewCondition: return "本世累计成功炼丹 {Target} 炉。";
    case TextId::AchievementForgeCondition: return "本世累计成功炼器 {Target} 次。";
    case TextId::AchievementWinsCondition: return "本世累计斗法获胜 {Target} 场。";
    case TextId::AchievementNodesCondition: return "本世解锁 {Target} 个悟道节点。";
    case TextId::AchievementManualsCondition: return "本世习得 {Target} 部不同功法。";
    case TextId::AchievementQuestsCondition: return "本世累计完成 {Target} 次机缘。";
    case TextId::AchievementWealthCondition: return "本世灵石持有量的历史最高记录达到 {Target} 枚。";
    case TextId::AchievementReward: return "首次达成可获 {Points} 传承点，用于下一世天赋预算；人生结算或实际重开时领取。";
    case TextId::AchievementClaimedReward: return "首次奖励 {Points} 传承点；已结算，跨世不再重复领取。";
    case TextId::AchievementRewardNote: return "达成成就不会立即增加本世悟道点。传承奖励在本世结算或实际重开时领取；同一成就跨世只领取一次，累计传承上限 {PointLimit} 点，实际新增以结算回执为准。";
    case TextId::GardenReserveRequiresBottle: return "持有小绿瓶后才可存入灵田加速储备。";
    case TextId::GardenBottleGrowthRule: return "每格同药材最多99份；每份成熟收获3份。天然每天100成长点，洞府每档+10；格内储备全额负担时灵气每档+30、绿瓶额外+200。日费=尚需加速种苗数×品阶×(灵气档位+绿瓶1)，储备不足自然生长且不扣费。外出照常生长，不自动扣主背包灵石。";
    case TextId::GardenNoBottleGrowthRule: return "每格同药材最多99份；每份成熟收获3份。天然每天100成长点，洞府每档+10；外出照常自然生长。灵气配给沿用原修炼规则，不额外加速灵田。";
    case TextId::PillCultivationCompleteEffect: return "标称修为 +{NominalValue}；实际吸收受本小境单次上限及距圆满余量限制。";
    case TextId::PillCultivationUseLimit: return "；修为增量不超过标称药效（{NominalValue}）、本小境单次上限（{StageLimit}）及距圆满余量（{RemainingValue}）";
    case TextId::DungeonElapsedPaused: return "秘境探索已结束，余下 {RemainingDays} 天探索耗时尚未结算。已回到岁月纪事，请先处理当前抉择并点击继续推进，再迎战兽潮或进行其他安排。";
    }
    throw std::invalid_argument("Invalid text ID");
}

// 用途：严格代入模板中的具名参数。
// 参与流程：集中内容文案格式化底层。
// 使用方式：传模板和参数列表；缺失、重复、未使用或畸形参数会抛错。
// 返回值：完成替换的字符串，插入值不会再次解析。
std::string formatTextTemplate(std::string_view pattern, std::initializer_list<TextArg> arguments) {
    std::map<std::string_view, std::string_view> values;
    for (const auto& argument : arguments) {
        if (argument.name.empty() || !values.emplace(argument.name, argument.value).second)
            throw std::invalid_argument("Empty or duplicate text argument");
    }
    std::set<std::string_view> used;
    std::string result;
    for (std::size_t position = 0; position < pattern.size();) {
        if (pattern[position] == '}') throw std::invalid_argument("Unmatched text placeholder");
        if (pattern[position] != '{') { result += pattern[position++]; continue; }
        const auto end = pattern.find('}', position + 1);
        if (end == std::string_view::npos) throw std::invalid_argument("Unclosed text placeholder");
        const auto name = pattern.substr(position + 1, end - position - 1);
        if (name.empty() || name.find('{') != std::string_view::npos)
            throw std::invalid_argument("Malformed text placeholder");
        const auto found = values.find(name);
        if (found == values.end()) throw std::invalid_argument("Missing text argument: " + std::string(name));
        result += found->second;
        used.insert(name);
        position = end + 1;
    }
    if (used.size() != values.size()) throw std::invalid_argument("Unused text argument");
    return result;
}
// 用途：按 TextId 读取模板并严格代入参数。
// 参与流程：卡牌与法宝说明生成。
// 使用方式：传文案 ID 和可选具名参数。
// 返回值：格式化文案；参数错误按模板规则抛异常。
std::string formatText(TextId id, std::initializer_list<TextArg> arguments) {
    return formatTextTemplate(text(id), arguments);
}
} // namespace nz::content
