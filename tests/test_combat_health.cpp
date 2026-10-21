#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>

namespace nztest {
namespace {
using namespace nz;

// 类型：测试专用独立API会话，拥有隔离Game；绑定期间所有请求只访问此会话，析构恢复默认绑定。
struct CombatHealthSession {
    api::Session SessionState;
    // 用途：绑定自有API会话；流程：健康数据回归；返回：合法空会话，无用户服务或网络连接。
    CombatHealthSession() { api::bindSession(&SessionState); }
    // 用途：在所有返回路径恢复绑定；流程：测试清理；无IO，不保存或访问已销毁状态。
    ~CombatHealthSession() { api::bindSession(nullptr); }
};

// 用途：建立有真实牌堆的健康度夹具；流程：NPC/妖兽斗法和真实API请求；返回：独立满血金丹角色。
Game mfCombatHealthGame(bool IsHuman = false) {
    CreateOptions Options;
    Options.seed = 1543;
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.pending.reset();
    State.job.reset();
    State.npcs.clear();
    auto& PlayerState = State.player;
    PlayerState.stage = 6;
    PlayerState.highestStage = 6;
    PlayerState.body = 0;
    PlayerState.talent.clear();
    PlayerState.traits.clear();
    PlayerState.arts.main.clear();
    PlayerState.arts.manuals.clear();
    PlayerState.arts.aux.clear();
    PlayerState.arts.nodes.clear();
    PlayerState.arts.gear.clear();
    PlayerState.arts.equipped.clear();
    PlayerState.arts.combatPrep.clear();
    PlayerState.arts.combatBuff = 0;
    PlayerState.arts.spells = {"wood"};
    PlayerState.arts.knownSpells = {"wood"};
    PlayerState.health = mfPlayerHealthMaximum(PlayerState);
    EnemySpec Enemy;
    Enemy.name = IsHuman ? "健康数据散修" : "健康数据妖兽";
    Enemy.kind = "spar"; // 同一非致命结算契约，避免把野外死亡惩罚误当延迟扣血。
    Enemy.stage = 6;
    Enemy.style = IsHuman ? "火" : "体";
    Enemy.beast = !IsHuman;
    Enemy.speed = 0;
    if (IsHuman) {
        Npc Opponent;
        Opponent.id = "combat-health-npc";
        Opponent.name = Enemy.name;
        Opponent.stage = 6;
        Opponent.health = 100;
        Opponent.body = 0;
        Opponent.dao = "火";
        Opponent.sect.clear();
        State.npcs.push_back(Opponent);
        Enemy.id = Opponent.id;
    }
    battleStart(State, Enemy);
    return State;
}

// 用途：将既有合法卡放入手牌；流程：回春、守势及胜利回归；返回：是否找到真实牌，不捏造牌ID。
bool mfHoldHealthCard(Battle& State, const string& SpellId) {
    const auto Card = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Item) { return Item.spell == SpellId; });
    if (Card == State.deck.end()) return false;
    State.hand = {Card->uid};
    State.draw.erase(std::remove(State.draw.begin(), State.draw.end(), Card->uid), State.draw.end());
    State.discard.erase(std::remove(State.discard.begin(), State.discard.end(), Card->uid), State.discard.end());
    return true;
}

// 用途：通过真实API提交战斗动作；流程：所有健康变化断言；返回：拥有的完整JSON响应，不连网络或玩家服务。
json::Value mfHealthTurn(const string& ActionId) {
    http::Request Request;
    Request.method = "POST";
    Request.path = "/api/battle/turn";
    json::Value Body(json::Object{});
    Body.set("id", ActionId);
    Request.body = Body.dump();
    return json::Value::parse(api::handle(Request).body);
}

// 用途：验证动作响应和独立战斗视图与即时引擎HP完全一致；流程：扣血、盾挡、DOT、治疗后共同断言。
// 返回：无，仅检查当前隔离角色；持久健康度不会被误当实时斗法气血。
void mfCheckLiveCombatHealth(const Game& State, const json::Value& Response) {
    check(Response["ok"].asBool(), "健康变化动作通过真实API成功提交");
    check(State.player.arts.battle.has_value(), "即时受伤或治疗后战斗仍在进行");
    if (!State.player.arts.battle) return;
    const auto& Combat = *State.player.arts.battle;
    const auto View = api::battleView(State);
    checkEq(Response["state"]["battle"]["hp"].asInt(), Combat.hp, "动作API返回实时斗法气血而非持久健康度");
    checkEq(Response["state"]["battle"]["max"].asInt(), Combat.max, "动作API返回当前斗法气血上限");
    checkEq(View["hp"].asInt(), Combat.hp, "独立只读战斗JSON与即时气血完全一致");
    checkEq(View["max"].asInt(), Combat.max, "独立只读战斗JSON上限与即时状态完全一致");
    checkEq(Response["state"]["player"]["health"].asInt(), State.player.health, "持久气血按点数保留至战后回写");
}
}

// 用途：验证即时斗法HP与结束健康百分比之间只归一一次；流程：真实API、NPC/妖兽、DOT、盾挡、回春、胜利/撤离。
// 返回：无，不修改生产规则，不读写玩家存档；API动作仅访问RAII隔离会话。
void mfRunCombatHealthTests() {
    std::printf("\n--- 实时斗法气血与健康归一回归 ---\n");
    {
        CombatHealthSession Scope;
        auto& State = Scope.SessionState.g;
        State = mfCombatHealthGame();
        Scope.SessionState.started = true;
        auto& Combat = *State.player.arts.battle;
        checkEq(Combat.hp, 724, "无被动金丹满气血开局为724");
        Combat.BeastActions.clear(); // 用冻结旧单意图隔离气血传导，不混入独立妖兽多段计划。
        Combat.intentKind = "attack";
        Combat.intentRaw = 60; // 冻结合法意图值，独立验证受伤与归一，不依赖意图随机数。
        const auto Hit = mfHealthTurn("end");
        checkEq(State.player.arts.battle->hp, 664, "妖兽真实回合立即扣除60斗法气血");
        checkEq(State.player.health, 724, "战中持久点数保留至实际战后回写");
        mfCheckLiveCombatHealth(State, Hit);
        State.player.arts.battle->enemyHp = 1;
        State.player.arts.battle->enemyShield = 0;
        check(mfHoldHealthCard(*State.player.arts.battle, "strike"), "胜利使用真实普通攻击卡");
        const auto Win = mfHealthTurn("strike");
        check(Win["ok"].asBool() && !State.player.arts.battle, "击杀后战斗正常结束");
        checkEq(State.player.health, 664, "结束直接写回664气血，没有二次扣血或百分比取整");
        checkEq(Win["state"]["player"]["health"].asInt(), 664, "胜利API下发真实剩余气血点数");
        check(Win["state"]["battle"].isNull(), "胜利API不保留过期战斗血量");
        const auto BeforeRepeat = saveToJson(State);
        check(!mfHealthTurn("strike")["ok"].asBool(), "结束后的重复动作被拒绝");
        check(saveToJson(State) == BeforeRepeat, "拒绝重复结算不再次扣血或改变其他游戏状态");
    }
    {
        CombatHealthSession Scope;
        auto& State = Scope.SessionState.g;
        State = mfCombatHealthGame(true);
        Scope.SessionState.started = true;
        auto& Combat = *State.player.arts.battle;
        Combat.enemyCards.planned.clear();
        for (const auto& Card : Combat.enemyCards.deck) if (Card.spell == "strike") {
            // 固定抽到的合法手牌，planned不能引用仍在抽牌堆中的卡；NPC实际施法仍完整经过引擎。
            Combat.enemyCards.hand = {Card.uid};
            Combat.enemyCards.draw.erase(std::remove(Combat.enemyCards.draw.begin(), Combat.enemyCards.draw.end(), Card.uid), Combat.enemyCards.draw.end());
            Combat.enemyCards.discard.erase(std::remove(Combat.enemyCards.discard.begin(), Combat.enemyCards.discard.end(), Card.uid), Combat.enemyCards.discard.end());
            Combat.enemyCards.planned.push_back(Card.uid);
            break;
        }
        check(Combat.enemyCards.enabled && !Combat.enemyCards.planned.empty(), "真实NPC使用自身牌堆的普通攻击");
        const int BeforeHealth = Combat.hp;
        const int PredictedDamage = battleEnemyPreview(State).damage;
        check(PredictedDamage > 0 && PredictedDamage < BeforeHealth, "NPC真实计划有非致命即时攻击伤害");
        const auto Hit = mfHealthTurn("end");
        checkEq(BeforeHealth - State.player.arts.battle->hp, PredictedDamage, "NPC完整回合即时扣血等于只读计划预览");
        mfCheckLiveCombatHealth(State, Hit);
    }
    {
        CombatHealthSession Scope;
        auto& State = Scope.SessionState.g;
        State = mfCombatHealthGame();
        Scope.SessionState.started = true;
        auto& Combat = *State.player.arts.battle;
        Combat.BeastActions.clear();
        Combat.intentKind = "guard";
        Combat.intentRaw = 0;
        Combat.playerPoison = 11;
        Combat.playerPoisonTurns = 1;
        Combat.enemyCards.burn = 17;
        Combat.enemyCards.burnTurns = 3;
        const auto Dot = mfHealthTurn("end");
        checkEq(State.player.arts.battle->hp, 696, "毒11和灼烧17在本轮立即合计扣28斗法气血");
        mfCheckLiveCombatHealth(State, Dot);
        check(mfHoldHealthCard(*State.player.arts.battle, "wood"), "回春使用真实已携带神通卡");
        const auto Heal = mfHealthTurn("wood");
        checkEq(State.player.arts.battle->hp, 724, "回春施放后即时回复缺失28气血且不超过上限");
        mfCheckLiveCombatHealth(State, Heal);
    }
    {
        CombatHealthSession Scope;
        auto& State = Scope.SessionState.g;
        State = mfCombatHealthGame();
        Scope.SessionState.started = true;
        check(mfHoldHealthCard(*State.player.arts.battle, "defend"), "守势使用真实固定防御卡");
        const auto Guard = mfHealthTurn("defend");
        checkEq(State.player.arts.battle->shield, 87, "守势即时增加87护盾");
        mfCheckLiveCombatHealth(State, Guard);
        State.player.arts.battle->BeastActions.clear();
        State.player.arts.battle->intentKind = "attack";
        State.player.arts.battle->intentRaw = 60;
        const auto Blocked = mfHealthTurn("end");
        checkEq(State.player.arts.battle->hp, 724, "护盾完全格挡60伤害后即时气血保持满值");
        mfCheckLiveCombatHealth(State, Blocked);
        const auto Escape = mfHealthTurn("escape");
        check(Escape["ok"].asBool() && !State.player.arts.battle && State.player.health == 724,
              "无气血损失的非致命撤离结束保持满气血");
    }
    {
        CombatHealthSession Scope;
        auto& State = Scope.SessionState.g;
        State = mfCombatHealthGame();
        Scope.SessionState.started = true;
        for (int RoundIndex = 0; RoundIndex < 2; ++RoundIndex) {
            State.player.arts.battle->BeastActions.clear();
            State.player.arts.battle->intentKind = "attack";
            State.player.arts.battle->intentRaw = 60;
            mfCheckLiveCombatHealth(State, mfHealthTurn("end"));
        }
        checkEq(State.player.arts.battle->hp, 604, "两个完整回合逐次即时扣血724减120为604");
        const auto Escape = mfHealthTurn("escape");
        check(Escape["ok"].asBool() && !State.player.arts.battle, "非致命撤离按当前斗法状态结束");
        checkEq(State.player.health, 604, "撤离直接回写604气血，不追加追击或惩罚");
        checkEq(Escape["state"]["player"]["health"].asInt(), 604, "撤离API气血与核心回写结果相同");
    }
}
} // namespace nztest
