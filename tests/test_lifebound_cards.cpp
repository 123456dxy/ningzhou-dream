#include "harness.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/hand_rules.h"
#include "core/save.h"
#include <algorithm>
#include <set>

namespace nztest {
namespace {
using namespace nz;

Player mfLifeboundActor(const string& Kind) {
    Player Actor;
    Actor.name = "本命规则回归";
    Actor.stage = Actor.highestStage = 3;
    Actor.lifespan = 200;
    Actor.talent.clear(); Actor.traits.clear(); Actor.body = 0;
    Actor.arts.main.clear(); Actor.arts.manuals.clear();
    Actor.arts.spells = {"gold", "purge"};
    Actor.arts.knownSpells = Actor.arts.spells;
    Actor.arts.speed = 200;
    Actor.arts.LifeboundSpell = "gold";
    Actor.arts.LifeboundKind = Kind;
    Actor.health = mfPlayerHealthMaximum(Actor);
    return Actor;
}

Game mfLifeboundBattle(const string& Kind) {
    Game State;
    State.seed = State.rng = 16091;
    State.player = mfLifeboundActor(Kind);
    EnemySpec Enemy;
    Enemy.name = "本命规则对手"; Enemy.stage = 0;
    Enemy.kind = "spar"; Enemy.beast = true; Enemy.speed = 0;
    battleStart(State, Enemy);
    auto& Active = *State.player.arts.battle;
    Active.enemyHp = Active.enemyMax = 1000000;
    Active.BeastActions.clear(); Active.intentKind = "guard"; Active.intentRaw = 0;
    return State;
}

string mfFirstLifeboundUid(const vector<Card>& Deck, const string& SpellId = "gold") {
    const auto Found = std::find_if(Deck.begin(), Deck.end(), [&](const auto& CardState) {
        return CardState.spell == SpellId;
    });
    return Found == Deck.end() ? "" : Found->uid;
}

void mfLifeboundHand(Battle& Active, const vector<string>& Hand) {
    Active.hand = Hand; Active.draw.clear(); Active.discard.clear(); Active.exhausted.clear();
    for (const auto& CardState : Active.deck)
        if (std::find(Hand.begin(), Hand.end(), CardState.uid) == Hand.end()) Active.draw.push_back(CardState.uid);
    Active.mana = 30; Active.cooldowns.clear();
}
} // namespace

// 用途：经正式开局与回合动作验证本命仅强化一个实例，普通规则与双方镜像保持一致。
void mfRunLifeboundCardTests() {
    using namespace nz;
    {
        Game State = mfLifeboundBattle("innate");
        const auto& Active = *State.player.arts.battle;
        checkEq(std::count_if(Active.deck.begin(), Active.deck.end(), [](const auto& CardState) {
            return CardState.Innate;
        }), 1, "同神通两张副本中仅首张附加固有");
        const string First = mfFirstLifeboundUid(Active.deck);
        check(std::find(Active.hand.begin(), Active.hand.end(), First) != Active.hand.end(),
            "指定首张固有卡保证进入真实起手");
        checkEq(Active.hand.size(), 5, "固有占正常五张起手名额，不额外发牌");
    }
    {
        Game State = mfLifeboundBattle("retain");
        auto& Active = *State.player.arts.battle;
        const string First = mfFirstLifeboundUid(Active.deck);
        checkEq(std::count_if(Active.deck.begin(), Active.deck.end(), [](const auto& CardState) {
            return CardState.Retain;
        }), 1, "同神通两张副本中仅首张附加保留");
        mfLifeboundHand(Active, {First});
        const string Before = saveToJson(State);
        battleEnemyPreview(State); battleEnemyPreview(State);
        checkStr(saveToJson(State), Before, "保留相关意图预演不改变真实牌堆和RNG");
        check(battleTurn(State, "end").ok, "保留卡通过真实完整回合推进");
        check(std::find(Active.hand.begin(), Active.hand.end(), First) != Active.hand.end(),
            "未打出的保留实例跨回合留在手牌");
        battleDraw(State, 100);
        checkEq(Active.hand.size(), BattleCards::MaximumHandSize, "保留实例占手牌容量，后续抽牌仍封顶十二张");
        checkEq(std::count(Active.hand.begin(), Active.hand.end(), First), 1, "跨轮和补牌不复制保留实例");
    }
    {
        Game State = mfLifeboundBattle("retain");
        auto& Active = *State.player.arts.battle;
        const string First = mfFirstLifeboundUid(Active.deck);
        mfLifeboundHand(Active, {First});
        check(battleTurn(State, First).ok, "保留卡可以正常施放");
        check(battleTurn(State, "end").ok, "打出保留卡后正常推进下一轮");
        check(std::find(Active.hand.begin(), Active.hand.end(), First) == Active.hand.end() &&
            std::find(Active.discard.begin(), Active.discard.end(), First) != Active.discard.end(),
            "已打出的保留卡留在正常弃牌堆，不自动返手");
    }
    {
        Game State = mfLifeboundBattle("retain");
        auto& Active = *State.player.arts.battle;
        const string First = mfFirstLifeboundUid(Active.deck);
        const string Purge = mfFirstLifeboundUid(Active.deck, "purge");
        mfLifeboundHand(Active, {First, Purge});
        check(battleTurn(State, Purge, First).ok, "忘机咒仍可按唯一UID移除保留实例");
        check(std::find(Active.exhausted.begin(), Active.exhausted.end(), First) != Active.exhausted.end(),
            "本命保留不能绕过本场移除规则");
    }
    {
        Game State = mfLifeboundBattle("retain");
        State.player.arts.main = "gold";
        State.player.arts.manuals = {{"gold", 5}, {"earth", 5}, {"mountain", 5}};
        State.player.arts.aux = {"earth", "mountain"};
        auto& Active = *State.player.arts.battle;
        const string First = mfFirstLifeboundUid(Active.deck);
        mfLifeboundHand(Active, {First, mfFirstLifeboundUid(Active.deck, "defend")});
        Active.intentKind = "attack"; Active.intentRaw = 100; Active.shield = 0;
        Game Ordinary = State;
        Ordinary.player.arts.LifeboundKind.clear(); Ordinary.player.arts.LifeboundSpell.clear();
        for (auto& CardState : Ordinary.player.arts.battle->deck) CardState.Retain = false;
        check(battleEnemyPreview(State).damage > battleEnemyPreview(Ordinary).damage,
            "保留实例不计入真实弃牌凝盾，预览仅按实际弃牌张数防护");
    }
    {
        Game State;
        Player Challenger = mfLifeboundActor("retain");
        Player Opponent = mfLifeboundActor("innate");
        Opponent.name = "本命影子对手"; Opponent.arts.speed = 0;
        battleStartShadow(State, Challenger, Opponent, 16091);
        const auto& Active = *State.player.arts.battle;
        const auto& Enemy = Active.enemyCards;
        const string First = mfFirstLifeboundUid(Enemy.deck);
        checkEq(std::count_if(Enemy.deck.begin(), Enemy.deck.end(), [](const auto& CardState) {
            return CardState.Innate;
        }), 1, "影子对手同样只有首张本命实例获得固有");
        check(std::find(Enemy.hand.begin(), Enemy.hand.end(), First) != Enemy.hand.end(),
            "影子对手的固有同样占正常起手名额");
        checkEq(Enemy.hand.size(), 5, "影子固有不额外发起手牌");
    }
    {
        Game State;
        Player Challenger = mfLifeboundActor("innate");
        Player Opponent = mfLifeboundActor("retain");
        Opponent.name = "保留影子对手"; Opponent.arts.speed = 0;
        battleStartShadow(State, Challenger, Opponent, 16092);
        auto& Active = *State.player.arts.battle;
        auto& Enemy = Active.enemyCards;
        const string First = mfFirstLifeboundUid(Enemy.deck);
        Enemy.hand = {First}; Enemy.draw.clear(); Enemy.discard.clear(); Enemy.exhausted.clear();
        for (const auto& CardState : Enemy.deck)
            if (CardState.uid != First) Enemy.draw.push_back(CardState.uid);
        Enemy.planned.clear(); Enemy.mana = 0;
        check(battleTurn(State, "end").ok, "影子保留经双方真实完整回合推进");
        check(std::find(Enemy.hand.begin(), Enemy.hand.end(), First) != Enemy.hand.end(),
            "影子未施放的保留实例跨回合留手");
    }
}
} // namespace nztest
