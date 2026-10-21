#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/content/forge_content.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <memory>

namespace nztest {
namespace {
using namespace nz;

// 用途：准备真实体道NPC与满层主修玩家；流程：反震、仅回血及失败原子性；返回：堆上独立局，不用玩家文件。
std::unique_ptr<Game> mfVitalityEdgeEncounter() {
    CreateOptions Options; Options.seed = 20261019; Options.traitsExplicit = true;
    auto State = std::make_unique<Game>(createGame(Options));
    State->pending.reset(); State->job.reset(); State->npcs.clear();
    auto& Actor = State->player;
    Actor.stage = Actor.highestStage = 3; Actor.talent.clear(); Actor.traits.clear();
    Actor.arts.main = "wave"; Actor.arts.manuals = {{"wave", 5}};
    Actor.arts.aux.clear(); Actor.arts.nodes.clear(); Actor.arts.gear.clear(); Actor.arts.equipped.clear();
    Actor.arts.spells = {"waterSeal", "wavehit", "waterDraw", "water", "mind"};
    Actor.arts.knownSpells = Actor.arts.spells; Actor.arts.speed = 200;
    Actor.health = mfPlayerHealthMaximum(Actor);
    Npc Opponent; Opponent.id = "water-vitality-edge-body"; Opponent.name = "反震边界修士";
    Opponent.stage = 12; Opponent.dao = "体"; Opponent.health = 100; Opponent.lifespan = realmOf(12).life;
    State->npcs.push_back(Opponent);
    EnemySpec Target; Target.id = Opponent.id; Target.name = Opponent.name; Target.stage = 12;
    Target.style = "体"; Target.speed = 0;
    battleStart(*State, Target);
    auto& Field = *Actor.arts.battle;
    Field.max = 1000; Field.hp = 500; Field.enemyMax = Field.enemyHp = 100000;
    Field.enemyShield = Field.shield = 0; Field.mana = 12;
    Field.poison = Field.poisonTurns = Field.burn = Field.burnTurns = 0;
    Field.enemyCards.guard = Field.enemyCards.reduction = 0;
    return State;
}

// 用途：把已有真实卡安排到手中；流程：边界真实出牌；返回：原牌唯一UID，四牌堆无重复。
string mfVitalityEdgeHand(Battle& Field, const string& SpellId) {
    Field.hand.clear(); Field.draw.clear(); Field.discard.clear(); Field.exhausted.clear();
    for (const auto& Card : Field.deck) {
        if (Field.hand.empty() && Card.spell == SpellId) Field.hand.push_back(Card.uid);
        else Field.draw.push_back(Card.uid);
    }
    return Field.hand.empty() ? "" : Field.hand.front();
}

// 用途：给当前玩家装备合法V2固定伤害法器；流程：主动回春/先验拒绝；返回：真实常驻主动ID。
string mfVitalityEdgeArtifact(Player& Actor, const string& Inscription = "") {
    const auto* Definition = content::mfFindForgeEffect("damage");
    Gear Item; Item.id = "water-vitality-edge-artifact"; Item.name = "水势边界匣"; Item.grade = 2;
    Item.slot = "weapon"; Item.shape = "box";
    Item.element = Definition->Elements.empty() ? "气" : Definition->Elements.front();
    Item.polarity = "天阳"; Item.crafted = true; Item.ForgeVersion = 2; Item.EffectId = "damage";
    Item.EffectPower = 1; Item.SourceMaterials = {{"iron", 10}}; Item.ForgeEnergy = 10;
    Item.dimensions = {100, 100, 100, 100, 100}; Item.InscriptionId = Inscription;
    Actor.arts.gear = {Item}; Actor.arts.equipped["weapon"] = Item.id;
    return "artifact:" + Item.id;
}
}

// 用途：覆盖真实调用点不能由纯helper证明的边界；流程：独立/统一harness；返回：测试失败计数。
void mfRunWaterVitalityEdgeTests() {
    using namespace nz;
    std::printf("\n--- 水功法回春反震、斩杀及原子性边界 ---\n");
    for (const bool Artifact : {false, true}) {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        check(Field.enemyCards.enabled && !Field.shadowPlayer, "反震夹具为真实普通NPC而非留影");
        check(std::find(Field.enemyCards.nodes.begin(), Field.enemyCards.nodes.end(), "6-5") != Field.enemyCards.nodes.end(),
            "真实体修已取得体五反震");
        const auto Id = Artifact ? mfVitalityEdgeArtifact(State->player) : mfVitalityEdgeHand(Field, "waterSeal");
        Field.hp = 1; Field.enemyShield = 10;
        const auto Forecast = battlePreview(State->player, Field, Artifact ? Id : "waterSeal");
        check(Forecast.damage > 0, "主段部分盾挡后仍造成实际HP损失");
        check(Forecast.heal < 0 && Forecast.healPotential == 0, "主段反震致死的预览不生成回血潜力");
        const int WinsBefore = State->player.arts.progress.wins;
        check(battleTurn(*State, Id).ok, "普通神通/V2真实部分盾挡反震动作已提交");
        check(!battleActive(*State), "原反震致死不会被随后满层回春救活");
        checkEq(State->player.arts.progress.wins, WinsBefore, "反震致死不误记作玩家斩杀获胜");
    }
    {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        const auto Id = mfVitalityEdgeArtifact(State->player); Field.hp = Field.max;
        auto NoMastery = State->player; NoMastery.arts.manuals["wave"] = 4;
        const auto Base = battlePreview(NoMastery, Field, Id);
        const auto Before = saveToJson(*State);
        const auto Forecast = battlePreview(State->player, Field, Id);
        const int Potential = Base.damage / 2;
        checkStr(saveToJson(*State), Before, "V2满血回春反复报价保持完整状态只读");
        checkEq(Forecast.RawDamage, Base.RawDamage, "V2满血Raw只保留原法器伤害");
        checkEq(Forecast.damage, Base.damage, "V2满血不追加伤害");
        checkEq(Forecast.healPotential, Potential, "V2满血仍显示本次治疗潜力");
        checkEq(Forecast.heal, 0, "V2满血实际回复零");
        const int TargetBefore = Field.enemyHp, PlayedBefore = Field.played;
        check(battleTurn(*State, Id).ok, "V2满血仍可正常催动原主动攻击");
        checkEq(TargetBefore - Field.enemyHp, Forecast.damage, "V2满血真实总损血与预览一致");
        checkEq(Field.played, PlayedBefore + 1, "V2回春不额外消耗第二次动作");
        checkEq(Field.cooldowns.at(Id), 2, "V2回春不额外重设或延长主动冷却");
    }
    {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        const auto Uid = mfVitalityEdgeHand(Field, "mind");
        Field.hp = Field.max; Field.enemyShield = 10000;
        auto NoMastery = State->player; NoMastery.arts.manuals["wave"] = 4;
        const auto Base = battlePreview(NoMastery, Field, "mind");
        const auto Forecast = battlePreview(State->player, Field, "mind");
        const int Potential = Base.damage / 2;
        check(Potential > 0, "穿盾主段产生可观测的治疗潜力");
        checkEq(Forecast.damage, Base.damage, "穿盾主段命中后回春不再追加伤害");
        checkEq(Forecast.RawDamage, Base.RawDamage, "满血预览Raw只含原主动伤害");
        checkEq(Forecast.healPotential, Potential, "满血时仍显示实际首击50%的治疗潜力");
        checkEq(Forecast.heal, 0, "满血穿盾命中不再额外反震或回血");
        const int TargetBefore = Field.enemyHp, ShieldBefore = Field.enemyShield;
        check(battleTurn(*State, Uid).ok, "穿盾后剩余护盾的真实动作完成");
        checkEq(TargetBefore - Field.enemyHp, Forecast.damage, "只有原穿盾命中真实损血符合预览");
        checkEq(Field.enemyShield, ShieldBefore, "回血机制不再额外消耗目标剩余护盾");
        checkEq(Field.hp, Field.max, "体五不会因回春触发额外反震");
    }
    {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        const auto Uid = mfVitalityEdgeHand(Field, "waterSeal");
        Field.max = mfPlayerHealthMaximum(State->player); Field.hp = Field.max / 2; Field.enemyHp = 5;
        auto NoMastery = State->player; NoMastery.arts.manuals["wave"] = 4;
        const auto Base = battlePreview(NoMastery, Field, "waterSeal");
        const auto Forecast = battlePreview(State->player, Field, "waterSeal");
        check(Base.damage > 5, "主段超杀夹具确实超过剩余5HP");
        checkEq(Forecast.heal, 2, "真实超杀首击只按原有5HP取floor50%治疗");
        checkEq(Forecast.RawDamage, Base.RawDamage, "原段斩杀目标时不再发出额外原伤");
        const int OwnBefore = Field.hp, WinsBefore = State->player.arts.progress.wins;
        check(battleTurn(*State, Uid).ok && !battleActive(*State), "真实原段斩杀完成结算");
        checkEq(State->player.health, OwnBefore + 2, "普通胜利结算保留按实际存量计算的回血");
        checkEq(State->player.arts.progress.wins, WinsBefore + 1, "原段斩杀只记录一次胜利");
    }
    {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        const auto Uid = mfVitalityEdgeHand(Field, "waterSeal"); Field.hp = Field.max;
        auto NoMastery = State->player; NoMastery.arts.manuals["wave"] = 4;
        const auto Base = battlePreview(NoMastery, Field, "waterSeal");
        Field.enemyHp = Base.damage + 1;
        const auto Forecast = battlePreview(State->player, Field, "waterSeal");
        checkEq(Forecast.damage, Base.damage, "原段留1HP时满血回春不再追加斩杀");
        checkEq(Forecast.RawDamage, Base.RawDamage, "满血回春不改变原段Raw伤害");
        const int WinsBefore = State->player.arts.progress.wins;
        check(battleTurn(*State, Uid).ok && battleActive(*State), "原段留1HP时对局继续，不因回春获胜");
        checkEq(Field.enemyHp, 1, "真实执行保留原攻击剩余的1HP");
        checkEq(State->player.arts.progress.wins, WinsBefore, "满血回春不凭空登记胜利");
    }
    for (const string Scenario : {"mana", "cooldown", "health"}) {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        const auto Id = mfVitalityEdgeArtifact(State->player, Scenario == "health" ? "white" : "");
        if (Scenario == "mana") Field.mana = 0;
        if (Scenario == "cooldown") Field.cooldowns[Id] = 1;
        if (Scenario == "health") Field.hp = jsRound(Field.max * .04);
        const auto Before = saveToJson(*State);
        static_cast<void>(battlePreview(State->player, Field, Id));
        checkStr(saveToJson(*State), Before, "V2先验拒绝预览不写状态：" + Scenario);
        check(!battleTurn(*State, Id).ok, "V2先验拒绝动作：" + Scenario);
        checkStr(saveToJson(*State), Before, "V2拒绝不部分扣费、回血、伤敌或消耗RNG：" + Scenario);
    }
    {
        auto State = mfVitalityEdgeEncounter(); auto& Field = *State->player.arts.battle;
        Field.poison = 10; Field.poisonTurns = 1; Field.poisonDamage = 20; Field.enemyCards.planned.clear();
        const int OwnBefore = Field.hp;
        check(battleTurn(*State, "end").ok && battleActive(*State), "纯持续伤害结束回合可正常推进");
        check(std::any_of(Field.log.begin(), Field.log.end(), [](const string& Text) {
            return Text.find("持续伤害20") != string::npos;
        }), "真实回合中已实际结算20毒伤");
        checkEq(Field.hp, OwnBefore, "无主动命中的毒伤不会触发主修回春");
    }
    for (const int Stage : {3, 9, 12}) {
        Npc WaterNpc; WaterNpc.id = "water-vitality-loadout"; WaterNpc.dao = "水"; WaterNpc.sect = "xinghe"; WaterNpc.stage = Stage;
        const auto Loadout = artNpcLoadout(WaterNpc);
        checkStr(Loadout.main, "wave", "普通星河主修稳定ID与玩家同路：" + std::to_string(Stage));
        const auto Level = Loadout.manuals.at("wave");
        check(Level >= 1 && Level <= 5, "普通星河功法仍遵守境界层数预算");
        if (Stage >= 9) {
            checkEq(Level, 5, "高境普通星河自然达到主修五层");
            check(artModsOf(Loadout).waterKeep >= .5, "主修满层回春没有删除原悟道五层留水");
        }
    }
}
} // namespace nztest

#ifdef NZ_WATER_VITALITY_EDGE_STANDALONE
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        nztest::mfRunWaterVitalityEdgeTests();
        return nztest::failures() == 0 ? 0 : 1;
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "水回春边界夹具异常：%s\n", Error.what());
        return 2;
    }
}
#endif
