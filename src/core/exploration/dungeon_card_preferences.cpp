#include "core/exploration/dungeon_card_preferences.h"
#include "core/battle.h"
#include "core/model.h"
#include <algorithm>

namespace nz {
namespace {
// 用途：缓存真实神通目录可用的大道白名单；流程：筛选与校验；用法：内部只读访问；返回：不可变目录，不依赖活动角色或世界状态。
const std::vector<content::DaoId>& mfAvailableRewardDaos() {
    static const std::vector<content::DaoId> Daos = [] {
        std::vector<content::DaoId> Result;
        for (const auto Dao : content::allDaoIds()) {
            if (std::any_of(allSpells().begin(), allSpells().end(), [&](const SpellDef& Spell) {
                    return Spell.id != "strike" && Spell.id != "defend" && content::parseDaoId(Spell.dao) == Dao;
                })) Result.push_back(Dao);
        }
        return Result;
    }();
    return Daos;
}
} // namespace

// 用途：解析严格模式协议键；流程：入场请求与读档边界；用法：传原字符串；返回：已登记模式或空，不猜测未知值。
std::optional<DungeonCardRewardMode> mfParseDungeonCardRewardMode(std::string_view Mode) {
    if (Mode == "all") return DungeonCardRewardMode::All;
    if (Mode == "none") return DungeonCardRewardMode::None;
    if (Mode == "selected") return DungeonCardRewardMode::Selected;
    return {};
}

// 用途：输出模式的固定机器值；流程：视图与持久化；用法：传枚举；返回：稳定键，未知枚举为空。
std::string_view mfDungeonCardRewardModeKey(DungeonCardRewardMode Mode) {
    switch (Mode) {
    case DungeonCardRewardMode::All: return "all";
    case DungeonCardRewardMode::None: return "none";
    case DungeonCardRewardMode::Selected: return "selected";
    }
    return {};
}

// 用途：共享真实可选大道目录；流程：外层单系选择与领域校验；用法：只读查询；返回：目录副本，不触碰活动RNG或时间。
std::vector<content::DaoId> mfDungeonCardRewardDaos() { return mfAvailableRewardDaos(); }

// 用途：拒绝未知、重复和矛盾偏好；流程：入场与存档校验；用法：传值对象；返回：原因或空，不改变输入。
std::string mfValidateDungeonCardRewardPreference(const DungeonCardRewardPreference& Preference) {
    if (mfDungeonCardRewardModeKey(Preference.Mode).empty()) return "秘境临时卡奖励模式无效。";
    if (Preference.Mode != DungeonCardRewardMode::Selected)
        return Preference.Daos.empty() ? "" : "全部或不接收临时卡时不能附加大道选择。";
    if (Preference.Daos.empty()) return "请选择至少一种临时卡奖励大道。";
    const auto& Available = mfAvailableRewardDaos();
    if (Preference.Daos.size() > Available.size()) return "临时卡奖励大道数量无效。";
    for (auto Current = Preference.Daos.begin(); Current != Preference.Daos.end(); ++Current) {
        if (std::find(Available.begin(), Available.end(), *Current) == Available.end()) return "该大道没有可获得的临时神通。";
        if (std::find(Preference.Daos.begin(), Current, *Current) != Current) return "临时卡奖励大道不能重复选择。";
    }
    return "";
}

// 用途：统一新入场的单系限制；流程：普通秘境与绿瓶专属秘境的请求和领域入口；用法：传待冻结偏好；返回：错误或空，保留旧多系快照的读取规则。
std::string mfValidateDungeonCardRewardEntryPreference(const DungeonCardRewardPreference& Preference) {
    if (const auto Error = mfValidateDungeonCardRewardPreference(Preference); !Error.empty()) return Error;
    if (Preference.Mode == DungeonCardRewardMode::Selected && Preference.Daos.size() != 1)
        return "秘境临时神通奖励只能选择一个道系。";
    return "";
}

// 用途：只过滤沿途新增临时牌；流程：奖励池与冻结选项核对；用法：传Run和神通；返回：是否符合偏好，旧版本保持原池且不审查已有牌组。
bool mfDungeonCardRewardAllowed(const DungeonRun& Run, const SpellDef& Spell) {
    if (Spell.id == "strike" || Spell.id == "defend") return false;
    if (Run.CardRewardRulesVersion == 0)
        return Run.CardRewardPreference.Mode == DungeonCardRewardMode::All && Run.CardRewardPreference.Daos.empty();
    if (Run.CardRewardRulesVersion != 1 || !mfValidateDungeonCardRewardPreference(Run.CardRewardPreference).empty()) return false;
    if (Run.CardRewardPreference.Mode == DungeonCardRewardMode::None) return false;
    const auto Dao = content::parseDaoId(Spell.dao);
    if (!Dao || std::find(mfAvailableRewardDaos().begin(), mfAvailableRewardDaos().end(), *Dao) == mfAvailableRewardDaos().end()) return false;
    return Run.CardRewardPreference.Mode == DungeonCardRewardMode::All ||
        std::find(Run.CardRewardPreference.Daos.begin(), Run.CardRewardPreference.Daos.end(), *Dao) != Run.CardRewardPreference.Daos.end();
}
} // namespace nz
