#include "core/character_build.h"
#include <algorithm>
#include <stdexcept>

namespace nz {
// 用途：提供所有已打包人物立绘的创建白名单；流程：建角选项、请求校验及读档外观；使用：按性别选择稳定ID；返回：只读目录，不将外观与宗门资格绑定。
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
        {"npc-jinhong-female-v1", "金虹·叶簪剑印", "女", "/assets/portraits/npc-jinhong-female-v1.webp"},
        {"npc-jinhong-male-v1", "金虹·月冠执剑", "男", "/assets/portraits/npc-jinhong-male-v1.webp"},
        {"npc-xinghe-female-v1", "星河·月簪星盘", "女", "/assets/portraits/npc-xinghe-female-v1.webp"},
        {"npc-xinghe-male-v1", "星河·银饰执扇", "男", "/assets/portraits/npc-xinghe-male-v1.webp"},
        {"npc-zhushan-female-v1", "竹山·叶簪竹笛", "女", "/assets/portraits/npc-zhushan-female-v1.webp"},
        {"npc-zhushan-male-v1", "竹山·竹杖药囊", "男", "/assets/portraits/npc-zhushan-male-v1.webp"},
        {"npc-lihuo-female-v1", "离火·凤簪铜扇", "女", "/assets/portraits/npc-lihuo-female-v1.webp"},
        {"npc-lihuo-male-v1", "离火·赤铜火杖", "男", "/assets/portraits/npc-lihuo-male-v1.webp"},
        {"npc-huachen-female-v1", "化尘·竹卷铜盘", "女", "/assets/portraits/npc-huachen-female-v1.webp"},
        {"npc-huachen-male-v1", "化尘·石簪拄剑", "男", "/assets/portraits/npc-huachen-male-v1.webp"},
        {"npc-rogue-female-v1", "散修·行旅轻装", "女", "/assets/portraits/npc-rogue-female-v1.webp"},
        {"npc-rogue-male-v1", "散修·云游侠客", "男", "/assets/portraits/npc-rogue-male-v1.webp"},
        {"xuandao-female-v1", "玄道·五行剑意", "女", "/assets/portraits/sect-xuandao-female-v1.webp"},
        {"xuandao-male-v1", "玄道·五行剑意", "男", "/assets/portraits/sect-xuandao-male-v1.webp"},
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
