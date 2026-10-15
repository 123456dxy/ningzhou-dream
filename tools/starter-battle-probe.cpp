// 独立诊断：真实建角与山道劫修流程；仅写命令行指定的诊断报告，不读取玩家存档或服务。
#include "core/adventure.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/json.h"
#include "core/sim.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace nz;
using Json = json::Value;
constexpr int SeedCount = 30;
constexpr int MaximumActions = 500;
constexpr int MaximumEndTurns = 28;

// 类型：无外部资源的诊断决策快照；只保存当前可见手牌候选，不持有游戏引用。
struct Decision {
    string Uid, SpellId, Target;
    double Score = -1;
    Preview Forecast;
};

// 用途：序列化实际字符串序列；流程：报告快照；用法：传稳定 ID 列表；返回：JSON 数组。
Json mfStrings(const vector<string>& Values) {
    json::Array Output;
    for (const auto& Value : Values) Output.emplace_back(Value);
    return Json(std::move(Output));
}

// 用途：记录真实角色建角属性与配装；流程：开局证据；用法：传 createGame 结果；返回：只读 JSON 快照。
Json mfStarterSnapshot(const Game& GameState) {
    const auto& Actor = GameState.player;
    Json Output(json::Object{});
    Output.set("stage", Actor.stage); Output.set("health", Actor.health);
    Output.set("apt", Actor.apt); Output.set("body", Actor.body);
    Output.set("insight", Actor.insight); Output.set("stones", Actor.stones);
    Output.set("traits", mfStrings(Actor.traits)); Output.set("carried", mfStrings(Actor.arts.spells));
    Output.set("known", mfStrings(Actor.arts.knownSpells)); Output.set("main", Actor.arts.main);
    Output.set("aux", mfStrings(Actor.arts.aux)); Output.set("nodeCount", static_cast<int>(Actor.arts.nodes.size()));
    Json Manuals(json::Object{});
    for (const auto& [Id, Level] : Actor.arts.manuals) Manuals.set(Id, Level);
    Output.set("manuals", std::move(Manuals));
    return Output;
}

// 用途：保留真实 current，其他历史候选先显式复原旧御剑基线；流程：诊断变体；用法：传事件前副本及变体；返回：无，不改真实源角色。
void mfApplyVariant(Game& GameState, const string& Variant) {
    if (Variant == "current") return;
    auto& ArtsState = GameState.player.arts;
    // 诊断夹具专用：新默认变化后，历史候选仍从当时的四张牌和六种已知神通复现。
    ArtsState.spells = {"sword", "water", "wood", "skim"};
    ArtsState.knownSpells = {"sword", "water", "wood", "skim", "refresh", "purge"};
    const auto Position = std::find(ArtsState.spells.begin(), ArtsState.spells.end(), "sword");
    if (Variant == "gold" || Variant == "body") *Position = Variant;
    else if (Variant == "goldGather") ArtsState.spells = {"gold", "wood", "skim", "gather"};
    else if (Variant == "goldCover") ArtsState.spells = {"gold", "goldcover", "wood", "skim"};
    else if (Variant == "sealCycle") ArtsState.spells = {"woodSeal", "fireSeal", "water", "gather"};
    else if (Variant == "goldSeals") ArtsState.spells = {"gold", "woodSeal", "fireSeal", "wood"};
    else if (Variant == "goldFire") ArtsState.spells = {"gold", "fire", "water", "gather"};
    else throw std::runtime_error("未知开局变体");
    for (const auto& SpellId : ArtsState.spells) {
        const auto* Spell = battleSpell(SpellId);
        if (!Spell || Spell->stage > GameState.player.stage) throw std::runtime_error("候选有非法初装神通");
        if (std::find(ArtsState.knownSpells.begin(), ArtsState.knownSpells.end(), SpellId) == ArtsState.knownSpells.end())
            ArtsState.knownSpells.push_back(SpellId);
    }
}

// 用途：按正式移除规则提供基础牌实例；流程：自动策略目标校验；用法：传当前可见堆及定义；返回：合法单 UID，无目标为空。
string mfRemoveTarget(const Battle& State, const SpellDef& Spell) {
    if (!battleNeedsTarget(Spell)) return "";
    for (const string Wanted : {"defend", "strike"})
        for (const auto* Pile : {&State.hand, &State.draw, &State.discard}) for (const auto& Uid : *Pile) {
            const auto Position = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Card) { return Card.uid == Uid; });
            if (Position != State.deck.end() && Position->spell == Wanted) return Uid;
        }
    return "";
}

// 用途：以可见预览产生简单启发式评分；流程：VisibleGreedyV1 策略；用法：传当前手牌预览与公开意图；返回：效用/费用，不看未来抽牌。
double mfVisibleScore(const Battle& State, const Preview& Forecast, const EnemyPreview& Incoming) {
    if (Forecast.damage >= State.enemyHp && Forecast.damage > 0) return 1000000 + Forecast.damage;
    // RawDamage 已是后端公开的施术量，盾上的伤害同样推进战局；不把破盾攻击误判为零效用。
    const double DamageUtility = Forecast.RawDamage * 1.25;
    const double HealUtility = std::max(0, Forecast.heal) * .8;
    const double ShieldUtility = std::min(std::max(0, Forecast.shield), Incoming.damage) * .65;
    const bool HasManaForDrawnCard = State.mana > Forecast.cost;
    const double DrawUtility = State.hand.size() <= 6 && HasManaForDrawnCard ? Forecast.draw * 8.0 : 0.0;
    const double ManaUtility = Forecast.manaGain * 9.0;
    return (DamageUtility + HealUtility + ShieldUtility + DrawUtility + ManaUtility) / std::max(1, Forecast.cost);
}

// 用途：给本轮可见局面评分而非追踪未知抽牌；流程：两步策略叶节点；用法：传原始战况和隔离施法后状态；返回：伤害、破盾、生存及余灵效用。
double mfPositionValue(const Battle& Origin, const Game& Projection, int WinsBefore, bool LowHealthPriority) {
    if (!battleActive(Projection)) return Projection.player.arts.progress.wins > WinsBefore ? 1000000 : -1000000;
    const auto& State = *Projection.player.arts.battle;
    const auto Incoming = battleEnemyPreview(Projection);
    const int HpDamage = std::max(0, Origin.enemyHp - State.enemyHp);
    const int ShieldDamage = std::max(0, Origin.enemyShield - State.enemyShield);
    const int HealthGain = State.hp - Origin.hp;
    const double LethalPenalty = Incoming.damage >= State.hp ? 10000 : 0;
    if (LowHealthPriority) {
        const double LowHealthThreshold = Origin.max * .55;
        const double ProjectedHealth = std::max(0, State.hp - Incoming.damage);
        // 高血量允许换伤抢进度；低于55%后的血量价值更高，避免线性评分导致满血也守到超时。
        const double HealthUtility = std::min(ProjectedHealth, LowHealthThreshold) * 1.5 +
            std::max(0.0, ProjectedHealth - LowHealthThreshold) * .15;
        return HpDamage * 1.2 + ShieldDamage * .8 + HealthUtility + State.mana * 4.0 - LethalPenalty;
    }
    return HpDamage * 1.2 + ShieldDamage * .65 + HealthGain - Incoming.damage * 1.15 + State.mana * 4.0 - LethalPenalty;
}

// 用途：只枚举当前已知手牌的两步实战动作；流程：命名两步策略决策；用法：传隔离事件副本与低血优先开关；返回：首张 UID 与评分。
Decision mfVisibleTwoStep(const Game& Projection, const Battle& Origin, int WinsBefore, int RemainingDepth, bool LowHealthPriority) {
    Decision Best;
    if (!battleActive(Projection) || RemainingDepth <= 0) return Best;
    const auto& State = *Projection.player.arts.battle;
    const double EndValue = mfPositionValue(Origin, Projection, WinsBefore, LowHealthPriority);
    Best.Score = EndValue;
    for (const auto& Uid : State.hand) {
        const auto Card = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
        const auto* Spell = Card == State.deck.end() ? nullptr : battleSpell(Card->spell);
        if (!Spell || Spell->stage > Projection.player.stage) continue;
        const auto Cooldown = State.cooldowns.find(Spell->id);
        if (Cooldown != State.cooldowns.end() && Cooldown->second > 0) continue;
        const auto Forecast = battlePreview(Projection.player, State, Spell->id);
        if (!Forecast.valid || State.mana < Forecast.cost) continue;
        const string Target = mfRemoveTarget(State, *Spell);
        if (battleNeedsTarget(*Spell) && Target.empty()) continue;
        double Score = EndValue;
        if (Forecast.draw > 0) {
            // 不执行抽牌预演，也不读 draw/RNG；只按公开张数估值。最后一费抽牌不自动占优。
            if (State.mana > Forecast.cost && State.hand.size() <= 5)
                Score += Forecast.draw * 8.0 - Forecast.cost * 5.0;
        } else {
            Game Child = Projection;
            const auto Applied = battleTurn(Child, Uid, Target);
            if (!Applied.ok) throw std::runtime_error("两步预演使用非法动作: " + Applied.error);
            Score = mfPositionValue(Origin, Child, WinsBefore, LowHealthPriority);
            if (battleActive(Child) && RemainingDepth > 1)
                Score = std::max(Score, mfVisibleTwoStep(Child, Origin, WinsBefore, RemainingDepth - 1, LowHealthPriority).Score);
        }
        if (Score > Best.Score) Best = {Uid, Spell->id, Target, Score, Forecast};
    }
    return Best;
}

// 用途：仅从真实可见手牌选择下一动作；流程：命名固定策略；用法：传报告所列策略 ID；返回：合法 UID，无正分则结束回合。
Decision mfChoose(const Game& GameState, const string& Policy) {
    const auto& Actor = GameState.player;
    const auto& State = *Actor.arts.battle;
    if (Policy == "VisibleTwoStepV1" || Policy == "VisibleTwoStepV2") {
        if (!State.npcId.empty()) throw std::runtime_error("两步策略只用于无具名 NPC 的山道劫修");
        Game Projection = GameState;
        // 实际敌方功法已冻结在 Battle；空 npcId 不引用世界角色。删去无关世界快照，避免搜索复制720名NPC。
        Projection.npcs.clear(); Projection.logs.clear(); Projection.checkpoint.snapshot.clear();
        return mfVisibleTwoStep(Projection, State, Actor.arts.progress.wins, 2, Policy == "VisibleTwoStepV2");
    }
    const EnemyPreview Incoming = battleEnemyPreview(GameState);
    Decision Best;
    for (const auto& Uid : State.hand) {
        const auto Card = std::find_if(State.deck.begin(), State.deck.end(), [&](const auto& Entry) { return Entry.uid == Uid; });
        const auto* Spell = Card == State.deck.end() ? nullptr : battleSpell(Card->spell);
        if (!Spell || Actor.stage < Spell->stage) continue;
        const auto Cooldown = State.cooldowns.find(Spell->id);
        if (Cooldown != State.cooldowns.end() && Cooldown->second > 0) continue;
        const Preview Forecast = battlePreview(Actor, State, Spell->id);
        if (!Forecast.valid || State.mana < Forecast.cost) continue;
        const string Target = mfRemoveTarget(State, *Spell);
        if (battleNeedsTarget(*Spell) && Target.empty()) continue;
        const double Score = Policy == "NpcPriorityV1" ? mfBattleNpcCardPriority(Actor, State, *Spell)
                                                       : mfVisibleScore(State, Forecast, Incoming);
        if (Score > 0 && Score > Best.Score) Best = {Uid, Spell->id, Target, Score, Forecast};
    }
    return Best;
}

// 用途：保存双方真实开战牌堆及先手；流程：对手规格证据；用法：传已通过 adventureChoose 开战的状态；返回：JSON，不复制战斗公式。
Json mfOpeningSnapshot(const Game& GameState) {
    const auto& State = *GameState.player.arts.battle;
    Json Output(json::Object{});
    Output.set("playerHp", State.hp); Output.set("playerHpMax", State.max);
    Output.set("playerMana", State.mana); Output.set("playerFirst", State.playerFirst);
    Output.set("playerSpeed", State.playerSpeed); Output.set("enemySpeed", State.enemySpeed);
    Output.set("enemyHp", State.enemyHp); Output.set("enemyHpMax", State.enemyMax);
    Output.set("enemyStage", State.enemyStage); Output.set("enemyKind", State.kind);
    Output.set("enemyMain", State.enemyCards.main); Output.set("enemySpells", mfStrings(State.enemySpells));
    Output.set("lethalFlag", State.lethal); Output.set("escapeChance", battleEscapeChance(GameState.player, State));
    Output.set("enemyActionCap", mfBattleEnemyActionLimit(GameState, State));
    Output.set("deckCardCount", static_cast<int>(State.deck.size()));
    Output.set("openingHand", mfStrings(handSpells(State)));
    const auto Incoming = battleEnemyPreview(GameState);
    Output.set("openingIncomingDamage", Incoming.damage); Output.set("openingIncomingShield", Incoming.shield);
    json::Array CardPreviews;
    for (const string SpellId : {"strike", "defend", "sword", "gold", "body", "water", "wood", "skim", "gather", "goldcover"}) {
        const auto Forecast = battlePreview(GameState.player, State, SpellId);
        Json Item(json::Object{}); Item.set("id", SpellId); Item.set("cost", Forecast.cost);
        Item.set("rawDamage", Forecast.RawDamage); Item.set("damage", Forecast.damage);
        Item.set("shield", Forecast.shield); Item.set("heal", Forecast.heal);
        Item.set("healPotential", Forecast.healPotential); Item.set("manaGain", Forecast.manaGain);
        Item.set("draw", Forecast.draw); CardPreviews.push_back(std::move(Item));
    }
    Output.set("cardPreviews", Json(std::move(CardPreviews)));
    return Output;
}

// 用途：沿真实事件选择与出牌入口完成一局；流程：配对样本；用法：仅事件境界覆盖为规则允许的 0/1 档；返回：完整动作证据和明确结局。
Json mfPlay(Game Scenario, const string& Variant, const string& Policy, int EnemyStage, unsigned Seed) {
    mfApplyVariant(Scenario, Variant);
    if (!Scenario.pending || !Scenario.pending->adventure) throw std::runtime_error("缺少真实奇遇上下文");
    const int NaturalStage = Scenario.pending->adventure->stage;
    Scenario.pending->adventure->stage = EnemyStage;
    Json Output(json::Object{});
    Output.set("seed", static_cast<double>(Seed)); Output.set("variant", Variant); Output.set("policy", Policy);
    Output.set("naturalEnemyStage", NaturalStage); Output.set("diagnosticThreatStage", EnemyStage);
    Output.set("threatOverride", "only pending.adventure.stage = legal boundary 0 or 1; EnemySpec from adventureChoose");
    Output.set("starter", mfStarterSnapshot(Scenario));
    const int WinsBefore = Scenario.player.arts.progress.wins;
    if (!adventureChoose(Scenario, "fight") || !battleActive(Scenario)) throw std::runtime_error("真实山道劫修开战失败");
    Output.set("opening", mfOpeningSnapshot(Scenario));
    json::Array Trace;
    int ActionCount = 0, EndCount = 0;
    while (battleActive(Scenario) && ActionCount < MaximumActions && EndCount < MaximumEndTurns) {
        const auto& State = *Scenario.player.arts.battle;
        const Decision Choice = mfChoose(Scenario, Policy);
        Json Action(json::Object{});
        Action.set("turn", State.turn); Action.set("rngBefore", static_cast<double>(Scenario.rng));
        Action.set("hpBefore", State.hp); Action.set("enemyHpBefore", State.enemyHp);
        Action.set("manaBefore", State.mana); Action.set("hand", mfStrings(handSpells(State)));
        Action.set("shieldBefore", State.shield); Action.set("enemyShieldBefore", State.enemyShield);
        const string Operation = Choice.Uid.empty() ? "end" : Choice.Uid;
        Action.set("action", Choice.Uid.empty() ? "end" : Choice.SpellId);
        Action.set("uid", Choice.Uid); Action.set("score", Choice.Score);
        if (!Choice.Uid.empty()) {
            Action.set("rawDamage", Choice.Forecast.RawDamage); Action.set("damage", Choice.Forecast.damage);
            Action.set("shield", Choice.Forecast.shield); Action.set("heal", Choice.Forecast.heal);
            Action.set("cost", Choice.Forecast.cost); Action.set("draw", Choice.Forecast.draw);
        } else {
            Action.set("incomingDamage", battleEnemyPreview(Scenario).damage); ++EndCount;
            Action.set("poisonTickPotential", State.poisonTurns > 0 ? State.poisonDamage : 0);
            Action.set("burnTickPotential", State.burnTurns > 0 ? State.burn : 0);
        }
        const TurnResult Turn = battleTurn(Scenario, Operation, Choice.Target);
        ++ActionCount;
        if (!Turn.ok) throw std::runtime_error("策略非法动作: " + Turn.error);
        Action.set("rngAfter", static_cast<double>(Scenario.rng));
        if (Scenario.player.arts.battle) {
            Action.set("hpAfter", Scenario.player.arts.battle->hp);
            Action.set("enemyHpAfter", Scenario.player.arts.battle->enemyHp);
            Action.set("shieldAfter", Scenario.player.arts.battle->shield);
            Action.set("enemyShieldAfter", Scenario.player.arts.battle->enemyShield);
        }
        Trace.push_back(std::move(Action));
    }
    if (battleActive(Scenario)) throw std::runtime_error("诊断动作上限提前中断，不能计入胜负");
    const bool Won = Scenario.player.arts.progress.wins > WinsBefore;
    Output.set("outcome", Won ? "win" : "loss"); Output.set("lifeEnded", Scenario.ended);
    Output.set("fieldHealthAfter", Scenario.player.health); Output.set("endTurns", EndCount);
    Output.set("actions", ActionCount); Output.set("trace", Json(std::move(Trace)));
    return Output;
}

// 用途：验证事件提供的非战斗回避及真实撤离机会；流程：风险证据；用法：传同种子原始事件副本；返回：资源差和是否开战，不计入战胜率。
Json mfAvoidance(const Game& Scenario) {
    Json Output(json::Object{});
    for (const string Option : {"leave", "talk"}) {
        Game Copy = Scenario;
        const int StonesBefore = Copy.player.stones, HealthBefore = Copy.player.health;
        if (!adventureChoose(Copy, Option)) throw std::runtime_error("回避选项无法执行");
        Json Item(json::Object{});
        Item.set("healthDelta", Copy.player.health - HealthBefore);
        Item.set("stonesDelta", Copy.player.stones - StonesBefore);
        Item.set("battleActive", battleActive(Copy)); Item.set("pending", Copy.pending.has_value());
        Output.set(Option, std::move(Item));
    }
    Game Copy = Scenario;
    adventureChoose(Copy, "fight");
    if (!battleActive(Copy)) throw std::runtime_error("撤离检查无法开战");
    Output.set("battleEscapeChance", battleEscapeChance(Copy.player, *Copy.player.arts.battle));
    const TurnResult Escape = battleTurn(Copy, "escape");
    Output.set("escapeAccepted", Escape.ok); Output.set("battleAfterEscape", battleActive(Copy));
    Output.set("lifeEndedAfterEscape", Copy.ended);
    return Output;
}

// 用途：按同种子真实建角生成比较矩阵并保存报告；流程：独立命令行诊断；用法：starter-battle-probe 报告路径；返回：非法动作/中断或写入失败时非零。
int mfRun(const string& ReportPath, const string& PolicyFilter, const string& VariantFilter) {
    Json Report(json::Object{});
    Report.set("diagnosticVersion", "StarterBattleV5");
    Report.set("variantContract", "current is untouched actual createGame; any historical candidate first explicitly restores old sword/water/wood/skim and its six known spells, then applies only that candidate");
    Report.set("creation", "createGame; explicit no traits; legal 10 points allocated apt=10, body=0; no manuals/nodes/gear upgraded");
    Report.set("encounter", "adventureEvent(bandit) -> pending stage boundary fixture -> adventureChoose(fight) -> battleTurn");
    Report.set("policies", "NpcPriorityV1 = official NPC priority with real player action budget; VisibleGreedyV2 = RawDamage includes shield breaking, no last-mana blind draw; VisibleTwoStepV1/V2 = at most 2 actual non-draw casts from already held cards, public enemy forecast, draws valued from public count but never executed/searched. V1 linear incoming-health weight favors stalling; V2 uses lower health weight above 55% to trade health for progress. None represents skilled human ceiling. Discard prior VisibleGreedyV1: it incorrectly valued shield-breaking attacks at zero.");
    vector<string> Policies = {"NpcPriorityV1", "VisibleGreedyV2", "VisibleTwoStepV1", "VisibleTwoStepV2"};
    if (PolicyFilter != "all") {
        if (std::find(Policies.begin(), Policies.end(), PolicyFilter) == Policies.end()) throw std::runtime_error("未知策略筛选");
        Policies = {PolicyFilter};
    }
    vector<string> Variants = {"current", "gold", "body", "goldGather", "goldCover", "sealCycle", "goldSeals", "goldFire"};
    if (VariantFilter == "new") Variants = {"sealCycle", "goldSeals", "goldFire"};
    else if (VariantFilter != "all") {
        if (std::find(Variants.begin(), Variants.end(), VariantFilter) == Variants.end()) throw std::runtime_error("未知初装变体筛选");
        Variants = {VariantFilter};
    }
    Report.set("limitations", "Bandit EnemySpec lethal=false makes battle escape guaranteed despite wild defeat ending life. Travel boat description and real nonzero ambush risk differ; neither is changed here. Natural encounters are 82% same / 18% +1 small realm by source; this matrix deliberately tests both legal boundaries for every birth seed.");
    json::Array Games, Avoidance;
    Json Summaries(json::Object{});
    int NaturalUpperCount = 0;
    for (int SeedIndex = 0; SeedIndex < SeedCount; ++SeedIndex) {
        const unsigned Seed = 31U + 997U * static_cast<unsigned>(SeedIndex);
        CreateOptions Options;
        Options.name = "开局诊断"; Options.seed = Seed;
        Options.traitsExplicit = true; Options.traits.clear();
        Options.allocationExplicit = true; Options.allocation = {{"apt", 10}};
        Game Scenario = createGame(Options);
        if (Scenario.player.stage != 0 || Scenario.player.arts.main != "tuna") throw std::runtime_error("真实建角基线改变");
        adventureEvent(Scenario, "bandit");
        if (!Scenario.pending || !Scenario.pending->adventure) throw std::runtime_error("奇遇生成失败");
        NaturalUpperCount += Scenario.pending->adventure->stage == 1;
        Json Avoided = mfAvoidance(Scenario); Avoided.set("seed", static_cast<double>(Seed));
        Avoidance.push_back(std::move(Avoided));
        for (const string& Policy : Policies)
            for (const int EnemyStage : {0, 1})
                for (const string& Variant : Variants) {
                    Json Result = mfPlay(Scenario, Variant, Policy, EnemyStage, Seed);
                    const string Key = Policy + ":stage" + std::to_string(EnemyStage) + ":" + Variant;
                    Json Summary = Summaries[Key];
                    Summary.set("games", Summary["games"].asInt() + 1);
                    Summary.set("wins", Summary["wins"].asInt() + (Result["outcome"].asString() == "win" ? 1 : 0));
                    Summary.set("deaths", Summary["deaths"].asInt() + (Result["lifeEnded"].asBool() ? 1 : 0));
                    Summary.set("turnLimitLosses", Summary["turnLimitLosses"].asInt() +
                        (Result["outcome"].asString() == "loss" && Result["endTurns"].asInt() >= 24 ? 1 : 0));
                    Summary.set("endTurnTotal", Summary["endTurnTotal"].asInt() + Result["endTurns"].asInt());
                    Summaries.set(Key, std::move(Summary)); Games.push_back(std::move(Result));
                }
    }
    Report.set("seedCount", SeedCount); Report.set("naturalUpperSmallRealmCount", NaturalUpperCount);
    Report.set("summary", Summaries); Report.set("avoidance", Json(std::move(Avoidance)));
    Report.set("games", Json(std::move(Games)));
    std::ofstream File(ReportPath, std::ios::binary);
    if (!File) throw std::runtime_error("无法写入指定诊断报告");
    File << Report.dump(2);
    if (!File) throw std::runtime_error("写入诊断报告失败");
    std::cout << Summaries.dump(2) << "\n";
    return 0;
}
}

// 用途：保留系统 main 约定并收口诊断错误；流程：独立 EXE 入口；用法：报告路径及可选策略/变体筛选；返回：成功 0，参数/规则/IO 失败 1。
int main(int ArgumentCount, char** Arguments) {
    try {
        if (ArgumentCount < 2 || ArgumentCount > 4) throw std::runtime_error("用法: starter-battle-probe report.json [策略或all] [变体或all/new]");
        return mfRun(Arguments[1], ArgumentCount >= 3 ? Arguments[2] : "all", ArgumentCount == 4 ? Arguments[3] : "all");
    } catch (const std::exception& Error) {
        std::cerr << Error.what() << "\n";
        return 1;
    }
}
