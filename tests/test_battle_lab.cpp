#include "harness.h"

#include "core/arts.h"
#include "core/battle.h"
#include "core/content/advanced_builds.h"
#include "core/content/npc_combat_profiles.h"
#include "core/combat/battle_lab.h"
#include "core/interface/battle_lab_api.h"
#include <algorithm>
#include <limits>

namespace nztest {
namespace {

// 用途：构造隔离试炼 HTTP 请求；流程：参数/重复动作测试；用法：传路径/JSON/方法；返回：值对象，不访问实际服务器。
nz::http::Request mfLabRequest(const std::string& Path, const std::string& Body = "", const std::string& Method = "POST") {
    nz::http::Request Request;
    Request.path = Path;
    Request.body = Body;
    Request.method = Method;
    return Request;
}

// 用途：读取可用手牌 UID；流程：真实动作测试；用法：传只读会话；返回：首张非指定移除牌的合法 UID，未找到为空。
std::string mfPlayableCard(const nz::BattleLab::Session& LabSession) {
    const auto View = nz::BattleLab::mfStateView(LabSession);
    for (const auto& Card : View["battle"]["hand"].asArray())
        if (Card["playable"].asBool() && !Card["requiresTarget"].asBool()) return Card["uid"].asString();
    return "";
}

} // namespace

// 用途：验证隔离试炼真实 PvE、合法构筑、原子请求和只读预览；流程：本轮定向测试；用法：测试入口调用；返回：通过 harness 记录结果。
void mfRunBattleLabTests() {
    using namespace nz;
    std::printf("\n-- 独立斗法试炼场 --\n");
    BattleLab::Session LabSession;
    const auto Configuration = BattleLab::mfConfigurationView();
    checkEq(Configuration["builds"].asArray().size(), 8, "试炼目录提供真实八个流派");
    checkEq(Configuration["realms"].asArray().size(), 5, "试炼目录提供五个大境界");
    checkEq(Configuration["opponents"].asArray().size(), 4 + SECTS().size() + content::npcCombatProfiles().size(),
        "试炼对手包含三妖兽、散修及内容目录全部宗门/完整传承");
    checkEq(Configuration["tiers"].asArray().size(), 2, "试炼目录区分初始流派和完整传承");
    checkStr(Configuration["defaultSelection"]["tier"].asString(), "initial", "缺省试炼层级是初始流派");
    check(BattleLab::mfStateView(LabSession)["battle"].isNull(), "空试炼不会隐式开局");

    bool AllLegal = true;
    bool AllRealPve = true;
    bool AllBasicDecks = true;
    bool AllStagesOpened = true;
    for (const auto& Build : artBuilds()) {
        for (const int Stage : BattleLab::mfStages()) {
            BattleLab::Selection Selected;
            Selected.BuildId = Build.id;
            Selected.Stage = Stage;
            const auto Started = BattleLab::mfStart(LabSession, Selected);
            AllStagesOpened = AllStagesOpened && Started.Succeeded;
            if (!Started.Succeeded) continue;
            const auto& PlayerState = LabSession.CurrentGame().player;
            const auto& Loadout = PlayerState.arts;
            const auto& BattleState = *Loadout.battle;
            AllRealPve = AllRealPve && !BattleState.shadowPlayer && BattleState.enemyBeast &&
                BattleState.enemyMax == jsRound((110 + BATTLE_POWER[Stage] * 2.4) * 1.45);
            AllLegal = AllLegal && Loadout.aux.size() <= 4 &&
                Loadout.spells.size() <= static_cast<std::size_t>(battleSpellSlots(PlayerState));
            for (const auto& [ManualId, Level] : Loadout.manuals) {
                ManualInfo Definition;
                AllLegal = AllLegal && findManualInfo(ManualId, Definition) &&
                    Definition.stage <= Stage && Level == 5;
            }
            for (const auto& SpellId : Loadout.spells) {
                const auto* Definition = battleSpell(SpellId);
                AllLegal = AllLegal && Definition && Definition->stage <= Stage;
            }
            int Attacks = 0, Defenses = 0;
            for (const auto& Card : BattleState.deck) {
                if (Card.spell == "strike") ++Attacks;
                if (Card.spell == "defend") ++Defenses;
            }
            AllBasicDecks = AllBasicDecks && Attacks == 5 && Defenses == 5;
        }
    }
    check(AllStagesOpened, "八流派五境界均能用真实规则开局");
    check(AllLegal, "所有试炼按功法/神通境界及槽位过滤，合法功法统一五层");
    check(AllRealPve, "试炼是真实普通PvE妖兽倍率，不是留影规则");
    check(AllBasicDecks, "所有预设固定普通攻击和普通防御各五张");

    bool AllAdvancedOpened = true;
    bool AllAdvancedLegal = true;
    bool AllAdvancedDiffer = true;
    bool AllAdvancedReadOnly = true;
    for (const auto& Build : artBuilds()) {
        const auto* Advanced = content::mfFindAdvancedBuild(Build.id);
        AllAdvancedDiffer = AllAdvancedDiffer && Advanced &&
            (Advanced->manuals != Build.manuals || Advanced->spells != Build.spells || Advanced->nodes != Build.nodes);
        for (const int Stage : BattleLab::mfStages()) {
            BattleLab::Selection AdvancedSelection;
            AdvancedSelection.BuildId = Build.id;
            AdvancedSelection.Stage = Stage;
            AdvancedSelection.LoadoutTier = "advanced";
            const auto Started = BattleLab::mfStart(LabSession, AdvancedSelection);
            AllAdvancedOpened = AllAdvancedOpened && Started.Succeeded;
            if (!Started.Succeeded) continue;
            const auto& PlayerState = LabSession.CurrentGame().player;
            const auto& Loadout = PlayerState.arts;
            AllAdvancedLegal = AllAdvancedLegal && Loadout.aux.size() <= 4 &&
                Loadout.spells.size() <= static_cast<std::size_t>(battleSpellSlots(PlayerState));
            for (const auto& [ManualId, Level] : Loadout.manuals) {
                ManualInfo Definition;
                AllAdvancedLegal = AllAdvancedLegal && findManualInfo(ManualId, Definition) &&
                    Definition.stage <= Stage && Level == 5 && Advanced &&
                    std::find(Advanced->manuals.begin(), Advanced->manuals.end(), ManualId) != Advanced->manuals.end();
            }
            for (const auto& SpellId : Loadout.spells) {
                const auto* Spell = battleSpell(SpellId);
                AllAdvancedLegal = AllAdvancedLegal && Spell && Spell->stage <= Stage && Advanced &&
                    std::find(Advanced->spells.begin(), Advanced->spells.end(), SpellId) != Advanced->spells.end();
            }
            const auto Rng = LabSession.CurrentGame().rng;
            const auto FirstView = BattleLab::mfStateView(LabSession).dump();
            const auto SecondView = BattleLab::mfStateView(LabSession).dump();
            AllAdvancedReadOnly = AllAdvancedReadOnly && Rng == LabSession.CurrentGame().rng && FirstView == SecondView &&
                BattleLab::mfStateView(LabSession)["selection"]["tier"].asString() == "advanced";
        }
    }
    check(AllAdvancedOpened, "八套完整传承在五大境界均可真实开局");
    check(AllAdvancedLegal, "完整传承只使用统一目录真实功法神通，依旧遵守境界和槽位");
    check(AllAdvancedDiffer, "八流派完整传承都有真实清单变化，不是初始配置的隐藏伤害倍率");
    check(AllAdvancedReadOnly, "完整传承层级输出明确，重复查询仍不改变活动RNG");

    bool AllOpponentsOpened = true;
    bool AllEliteProfiles = true;
    BattleLab::Selection Selected;
    Selected.Stage = 6;
    for (const auto& Opponent : BattleLab::mfOpponents()) {
        Selected.OpponentId = Opponent.Id;
        const auto Started = BattleLab::mfStart(LabSession, Selected);
        AllOpponentsOpened = AllOpponentsOpened && Started.Succeeded;
        if (!Started.Succeeded) continue;
        const auto& BattleState = *LabSession.CurrentGame().player.arts.battle;
        AllEliteProfiles = AllEliteProfiles &&
            BattleState.enemyBeast == Opponent.IsBeast && BattleState.enemyCards.enabled == !Opponent.IsBeast;
        if (!Opponent.ProfileId.empty())
            AllEliteProfiles = AllEliteProfiles && LabSession.CurrentGame().npcs.front().combatProfileId == Opponent.ProfileId;
    }
    check(AllOpponentsOpened, "目录全部真实PvE对手都能开局");
    check(AllEliteProfiles, "真实修士AI与完整传承模板均启用");

    Selected.OpponentId = "beast-body";
    Selected.Seed = 104729;
    BattleLab::Session Repeated;
    const auto FirstStart = BattleLab::mfStart(LabSession, Selected);
    const auto SecondStart = BattleLab::mfStart(Repeated, Selected);
    check(FirstStart.Succeeded && SecondStart.Succeeded &&
        LabSession.CurrentGame().rng == Repeated.CurrentGame().rng &&
        LabSession.CurrentGame().player.arts.battle->hand == Repeated.CurrentGame().player.arts.battle->hand &&
        LabSession.CurrentGame().player.arts.battle->draw == Repeated.CurrentGame().player.arts.battle->draw,
        "同流派、境界、对手和种子真实发牌可复现");
    BattleLab::Session IndependentCopy = LabSession;
    IndependentCopy.CurrentGame().player.health = 37;
    check(&IndependentCopy.CurrentGame() != &LabSession.CurrentGame() && LabSession.CurrentGame().player.health == 100,
        "事务会话深拷贝真实Game，不共享可变角色状态");

    const auto RngBefore = LabSession.CurrentGame().rng;
    const int DaysBefore = LabSession.CurrentGame().clock.days;
    const auto HandBefore = LabSession.CurrentGame().player.arts.battle->hand;
    const auto NpcBagBefore = LabSession.CurrentGame().npcs.front().bag;
    const auto StateBefore = BattleLab::mfStateView(LabSession).dump();
    const auto StateAgain = BattleLab::mfStateView(LabSession).dump();
    check(StateBefore == StateAgain && RngBefore == LabSession.CurrentGame().rng &&
        DaysBefore == LabSession.CurrentGame().clock.days && HandBefore == LabSession.CurrentGame().player.arts.battle->hand &&
        NpcBagBefore == LabSession.CurrentGame().npcs.front().bag,
        "重复快照/未来状态预演不改变活动RNG、月历、牌堆或NPC背包");

    Selected.Stage = 2;
    const auto InvalidStage = BattleLab::mfStart(LabSession, Selected);
    check(!InvalidStage.Succeeded && BattleLab::mfStateView(LabSession).dump() == StateBefore,
        "非法境界开局原子拒绝");
    Selected.Stage = 6;
    Selected.BuildId = "missing-build";
    const auto InvalidBuild = BattleLab::mfStart(LabSession, Selected);
    check(!InvalidBuild.Succeeded && BattleLab::mfStateView(LabSession).dump() == StateBefore,
        "非法流派开局不覆盖当前战斗");
    const auto InvalidCard = BattleLab::mfTurn(LabSession, "spell", "gold", "", LabSession.Revision);
    check(!InvalidCard.Succeeded && BattleLab::mfStateView(LabSession).dump() == StateBefore,
        "出牌需真实手牌UID，不能用神通ID绕过所持卡牌");

    const int RevisionBefore = LabSession.Revision;
    const std::string CardUid = mfPlayableCard(LabSession);
    const auto UnusedTarget = BattleLab::mfTurn(LabSession, "spell", CardUid, "unknown-target", RevisionBefore);
    check(!UnusedTarget.Succeeded && BattleLab::mfStateView(LabSession).dump() == StateBefore,
        "无需指定目标的牌不会静默接受未知目标");
    const auto Played = BattleLab::mfTurn(LabSession, "spell", CardUid, "", RevisionBefore);
    check(Played.Succeeded && LabSession.Revision == RevisionBefore + 1, "合法真实手牌成功提交且Revision增加一次");
    const auto StateAfter = BattleLab::mfStateView(LabSession).dump();
    const auto Duplicate = BattleLab::mfTurn(LabSession, "spell", CardUid, "", RevisionBefore);
    check(!Duplicate.Succeeded && BattleLab::mfStateView(LabSession).dump() == StateAfter,
        "重复点击的旧Revision拒绝，不再扣灵力或抽牌");

    const std::vector<std::string> InvalidInputs{
        "{\"buildId\":\"metal-discard\",\"stage\":3.5,\"opponentId\":\"beast-body\",\"seed\":1}",
        "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":-1}",
        "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":4294967296}",
        "{\"buildId\":\"metal-discard\",\"stage\":true,\"opponentId\":\"beast-body\",\"seed\":1}",
        "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":1,\"hp\":999999}",
        "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":1,\"tier\":\"ancient\"}",
        "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":1,\"tier\":null}",
        "{not-json}", "null"
    };
    bool AllApiRejectionsAtomic = true;
    for (const auto& Input : InvalidInputs) {
        const auto Response = BattleLab::mfHandle(LabSession, mfLabRequest("/api/lab/start", Input));
        AllApiRejectionsAtomic = AllApiRejectionsAtomic && Response.status == 400 &&
            BattleLab::mfStateView(LabSession).dump() == StateAfter;
    }
    check(AllApiRejectionsAtomic, "HTTP拒绝小数、负数、溢出、布尔、未知属性/层级及畸形JSON，完整状态不变");
    const auto StaleResponse = BattleLab::mfHandle(LabSession,
        mfLabRequest("/api/lab/turn", "{\"action\":\"end\",\"revision\":" + std::to_string(RevisionBefore) + "}"));
    checkEq(StaleResponse.status, 409, "过期HTTP动作明确返回409");
    const auto WrongMethod = BattleLab::mfHandle(LabSession, mfLabRequest("/api/lab/start", "", "GET"));
    checkEq(WrongMethod.status, 405, "GET不能启动或覆盖试炼");
    const auto UnknownPath = BattleLab::mfHandle(LabSession, mfLabRequest("/api/save", "{}"));
    checkEq(UnknownPath.status, 404, "试炼没有人生存档或正式游戏接口");

    const auto ZeroSeedResponse = BattleLab::mfHandle(LabSession,
        mfLabRequest("/api/lab/start", "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":0}"));
    check(ZeroSeedResponse.status == 200 && LabSession.CurrentGame().seed == 0 && LabSession.CurrentSelection.LoadoutTier == "initial",
        "种子0合法、旧请求缺省tier回到初始流派，不沿用先前完整传承");
    const auto MaximumSeedResponse = BattleLab::mfHandle(LabSession,
        mfLabRequest("/api/lab/start", "{\"buildId\":\"metal-discard\",\"stage\":3,\"opponentId\":\"beast-body\",\"seed\":4294967295}"));
    check(MaximumSeedResponse.status == 200 && LabSession.CurrentGame().seed == std::numeric_limits<std::uint32_t>::max(),
        "完整uint32最大种子在JSON边界精确保留");

    BattleLab::Selection EndingSelection;
    EndingSelection.BuildId = "mind-cycle";
    EndingSelection.Stage = 3;
    const auto StartedEnding = BattleLab::mfStart(LabSession, EndingSelection);
    bool AllTurnsSucceeded = StartedEnding.Succeeded;
    int CompletedEndActions = 0;
    for (int EndCount = 0; EndCount < 24 && battleActive(LabSession.CurrentGame()); ++EndCount) {
        const auto EndedTurn = BattleLab::mfTurn(LabSession, "end", "", "", LabSession.Revision);
        AllTurnsSucceeded = AllTurnsSucceeded && EndedTurn.Succeeded;
        if (EndedTurn.Succeeded) ++CompletedEndActions;
    }
    const auto EndState = BattleLab::mfStateView(LabSession);
    check(AllTurnsSucceeded && !battleActive(LabSession.CurrentGame()) && LabSession.MatchResult.has_value() &&
        EndState["battle"].isNull() && EndState["result"].isObject(), "真实PvE结局清战斗后仍保留结果供测试页展示");
    check(LabSession.MatchResult && LabSession.MatchResult->Turns == CompletedEndActions &&
        std::find(LabSession.CombatLog.begin(), LabSession.CombatLog.end(), "最后行动：结束回合。") != LabSession.CombatLog.end(),
        "终结回合计数对应真实已执行end，末次行动摘要不伪造伤害");
    checkEq(LabSession.CurrentGame().clock.days, 0, "整个试炼不会推进人生月历或世界时间");
}

} // namespace nztest
