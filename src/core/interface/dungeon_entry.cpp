#include "core/interface/dungeon_entry.h"

#include "core/content/game_ids.h"
#include "core/dungeon.h"

namespace nz::api {
// 用途：将外部模式/道系翻译为强类型选择；流程：普通与绿瓶实际入场；用法：仅在合法请求对象上读取可选字段；返回：中文错误或空，拒绝未知键/错类型/非法道系，不消耗RNG或资源。
std::string mfReadDungeonCardRewardPreference(const json::Value& Input, DungeonCardRewardPreference& Preference) {
    if (!Input.isObject()) return "秘境入场需要JSON对象。";
    DungeonCardRewardPreference Parsed;
    if (!Input.asObject().contains("cardRewardPreference")) {
        Preference = std::move(Parsed);
        return "";
    }
    const auto& PreferenceValue = Input["cardRewardPreference"];
    if (!PreferenceValue.isObject()) return "神通奖励偏好必须为JSON对象。";
    for (const auto& [Key, Field] : PreferenceValue.asObject()) {
        static_cast<void>(Field);
        if (Key != "mode" && Key != "daos") return "神通奖励偏好包含未知字段。";
    }
    if (!PreferenceValue["mode"].isString() || !PreferenceValue["daos"].isArray() || PreferenceValue["daos"].asArray().size() > content::allDaoIds().size())
        return "请提供有效的奖励模式与道系列表。";
    const auto Mode = mfParseDungeonCardRewardMode(PreferenceValue["mode"].asString());
    if (!Mode) return "未知神通奖励模式。";
    Parsed.Mode = *Mode;
    for (const auto& Entry : PreferenceValue["daos"].asArray()) {
        if (!Entry.isString()) return "神通奖励道系必须为稳定ID。";
        const auto Dao = content::parseDaoId(Entry.asString());
        if (!Dao || content::stableId(*Dao) != Entry.asString()) return "未知神通奖励道系。";
        Parsed.Daos.push_back(*Dao);
    }
    if (const auto Error = mfValidateDungeonCardRewardEntryPreference(Parsed); !Error.empty()) return Error;
    Preference = std::move(Parsed);
    return "";
}

// 用途：拒绝非法ID或偏好后进入秘境；流程：HTTP入场动作；用法：传活动Game与请求；返回：领域结果，失败不创建入口或覆盖在途奖励。
std::string mfExecuteDungeonEnter(Game& State, const json::Value& Input) {
    if (!Input.isObject() || !Input["id"].isString() || Input["id"].asString().empty()) return "请提供有效的秘境入口ID。";
    for (const auto& [Key, Field] : Input.asObject()) {
        static_cast<void>(Field);
        if (Key != "id" && Key != "cardRewardPreference") return "秘境入场请求包含未知参数。";
    }
    DungeonCardRewardPreference Preference;
    if (const auto Error = mfReadDungeonCardRewardPreference(Input, Preference); !Error.empty()) return Error;
    return mfEnterDungeonWithCardRewards(State, Input["id"].asString(), Preference);
}
} // namespace nz::api
