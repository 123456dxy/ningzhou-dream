#include "core/content/beast_portraits.h"
#include <algorithm>
#include <array>

namespace nz::Appearance {
namespace {
constexpr std::array<BeastPortraitDefinition, 6> Themes{{
    {BeastPortraitTheme::StoneGuard, "stone-guard", content::DaoId::Earth},
    {BeastPortraitTheme::FlameBeast, "flame-beast", content::DaoId::Fire},
    {BeastPortraitTheme::VenomBeast, "venom-beast", content::DaoId::Wood},
    {BeastPortraitTheme::MindBeast, "mind-beast", content::DaoId::Mind},
    {BeastPortraitTheme::RendingBeast, "rending-beast", content::DaoId::Metal},
    {BeastPortraitTheme::ChargingBeast, "charging-beast", content::DaoId::Body}
}};
constexpr int MaximumStage = 12;
constexpr int StagesPerMajorRealm = 3;
constexpr std::string_view PortraitRoot = "/assets/portraits/beast-";
} // namespace

// 用途：读取静态六主题目录；流程：视图和资源校验；用法：只读遍历；返回：无游戏依赖的固定表。
std::span<const BeastPortraitDefinition> mfBeastPortraitThemes() { return Themes; }

// 用途：从稳定主题ID或已登记大道取外观；流程：真实战况选图；用法：不传中文名称或任意资源路径。
// 返回值：对应主题，未知为空；剑/水只作机制相同的显式共享，不更改战斗规则。
std::optional<BeastPortraitTheme> mfBeastPortraitTheme(std::string_view StyleOrId) {
    for (const auto& Definition : Themes) if (Definition.Id == StyleOrId) return Definition.Theme;
    const auto Dao = content::parseDaoId(StyleOrId);
    if (!Dao) return {};
    if (*Dao == content::DaoId::Water) return BeastPortraitTheme::StoneGuard;
    if (*Dao == content::DaoId::Sword) return BeastPortraitTheme::RendingBeast;
    for (const auto& Definition : Themes) if (Definition.Dao == *Dao) return Definition.Theme;
    return {};
}

// 用途：将主题枚举转换为资源目录ID；流程：白名单路径构造；用法：传枚举；返回：未知枚举为空。
std::string_view mfBeastPortraitThemeId(BeastPortraitTheme Theme) {
    for (const auto& Definition : Themes) if (Definition.Theme == Theme) return Definition.Id;
    return {};
}

// 用途：将实际敌方境界与已识别主题映射到固定文件；流程：正式斗法/试炼共享外观事实。
// 使用方式：只读传风格和stage；返回值：白名单路径，未知风格保留经典五境图，绝不使用显示名或外部路径。
std::string mfBeastPortraitPath(std::string_view StyleOrId, int Stage) {
    const int Major = std::clamp(Stage, 0, MaximumStage) / StagesPerMajorRealm;
    const auto Theme = mfBeastPortraitTheme(StyleOrId);
    const std::string Prefix(PortraitRoot);
    return Theme ? Prefix + std::string(mfBeastPortraitThemeId(*Theme)) + "-" + std::to_string(Major) + "-v1.webp" :
        Prefix + std::to_string(Major) + ".webp";
}
} // namespace nz::Appearance
