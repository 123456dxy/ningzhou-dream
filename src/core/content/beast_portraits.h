#pragma once

#include "core/content/game_ids.h"
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace nz::Appearance {

// 所属内容外观模块；六种稳定主题不依赖敌人中文名字或资源路径输入。
enum class BeastPortraitTheme { StoneGuard, FlameBeast, VenomBeast, MindBeast, RendingBeast, ChargingBeast };

// 无外部资源、可复制的只读目录值；静态ID用于文件白名单，Dao用于真实战斗风格映射。
struct BeastPortraitDefinition {
    BeastPortraitTheme Theme;
    std::string_view Id;
    content::DaoId Dao;
};

// 用途：公开六类妖兽立绘白名单；流程：视图/资源检查；用法：只读遍历；返回：静态目录，不访问游戏状态。
std::span<const BeastPortraitDefinition> mfBeastPortraitThemes();
// 用途：解析稳定主题ID或大道风格；流程：真实战况外观；用法：传已知ID、稳定大道或兼容大道键。
// 返回值：合法主题或空；水共享凝甲主题、剑共享迅爪主题，未知值不会拼接为资源路径。
std::optional<BeastPortraitTheme> mfBeastPortraitTheme(std::string_view StyleOrId);
// 用途：查询枚举的固定主题ID；流程：路径构造与校验；用法：传主题；返回：白名单ID，非法枚举为空。
std::string_view mfBeastPortraitThemeId(BeastPortraitTheme Theme);
// 用途：共用主题与实际境界选图；流程：正式斗法和独立试炼视图；用法：传风格/主题和敌方实际stage。
// 返回值：固定目录内路径；未识别主题保留旧beast-0..4兼容图，不改RNG、战斗、存档或人物注册。
std::string mfBeastPortraitPath(std::string_view StyleOrId, int Stage);

} // namespace nz::Appearance
