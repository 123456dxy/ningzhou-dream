#include "core/character_build.h"
#include <algorithm>
#include <stdexcept>

namespace nz {
const vector<PlayerPortraitDef>& playerPortraits() {
    static const vector<PlayerPortraitDef> portraits = {
        {"player-female", "初入仙途", "女", "/assets/portraits/player-female.webp"},
        {"player-male", "初入仙途", "男", "/assets/portraits/player-male.webp"},
        {"rogue-female", "山野散修", "女", "/assets/portraits/rogue-female.webp"},
        {"rogue-male", "山野散修", "男", "/assets/portraits/rogue.webp"},
        {"jinhong-female", "负剑行者", "女", "/assets/portraits/sect-jinhong-female.webp"},
        {"jinhong-male", "负剑行者", "男", "/assets/portraits/sect-jinhong.webp"},
        {"zhushan-female", "青竹药香", "女", "/assets/portraits/sect-zhushan.webp"},
        {"zhushan-male", "青竹药香", "男", "/assets/portraits/sect-zhushan-male.webp"},
        {"lihuo-female", "离火丹心", "女", "/assets/portraits/sect-lihuo-female.webp"},
        {"lihuo-male", "离火丹心", "男", "/assets/portraits/sect-lihuo.webp"},
        {"xinghe-female", "星河问道", "女", "/assets/portraits/sect-xinghe.webp"},
        {"xinghe-male", "星河问道", "男", "/assets/portraits/sect-xinghe-male.webp"},
        {"huachen-female", "花尘清影", "女", "/assets/portraits/sect-huachen-female.webp"},
        {"huachen-male", "花尘清影", "男", "/assets/portraits/sect-huachen.webp"},
    };
    return portraits;
}
const PlayerPortraitDef* findPlayerPortrait(const string& id) {
    for (const auto& portrait : playerPortraits()) if (id == portrait.id) return &portrait;
    return nullptr;
}
string defaultPlayerPortraitId(const string& gender) {
    return gender == "男" ? "player-male" : "player-female";
}
string playerPortrait(const Player& player) {
    const auto* portrait = findPlayerPortrait(player.portraitId);
    if (!portrait || player.gender != portrait->gender)
        portrait = findPlayerPortrait(defaultPlayerPortraitId(player.gender));
    return portrait->src;
}
CreateOptions resolveCreateOptions(const CreateOptions& source) {
    CreateOptions options = source;
    if (options.gender != "男" && options.gender != "女")
        throw std::invalid_argument("请选择有效的角色性别。");
    if (options.portraitId.empty()) options.portraitId = defaultPlayerPortraitId(options.gender);
    const auto* portrait = findPlayerPortrait(options.portraitId);
    if (!portrait || options.gender != portrait->gender)
        throw std::invalid_argument("请选择与角色性别一致的立绘。");
    if (options.traits.empty() && !options.traitsExplicit) {
        const auto* preset = findTalent(options.talent);
        if (!preset) throw std::invalid_argument("没有这个天赋。");
        options.traits.push_back(preset->id);
    }
    if (options.allocation.empty() && !options.allocationExplicit) {
        int talentCost = 0;
        for (const auto& id : options.traits) {
            const auto* talent = findTalent(id);
            if (!talent) throw std::invalid_argument("没有这个天赋。");
            talentCost += talent->cost;
        }
        options.allocation["apt"] = std::min(10, std::max(0, options.budget - talentCost));
    }
    // The same validator also fills the five missing allocation keys with zero.
    const Build build = buildCharacter(options.traits, options.allocation, options.budget, options.seed);
    options.allocation = build.allocation;
    options.traitsExplicit = true;
    options.allocationExplicit = true;
    return options;
}
} // namespace nz
