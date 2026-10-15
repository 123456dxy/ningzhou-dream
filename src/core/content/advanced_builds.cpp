#include "core/content/advanced_builds.h"
#include "core/content/npc_combat_profiles.h"

namespace nz::content {
// 用途：以真实初始建议和六宗模板派生进阶目录；流程：试验与学习指引共用。
// 用法：读取结果，实际装备仍校验境界和槽位；返回：静态不可变目录，没有隐含伤害倍率。
const vector<BuildInfo>& mfAdvancedBuilds() {
    static const vector<BuildInfo> Catalog = [] {
        vector<BuildInfo> Builds;
        for (const auto& Initial : artBuilds()) {
            BuildInfo Advanced = Initial;
            Advanced.name += "·完整传承";
            Advanced.desc = "进阶配装：仅增加真实功法、神通选择，不附带隐藏倍率；须按境界与槽位筛选。" + Initial.desc;
            for (const auto& Profile : npcCombatProfiles()) {
                if (Profile.BuildId != Initial.id) continue;
                Advanced.name = Profile.name;
                Advanced.manuals = Profile.manuals;
                Advanced.spells = Profile.spells;
                break;
            }
            if (Initial.id == "sword-chain") {
                Advanced.name = "剑气连锋·进阶";
                Advanced.manuals = {"swordcraft", "hidden", "human", "sword", "zhou"};
                Advanced.spells = {"swordFocus", "sword", "humanSword", "rain", "swordDraw", "swordRelease", "skim", "scheme"};
            } else if (Initial.id == "mind-cycle") {
                Advanced.name = "震荡摄魂·进阶";
                Advanced.manuals = {"soulfire", "visual", "mind", "cloud", "zhou"};
                Advanced.spells = {"soulspark", "soulshock", "mind", "skim", "refresh", "purge", "hoard", "insightdraw"};
            }
            Builds.push_back(std::move(Advanced));
        }
        return Builds;
    }();
    return Catalog;
}
// 用途：查进阶目录且明确拒绝未知 ID；流程：真实测试开局与显示。
// 用法：传稳定原流派 ID；返回：静态目录借用指针，不保存传入视图。
const BuildInfo* mfFindAdvancedBuild(std::string_view BuildId) {
    for (const auto& Build : mfAdvancedBuilds()) if (Build.id == BuildId) return &Build;
    return nullptr;
}
} // namespace nz::content
