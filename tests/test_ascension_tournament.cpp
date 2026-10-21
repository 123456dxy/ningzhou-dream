#include "harness.h"
#include "core/model.h"
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#include "core/combat/artifact_effects.h"
#include "core/combat/battle_scaling.h"
#include "core/exploration/ascension_tournament.h"
#include "core/foundation/state_limits.h"
#include "core/interface/ascension_api.h"
#include "core/persistence/ascension_codec.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/talents.h"

#include <algorithm>

namespace nztest {
// Production changes caught here: repricing dominated starting talents and adding
// a combat-only heirloom without increasing everyone's starting attributes.
void mfRunAscensionTalentCatalogTests() {
    const std::map<std::string, int> Costs = {{"herb", 3}, {"root", 6}, {"body", 2},
        {"blade", 2}, {"calm", 1}, {"genius", 6}, {"family", 2}, {"artifactHeir", 5}};
    for (const auto& [Id, ExpectedCost] : Costs) {
        const nz::TalentDef* Match = nullptr;
        for (const auto& Talent : nz::TALENTS()) if (Talent.id == Id) Match = &Talent;
        check(Match && Match->cost == ExpectedCost, Id + "开局天赋点费符合定位");
    }
}

namespace {
nz::Game mfTournamentFixture(const std::vector<std::string>& Talents = {}) {
    nz::CreateOptions Options;
    Options.seed = 734;
    Options.name = "升仙大会隔离角色";
    Options.traits = Talents;
    Options.traitsExplicit = Options.allocationExplicit = true;
    nz::Game State = nz::createGame(Options);
    State.pending.reset();
    State.player.AscensionTournament.OfferedYear = -1;
    State.npcs.clear();
    return State;
}
}

void mfRunAscensionTournamentTests() {
    using namespace nz;
    Game State = mfTournamentFixture();
    const string BeforeView = saveToJson(State);
    const auto View = api::view::mfAscensionTournamentView(State);
    check(View["isTournamentYear"].asBool() && View["enabled"].asBool(), "人生首年提供升仙大会资格");
    checkStr(saveToJson(State), BeforeView, "大会预览不改变角色、时钟或随机数");
    check(mfOfferAscensionTournament(State), "真实空闲动作边界产生大会邀请");
    check(State.pending && State.pending->type == AscensionTournamentBattleKind, "大会邀请使用独立剧情类型");
    check(!mfOfferAscensionTournament(State), "同届邀请不会重复生成");
    check(mfChooseAscensionTournament(State, "decline"), "可先拒绝邀请并整备");
    check(!State.pending && mfAscensionTournamentEntryError(State).empty(), "暂不参赛保留本年报名资格");
    const int InitialStones = State.player.stones;
    const int InitialPills = State.player.arts.potions[AscensionTournamentPillId];
    string Error;
    check(mfEnterAscensionTournament(State, &Error), "合法报名进入首场连续擂台");
    check(State.player.AscensionTournament.Active && State.player.arts.battle &&
        State.player.arts.battle->kind == AscensionTournamentBattleKind, "大会激活并进入实际战斗引擎");
    check(mfAscensionTournamentStateError(State).empty(), "首场大会持久状态完整");
    check(!battleDefeatEndsLife(*State.player.arts.battle), "大会失败明确不结束人生");
    const string BeforeDuplicate = saveToJson(State);
    check(!mfEnterAscensionTournament(State, &Error), "重复报名请求明确拒绝");
    checkStr(saveToJson(State), BeforeDuplicate, "重复报名不消耗随机数或覆盖在途战斗");
    check(!artInstant(State, "main", "tuna").ok, "大会连战不能穿插切换功法");
    check(!artInstant(State, "spell", "water").ok, "大会连战不能穿插更换神通");
    checkStr(saveToJson(State), BeforeDuplicate, "连战中拒绝换装不部分改变角色");
    const auto FrozenSects = State.player.AscensionTournament.OpponentSects;
    checkEq(FrozenSects.size(), 3, "入场冻结三个不同的宗门对手");
    check(FrozenSects[0] != FrozenSects[1] && FrozenSects[0] != FrozenSects[2] && FrozenSects[1] != FrozenSects[2],
        "本届三位宗门对手来自不同宗门");
    for (int Round = 0; Round < 4; ++Round) {
        check(State.player.arts.battle.has_value(), "每场胜利无空档地开始下一场");
        if (!State.player.arts.battle) break;
        checkEq(State.player.arts.battle->enemyStage, std::max(0, Round - 1), "对手按初初中后逐渐提升境界");
        checkStr(State.player.arts.battle->enemySect, Round == 0 ? "" : FrozenSects[static_cast<size_t>(Round - 1)],
            "真实对手与入场冻结宗门一致");
        State.player.arts.battle->hp = 60;
        const Battle FinishedBattle = *State.player.arts.battle;
        battleFinish(State, true);
        checkEq(State.player.AscensionTournament.Round, Round + 1, "真实胜利回调推进一次场次");
        checkEq(State.player.stones, InitialStones + (Round == 3 ? 500 : 0), "仅四战全胜结算五百灵石");
        checkEq(State.player.arts.potions[AscensionTournamentPillId], InitialPills + (Round == 3 ? 1 : 0), "仅四战全胜结算一枚筑基丹");
        if (Round < 3 && State.player.arts.battle)
            check(State.player.arts.battle->hp <= 60 + mfHealthPercentAmount(mfPlayerHealthMaximum(State.player), 20),
                "场间最多恢复两成上限并承接伤势，不偷偷回满");
        if (Round == 3) {
            mfCompleteAscensionTournamentBattle(State, FinishedBattle, true, false);
            checkEq(State.player.stones, InitialStones + 500, "重复末场结算不重复发奖");
        }
    }
    check(!State.player.AscensionTournament.Active && !State.player.arts.battle && !State.ended,
        "四场结束后释放大会阻塞且保留人生");
    checkEq(State.player.health, 60, "最后一战不会获得额外恢复");
    check(!mfEnterAscensionTournament(State, &Error), "通关后不能重刷同届奖励");

    State = mfTournamentFixture();
    check(mfEnterAscensionTournament(State), "非致命失败夹具进入大会");
    State.player.arts.battle->hp = 0;
    battleFinish(State, false);
    check(!State.ended && !State.player.AscensionTournament.Active && State.player.health == 1,
        "大会落败保存实际伤势且最低一血，不会结束人生");
    check(!mfEnterAscensionTournament(State), "落败同样消耗本届唯一参赛资格");
    State = mfTournamentFixture();
    check(mfEnterAscensionTournament(State), "撤离夹具进入大会");
    const auto EscapeResult = battleTurn(State, "escape");
    check(EscapeResult.ok && !State.ended && !State.player.AscensionTournament.Active,
        "大会认输通过真实战斗动作结束四连战");
    check(!mfEnterAscensionTournament(State), "认输不能重复报名同届");

    State = mfTournamentFixture();
    for (int Year : {1, 4, 6}) {
        State.year = Year;
        check(!mfIsAscensionTournamentYear(State) && !mfEnterAscensionTournament(State), "非五年届次明确拒绝报名");
    }
    for (int Year : {0, 5, 10, 15}) {
        State.year = Year;
        check(mfIsAscensionTournamentYear(State), "大会从人生首年偏移每五年举办");
    }
    State.year = 0;
    State.player.age = 39;
    check(mfAscensionTournamentEntryError(State).empty(), "三十九岁仍可参加大会");
    State.player.age = 40;
    check(!mfAscensionTournamentEntryError(State).empty(), "刚满四十岁明确失去参赛资格");
    State.player.age = 16;
    State.player.stage = 3;
    check(!mfAscensionTournamentEntryError(State).empty(), "筑基修士不能参加炼气大会");

    State = mfTournamentFixture();
    check(mfEnterAscensionTournament(State), "在途持久化夹具进入大会");
    Game Loaded;
    check(loadFromJson(saveToJson(State), Loaded, &Error), "大会首场与冻结宗门可以完整存读档");
    checkStr(saveToJson(Loaded), saveToJson(State), "读档不重抽大会对手、牌堆或奖励");
    for (const bool FillPillInventory : {false, true}) {
        auto OverflowRoot = json::Value::parse(saveToJson(State));
        auto OverflowPlayer = OverflowRoot["player"];
        if (FillPillInventory) {
            auto OverflowArts = OverflowPlayer["arts"];
            auto OverflowPotions = OverflowArts["potions"];
            OverflowPotions.set(AscensionTournamentPillId, StateLimits::MaxPersistedInteger);
            OverflowArts.set("potions", OverflowPotions);
            OverflowPlayer.set("arts", OverflowArts);
        } else OverflowPlayer.set("stones", StateLimits::MaxPersistedInteger);
        OverflowRoot.set("player", OverflowPlayer);
        const string BeforeOverflowImport = saveToJson(Loaded);
        check(!loadFromJson(OverflowRoot.dump(), Loaded, &Error),
            FillPillInventory ? "在途大会拒绝无法承接筑基丹奖励的导入" : "在途大会拒绝无法承接灵石奖励的导入");
        checkStr(saveToJson(Loaded), BeforeOverflowImport, "奖励容量坏档拒绝后不覆盖当前角色、战斗或随机数");
    }
    Game CapacityState = State;
    CapacityState.player.stones = StateLimits::MaxPersistedInteger - AscensionTournamentRewardStones;
    CapacityState.player.arts.potions[AscensionTournamentPillId] = StateLimits::MaxPersistedInteger - 1;
    Game CapacityLoaded;
    check(loadFromJson(saveToJson(CapacityState), CapacityLoaded, &Error), "在途大会恰好保留奖励空间时允许导入");
    for (int Round = 0; Round < AscensionTournamentRoundCount && battleActive(CapacityLoaded); ++Round)
        battleFinish(CapacityLoaded, true);
    checkEq(CapacityLoaded.player.stones, StateLimits::MaxPersistedInteger, "灵石容量边界通关奖励不超过持久化上限");
    checkEq(CapacityLoaded.player.arts.potions[AscensionTournamentPillId], StateLimits::MaxPersistedInteger,
        "筑基丹容量边界通关奖励不超过持久化上限");
    Game CapacityRestored;
    check(loadFromJson(saveToJson(CapacityLoaded), CapacityRestored, &Error), "容量边界通关后仍可完整存读档");
    auto Root = json::Value::parse(saveToJson(State));
    auto InvalidTournament = Root["player"]["ascensionTournament"];
    InvalidTournament.set("round", 4);
    auto InvalidPlayer = Root["player"];
    InvalidPlayer.set("ascensionTournament", InvalidTournament);
    Root.set("player", InvalidPlayer);
    const string PreviousLoaded = saveToJson(Loaded);
    check(!loadFromJson(Root.dump(), Loaded, &Error), "拒绝已完成四场仍在进行中的伪造大会存档");
    checkStr(saveToJson(Loaded), PreviousLoaded, "大会坏档拒绝后不部分覆盖当前角色");
    auto CodecValue = persistence::mfAscensionTournamentToJson(State.player.AscensionTournament);
    CodecValue.set("active", 1);
    check(!persistence::mfAscensionTournamentShapeValid(CodecValue), "大会活动标记不接受数字冒充布尔");
    CodecValue = persistence::mfAscensionTournamentToJson(State.player.AscensionTournament);
    CodecValue.set("round", 1.5);
    check(!persistence::mfAscensionTournamentShapeValid(CodecValue), "大会场次不接受小数");
    CodecValue = persistence::mfAscensionTournamentToJson(State.player.AscensionTournament);
    CodecValue.set("unknown", 1);
    check(!persistence::mfAscensionTournamentShapeValid(CodecValue), "大会存档拒绝未知字段");
    CodecValue = persistence::mfAscensionTournamentToJson(State.player.AscensionTournament);
    CodecValue.set("attemptedYear", -2);
    check(!persistence::mfAscensionTournamentShapeValid(CodecValue), "大会届次拒绝非法负数");
    AscensionTournamentState Unchanged = State.player.AscensionTournament;
    const string BeforeCodec = persistence::mfAscensionTournamentToJson(Unchanged).dump();
    check(!persistence::mfAscensionTournamentFromJson(CodecValue, Unchanged, Error), "大会解码先校验再发布");
    checkStr(persistence::mfAscensionTournamentToJson(Unchanged).dump(), BeforeCodec,
        "大会坏字段不会部分改变解码输出");

    State = mfTournamentFixture();
    Root = json::Value::parse(saveToJson(State));
    auto LegacyPlayer = Root["player"];
    LegacyPlayer.asObject().erase("ascensionTournament");
    Root.set("player", LegacyPlayer);
    check(loadFromJson(Root.dump(), Loaded, &Error), "旧档缺大会字段仍可导入");
    check(Loaded.player.AscensionTournament.FirstYear == -1 && !Loaded.pending &&
        !Loaded.player.AscensionTournament.Active, "旧档导入保持默认状态，不在查询或解码补发邀请");
    check(!mfOfferAscensionTournament(Loaded), "旧档未初始化状态不在后续自动动作补发大会邀请");
    const string BeforeLegacyView = saveToJson(Loaded);
    check(api::view::mfAscensionTournamentView(Loaded)["enabled"].asBool(), "旧档首年可只读显示大会资格");
    checkStr(saveToJson(Loaded), BeforeLegacyView, "旧档大会预览不初始化持久字段");

    State = mfTournamentFixture({"artifactHeir"});
    checkEq(State.player.arts.gear.size(), 2, "护道遗宝真实出生生成两件法器");
    checkEq(State.player.arts.equipped.size(), 2, "遗宝只占用原有武器与配饰槽");
    checkEq(mfArtifactCombatSnapshot(State.player).RoundShield, 8, "炼气开局遗宝每轮真实提供八点护盾");
    const auto Reference = battleReferenceState(State.player);
    bool HasDamageArtifact = false;
    for (const auto& Ability : equippedArtifactAbilities(State.player)) {
        if (Ability.gear.EffectId != "damage") continue;
        HasDamageArtifact = true;
        const auto Preview = battlePreview(State.player, Reference, Ability.spell.id);
        check(Preview.valid && Preview.RawDamage == 40, "炼气开局遗宝直伤预览为四十点");
    }
    check(HasDamageArtifact, "遗宝主动法器在第一年即可催动");
    const auto InitialGearCount = State.player.arts.gear.size();
    talentBirth(State);
    checkEq(State.player.arts.gear.size(), InitialGearCount, "重复出生hook不会重复发放遗宝");
    check(loadFromJson(saveToJson(State), Loaded, &Error), "出生遗宝来源与大会状态共同往返存档");
}
} // namespace nztest
