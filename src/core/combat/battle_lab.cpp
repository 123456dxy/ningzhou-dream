#include "core/combat/battle_lab.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/character_build.h"
#include "core/content/advanced_builds.h"
#include "core/content/game_ids.h"
#include "core/content/npc_combat_profiles.h"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace nz::BattleLab {
namespace {

constexpr int PreparedManualLevel = 5;
constexpr std::size_t MaximumAuxiliaryManuals = 4;

// 用途：按初始/完整传承查唯一流派目录；流程：合法开局校验；用法：传选择中的稳定 ID 与层级；返回：借用静态定义或空。
const BuildInfo* mfFindBuild(const Selection& Requested) {
    if (Requested.LoadoutTier == "advanced") return content::mfFindAdvancedBuild(Requested.BuildId);
    if (Requested.LoadoutTier != "initial") return nullptr;
    const auto& Builds = artBuilds();
    const auto Found = std::find_if(Builds.begin(), Builds.end(), [&](const BuildInfo& Build) {
        return Build.id == Requested.BuildId;
    });
    return Found == Builds.end() ? nullptr : &*Found;
}

// 用途：查白名单对手；流程：开局校验；用法：传稳定 ID；返回：借用静态定义或空。
const OpponentDefinition* mfFindOpponent(const std::string& OpponentId) {
    for (const auto& Opponent : mfOpponents())
        if (Opponent.Id == OpponentId) return &Opponent;
    return nullptr;
}

// 用途：构造不带天赋、装备和丹药加成的标准角色；流程：试炼候选初始化；用法：传选择和流派；返回：拥有独立道法状态的 Player。
Player mfPreparePlayer(const Selection& Requested, const BuildInfo& Build,
                       std::vector<std::string>& Unavailable) {
    Player Prepared;
    Prepared.name = "斗法试炼者";
    Prepared.gender = "男";
    Prepared.age = 20;
    Prepared.stage = Requested.Stage;
    Prepared.highestStage = Requested.Stage;
    Prepared.lifespan = realmOf(Requested.Stage).life;
    Prepared.apt = 90;
    Prepared.insight = 37;
    Prepared.health = 100;
    Prepared.body = 0;
    Prepared.talent.clear();
    Prepared.traits.clear();
    Prepared.portraitId = defaultPlayerPortraitId(Prepared.gender);
    Prepared.arts = Arts{};
    auto& Loadout = Prepared.arts;
    Loadout.focus = Build.dao;
    Loadout.main.clear();
    Loadout.manuals.clear();
    Loadout.spells.clear();
    Loadout.knownSpells.clear();
    for (const auto& ManualId : Build.manuals) {
        ManualInfo Definition;
        if (!findManualInfo(ManualId, Definition)) continue;
        if (Definition.stage > Requested.Stage) {
            Unavailable.push_back(Definition.name + "：需" + realmName(Definition.stage));
            continue;
        }
        if (Loadout.main.empty()) Loadout.main = ManualId;
        else if (Loadout.aux.size() < MaximumAuxiliaryManuals) Loadout.aux.push_back(ManualId);
        else {
            Unavailable.push_back(Definition.name + "：辅修槽已满");
            continue;
        }
        Loadout.manuals[ManualId] = PreparedManualLevel;
    }
    if (Loadout.main.empty()) {
        Loadout.main = "tuna";
        Loadout.manuals[Loadout.main] = PreparedManualLevel;
    }
    const int ExtraSpellSlots = battleSpellSlots(Prepared);
    for (const auto& SpellId : Build.spells) {
        const SpellDef* Definition = battleSpell(SpellId);
        if (!Definition || SpellId == "strike" || SpellId == "defend") continue;
        if (Definition->stage > Requested.Stage) {
            Unavailable.push_back(Definition->name + "：需" + realmName(Definition->stage));
            continue;
        }
        if (static_cast<int>(Loadout.spells.size()) >= ExtraSpellSlots) {
            Unavailable.push_back(Definition->name + "：额外神通槽已满");
            continue;
        }
        if (std::find(Loadout.spells.begin(), Loadout.spells.end(), SpellId) == Loadout.spells.end())
            Loadout.spells.push_back(SpellId);
    }
    Loadout.knownSpells = Loadout.spells;
    // 每条建议节点都补齐其大道前置；悟道取得不受人物大境界限制，试炼统一视作已完整领悟。
    for (const auto& NodeId : Build.nodes) {
        const NodeDef* Definition = findNode(NodeId);
        if (!Definition) continue;
        const std::string Dao(ART_DAOS[Definition->dao]);
        Loadout.exp[Dao] = std::max(Loadout.exp[Dao], ART_THRESHOLDS[Definition->level]);
        for (int NodeLevel = 1; NodeLevel <= Definition->level; ++NodeLevel) {
            const std::string Prerequisite = std::to_string(Definition->dao) + "-" + std::to_string(NodeLevel);
            if (std::find(Loadout.nodes.begin(), Loadout.nodes.end(), Prerequisite) == Loadout.nodes.end())
                Loadout.nodes.push_back(Prerequisite);
        }
    }
    return Prepared;
}

// 用途：整体提交已完成候选会话；流程：开局/行动收口；用法：传活动和独立候选；返回：无，交换不抛异常。
void mfCommit(Session& LabSession, Session& Candidate) noexcept {
    static_assert(std::is_nothrow_move_constructible_v<Session> && std::is_nothrow_move_assignable_v<Session>);
    using std::swap;
    swap(LabSession, Candidate);
}

} // namespace

// 用途：构造拥有独立 Game 的空会话；流程：服务/候选建立；用法：默认构造；返回：构造后即可查询的会话。
Session::Session() : OwnedGame(std::make_unique<Game>()) {}

// 用途：深拷贝试炼状态；流程：事务候选；用法：传有效会话；返回：所有可变数据独立，分配失败不触碰源。
Session::Session(const Session& Source)
    : OwnedGame(std::make_unique<Game>(Source.CurrentGame())),
      CurrentSelection(Source.CurrentSelection), Revision(Source.Revision), HasMatch(Source.HasMatch),
      MatchResult(Source.MatchResult), CombatLog(Source.CombatLog), Unavailable(Source.Unavailable) {}

// 用途：以复制后无抛交换提供强异常保证；流程：会话赋值；用法：传有效源；返回：自身引用。
Session& Session::operator=(const Session& Source) {
    if (this == &Source) return *this;
    Session Candidate(Source);
    mfCommit(*this, Candidate);
    return *this;
}

// 用途：借用试炼 Game；流程：领域动作/测试；用法：未移动会话；返回：可写借用引用，移走状态拒绝访问。
Game& Session::CurrentGame() {
    if (!OwnedGame) throw std::logic_error("已移动的试炼会话不能访问 Game。 ");
    return *OwnedGame;
}

// 用途：只读借用试炼 Game；流程：视图/候选复制；用法：未移动会话；返回：常引用，不写外部状态。
const Game& Session::CurrentGame() const {
    if (!OwnedGame) throw std::logic_error("已移动的试炼会话不能访问 Game。 ");
    return *OwnedGame;
}

// 用途：集中白名单普通 PvE 对手；流程：配置/真实开局；用法：只读遍历；返回：稳定定义，不生成世界 NPC。
std::span<const OpponentDefinition> mfOpponents() {
    static const std::vector<OpponentDefinition> Definitions = [] {
        std::vector<OpponentDefinition> Catalog{
        {"beast-body", "厚甲妖兽", "体", "", "", true},
        {"beast-fire", "赤焰妖兽", "火", "", "", true},
        {"beast-mind", "裂魂妖兽", "神", "", "", true},
        {"scatter", "同境散修", "神", "", "", false}
        };
        // 普通宗门与完整传承均取唯一内容目录；新增宗门不需要在试炼另写一套技能表。
        for (const auto& Sect : SECTS()) {
            const auto SectId = content::parseSectId(Sect.id);
            if (!SectId) throw std::logic_error("试炼宗门目录含未注册宗门。 ");
            Catalog.push_back({Sect.id, std::string(Sect.name) + "弟子",
                std::string(content::legacyDaoKey(content::sectDao(*SectId))), Sect.id, "", false});
        }
        for (const auto& Profile : content::npcCombatProfiles()) {
            const auto SectId = content::parseSectId(Profile.sect);
            const auto* Sect = findSect(Profile.sect);
            if (!SectId || !Sect) throw std::logic_error("试炼完整传承目录含无效宗门。 ");
            Catalog.push_back({Profile.id, std::string(Sect->name) + "完整传承精英",
                std::string(content::legacyDaoKey(content::sectDao(*SectId))), Profile.sect, Profile.id, false});
        }
        return Catalog;
    }();
    return Definitions;
}

// 用途：列出五个大境界初期；流程：选择合法性校验；用法：无参数；返回：不可变静态索引。
std::span<const int> mfStages() {
    static constexpr std::array<int, 5> Stages{0, 3, 6, 9, 12};
    return Stages;
}

// 用途：用真实规则启动内存试炼；流程：配置提交；用法：先校验 ID/境界/Revision；返回：成功或拒绝，拒绝没有部分状态。
ActionResult mfStart(Session& LabSession, const Selection& Requested, std::optional<int> ExpectedRevision) {
    if (ExpectedRevision && *ExpectedRevision != LabSession.Revision)
        return {false, "战况已更新，请刷新后再试。"};
    if (LabSession.Revision == std::numeric_limits<int>::max())
        return {false, "试炼版本号已达上限，请重启试炼服务。"};
    const auto* Build = mfFindBuild(Requested);
    const auto* Opponent = mfFindOpponent(Requested.OpponentId);
    const auto Stages = mfStages();
    if (!Build || !Opponent || std::find(Stages.begin(), Stages.end(), Requested.Stage) == Stages.end())
        return {false, "未知流派、构筑层级、境界或对手。"};
    Session Candidate;
    Candidate.CurrentSelection = Requested;
    Candidate.Revision = LabSession.Revision + 1;
    Candidate.HasMatch = true;
    auto& GameState = Candidate.CurrentGame();
    GameState.seed = GameState.rng = Requested.Seed;
    GameState.player = mfPreparePlayer(Requested, *Build, Candidate.Unavailable);
    GameState.clock.ageBase = GameState.player.age;
    EnemySpec Enemy;
    Enemy.name = Opponent->Name;
    Enemy.id = "battle-lab-opponent";
    Enemy.kind = "spar";
    Enemy.stage = Requested.Stage;
    Enemy.style = Opponent->Dao;
    Enemy.sect = Opponent->Sect;
    Enemy.beast = Opponent->IsBeast;
    Enemy.gender = "男";
    Npc PreparedOpponent;
    PreparedOpponent.id = Enemy.id;
    PreparedOpponent.name = Enemy.name;
    PreparedOpponent.stage = Enemy.stage;
    PreparedOpponent.health = 100;
    PreparedOpponent.body = 0;
    PreparedOpponent.sect = Enemy.sect;
    PreparedOpponent.dao = Enemy.style;
    PreparedOpponent.gender = Enemy.gender;
    PreparedOpponent.beast = Enemy.beast;
    PreparedOpponent.combatProfileId = Opponent->ProfileId;
    PreparedOpponent.bag.clear();
    GameState.npcs.push_back(std::move(PreparedOpponent));
    battleStart(GameState, Enemy);
    if (!battleActive(GameState)) return {false, "试炼开局失败。"};
    Candidate.CombatLog = GameState.player.arts.battle->log;
    mfCommit(LabSession, Candidate);
    return {true, ""};
}

// 用途：在候选状态上执行手牌动作并捕获普通 PvE 结局；流程：试炼出牌；用法：传最新 Revision；返回：成功或无副作用拒绝。
ActionResult mfTurn(Session& LabSession, const std::string& Action, const std::string& SpellUid,
                    const std::string& Target, int ExpectedRevision) {
    if (ExpectedRevision != LabSession.Revision) return {false, "战况已更新，请刷新后再试。"};
    if (!LabSession.HasMatch || !battleActive(LabSession.CurrentGame())) return {false, "请先开始一场试炼。"};
    if (LabSession.Revision == std::numeric_limits<int>::max()) return {false, "试炼版本号已达上限，请重启试炼服务。"};
    if (Action != "spell" && Action != "end") return {false, "未知试炼行动。"};
    const Battle& Before = *LabSession.CurrentGame().player.arts.battle;
    if (Action == "spell" && std::find(Before.hand.begin(), Before.hand.end(), SpellUid) == Before.hand.end())
        return {false, "请选择当前手牌。"};
    if (Action == "end" && (!SpellUid.empty() || !Target.empty())) return {false, "结束回合不能附带神通或目标。"};
    std::string LastActionName = "结束回合";
    if (Action == "spell") {
        const auto HeldCard = std::find_if(Before.deck.begin(), Before.deck.end(), [&](const Card& CardDefinition) {
            return CardDefinition.uid == SpellUid;
        });
        const SpellDef* HeldSpell = HeldCard == Before.deck.end() ? nullptr : battleSpell(HeldCard->spell);
        if (!HeldSpell) return {false, "当前手牌定义无效。"};
        if (!Target.empty() && !battleNeedsTarget(*HeldSpell)) return {false, "这门神通不需要移除目标。"};
        LastActionName = "施放「" + HeldSpell->name + "」";
    }
    Session Candidate = LabSession;
    const int WinsBefore = Candidate.CurrentGame().player.arts.progress.wins;
    const std::size_t WorldLogCountBefore = Candidate.CurrentGame().logs.size();
    const TurnResult Turn = battleTurn(Candidate.CurrentGame(), Action == "end" ? "end" : SpellUid, Target);
    if (!Turn.ok) return {false, Turn.error};
    ++Candidate.Revision;
    if (battleActive(Candidate.CurrentGame())) {
        Candidate.CombatLog = Candidate.CurrentGame().player.arts.battle->log;
    } else {
        const bool Won = Candidate.CurrentGame().player.arts.progress.wins > WinsBefore;
        Candidate.MatchResult = Result{Won, Before.turn + 1, Won ? "试炼获胜。" : "试炼落败，可使用同一种子再试。"};
        // 普通 PvE 收口会清除 Battle：只记录确实执行的末次行动，不把预览伤害冒充最终战报。
        Candidate.CombatLog.push_back("最后行动：" + LastActionName + "。");
        for (std::size_t LogIndex = WorldLogCountBefore; LogIndex < Candidate.CurrentGame().logs.size(); ++LogIndex)
            Candidate.CombatLog.push_back(Candidate.CurrentGame().logs[LogIndex].text);
        Candidate.CombatLog.push_back(Candidate.MatchResult->Text);
    }
    mfCommit(LabSession, Candidate);
    return {true, ""};
}

} // namespace nz::BattleLab
