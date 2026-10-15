#include "harness.h"
#include "core/api.h"
#include "core/battle.h"
#include "core/combat/battle_lab.h"
#include "core/content/beast_portraits.h"
#include "core/interface/battle_lab_api.h"
#include "core/npc_learning.h"
#include "core/save.h"
#include <array>
#include <set>

namespace nztest {
namespace {
using namespace nz;

// 用途：为立绘接线建立真实隔离妖兽战；流程：API与纯度回归；用法：传境界和真实风格；返回：纯内存游戏。
Game mfPortraitEncounter(int Stage, const string& Dao) {
    Game State;
    State.seed = State.rng = 792047;
    State.player.name = "立绘接线回归"; State.player.stage = State.player.highestStage = Stage;
    State.player.lifespan = realmOf(Stage).life;
    State.player.talent.clear(); State.player.traits.clear();
    State.player.arts.spells = {"gold", "water", "gather"}; State.player.arts.knownSpells = State.player.arts.spells;
    EnemySpec Enemy;
    Enemy.name = "可随时修改的中文兽名"; Enemy.kind = "spar"; Enemy.stage = Stage;
    Enemy.style = Dao; Enemy.beast = true; Enemy.speed = 0;
    battleStart(State, Enemy);
    return State;
}
} // namespace

// 用途：验收六主题五境立绘、真实战斗/试炼同源和旧图兼容；流程：资源接线定向回归。
// 使用方式：统一测试调用；返回值：无，记录断言；不访问图片文件、玩家存档、账号或服务端口。
void mfRunBeastPortraitTests() {
    using namespace nz;
    std::printf("\n--- 妖兽主题与境界立绘 ---\n");
    constexpr std::array<std::string_view, 6> Ids{
        "stone-guard", "flame-beast", "venom-beast", "mind-beast", "rending-beast", "charging-beast"
    };
    constexpr std::array<std::string_view, 6> Styles{"土", "火", "木", "神", "金", "体"};
    std::set<string> Paths;
    checkEq(Appearance::mfBeastPortraitThemes().size(), Ids.size(), "六种妖兽主题有完整白名单");
    for (std::size_t Index = 0; Index < Ids.size(); ++Index) {
        const auto Theme = Appearance::mfBeastPortraitTheme(Ids[Index]);
        check(Theme.has_value(), "稳定妖兽主题ID可解析");
        if (!Theme) continue;
        checkStr(string(Appearance::mfBeastPortraitThemeId(*Theme)), string(Ids[Index]), "主题枚举与稳定ID往返一致");
        check(Appearance::mfBeastPortraitTheme(Styles[Index]) == Theme, "主题与实际大道风格一致");
        for (int Major = 0; Major < 5; ++Major) {
            const string Expected = "/assets/portraits/beast-" + string(Ids[Index]) + "-" + std::to_string(Major) + "-v1.webp";
            const int Stage = Major * 3;
            checkStr(Appearance::mfBeastPortraitPath(Styles[Index], Stage), Expected, "五个实际大境界使用对应主题立绘");
            check(Paths.insert(Expected).second, "不同主题与境界不能冒用同一路径");
            checkStr(Appearance::mfBeastPortraitPath(Ids[Index], Stage), Expected, "稳定主题入口与风格入口同源");
            if (Stage < 12) checkStr(Appearance::mfBeastPortraitPath(Styles[Index], Stage + 2), Expected, "同大境界三个小境可合理共图");
            auto State = mfPortraitEncounter(Stage, string(Styles[Index]));
            check(State.player.arts.battle.has_value(), "立绘回归建立真实普通PvE");
            if (!State.player.arts.battle) continue;
            const string Saved = saveToJson(State);
            const auto View = api::battleView(State);
            checkStr(View["enemyPortrait"].asString(), Expected, "正式战斗视图取真实主题和实际敌方境界");
            State.player.arts.battle->enemy = "完全不同的显示名称";
            checkStr(api::battleView(State)["enemyPortrait"].asString(), Expected, "改中文名称不改变妖兽主题外观");
            State.player.arts.battle->enemy = "可随时修改的中文兽名";
            (void)api::battleView(State);
            checkStr(saveToJson(State), Saved, "反复查询立绘与战况不改变RNG/世界/存档");
        }
    }
    checkEq(Paths.size(), 30, "六主题五境界共30个独立资源路径");
    for (const string Style : {"water", "水"})
        checkStr(Appearance::mfBeastPortraitPath(Style, 6), Appearance::mfBeastPortraitPath("stone-guard", 6), "水系凝甲妖兽合理共享岩甲主题");
    for (const string Style : {"sword", "剑"})
        checkStr(Appearance::mfBeastPortraitPath(Style, 6), Appearance::mfBeastPortraitPath("rending-beast", 6), "剑系穿甲妖兽合理共享迅爪主题");
    for (const string Invalid : {"", "unknown", "qi", "气", "array", "../rogue", "/assets/portraits/player-male.webp", "岩甲守卫"}) {
        check(!Appearance::mfBeastPortraitTheme(Invalid).has_value(), "未知主题、展示名与资源路径不进入主题白名单");
        for (int Major = 0; Major < 5; ++Major)
            checkStr(Appearance::mfBeastPortraitPath(Invalid, Major * 3), beastPortrait(Major * 3), "无主题时保留旧境界图接口兼容");
    }
    checkStr(Appearance::mfBeastPortraitPath("火", -50), Appearance::mfBeastPortraitPath("火", 0), "越下界境界安全回退炼气图");
    checkStr(Appearance::mfBeastPortraitPath("火", 100), Appearance::mfBeastPortraitPath("火", 12), "越上界境界安全回退化神图");
    check(Appearance::mfBeastPortraitThemeId(static_cast<Appearance::BeastPortraitTheme>(999)).empty(), "非法主题枚举不能形成任意路径");
    {
        auto State = mfPortraitEncounter(6, "火");
        State.player.arts.battle->style = "土";
        checkStr(api::battleView(State)["enemyPortrait"].asString(), Appearance::mfBeastPortraitPath("火", 6),
            "真实NPC本命大道优先于历史默认战斗style");
        State.player.arts.battle->enemyDao.clear();
        checkStr(api::battleView(State)["enemyPortrait"].asString(), Appearance::mfBeastPortraitPath("土", 6),
            "缺本命大道时回退既有战斗style");
        State.player.arts.battle->style.clear();
        checkStr(api::battleView(State)["enemyPortrait"].asString(), beastPortrait(6), "旧战斗缺主题时保持经典境界图");
    }
    for (const string OpponentId : {"beast-body", "beast-fire", "beast-mind"}) {
        BattleLab::Session Lab;
        BattleLab::Selection Selected; Selected.Stage = 6; Selected.OpponentId = OpponentId;
        const auto Started = BattleLab::mfStart(Lab, Selected);
        check(Started.Succeeded, "独立试炼可以建立真实妖兽战");
        if (!Started.Succeeded) continue;
        const string Saved = saveToJson(Lab.CurrentGame());
        const auto View = BattleLab::mfStateView(Lab);
        const auto Formal = api::battleView(Lab.CurrentGame());
        checkStr(View["battle"]["enemyPortrait"].asString(), Formal["enemyPortrait"].asString(), "试炼页与正式斗法复用同一主题立绘事实");
        checkStr(View["battle"]["enemyPortrait"].asString(), Appearance::mfBeastPortraitPath(View["battle"]["enemyDao"].asString(), 6),
            "试炼妖兽不会再回退散修或只按境界共图");
        checkStr(saveToJson(Lab.CurrentGame()), Saved, "试炼外观查询没有持久字段或RNG副作用");
    }
}
} // namespace nztest
