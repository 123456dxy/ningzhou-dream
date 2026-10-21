#include "core/interface/battle_lab_api.h"

#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/character_build.h"
#include "core/content/advanced_builds.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/elemental_sword.h"
#include "core/combat/hand_rules.h"
#include "core/talents.h"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nz::BattleLab {
namespace {

using JsonValue = json::Value;
constexpr std::size_t MaximumBodyBytes = 4096;
constexpr std::size_t MaximumInputStringBytes = 80;

// 用途：构造统一 JSON 响应；流程：所有试炼路由；用法：传状态码和拥有 JSON；返回：序列化响应，无会话副作用。
http::Response mfJsonResponse(int Status, JsonValue Body) {
    http::Response Response;
    Response.status = Status;
    Response.body = Body.dump();
    Response.extraHeaders.emplace_back("Cache-Control", "no-store");
    return Response;
}

// 用途：携带现有快照返回拒绝；流程：非法/过期动作；用法：传只读会话、原因和状态码；返回：方便页面刷新恢复的响应。
http::Response mfErrorResponse(const Session& LabSession, const std::string& Error, int Status = 400) {
    JsonValue Body = mfStateView(LabSession);
    Body.set("error", Error);
    return mfJsonResponse(Status, std::move(Body));
}

// 用途：校验有限整数及范围；流程：外部数值翻译；用法：传 JSON 和闭区间；返回：合法时为真，不采用截断转换。
bool mfIsInteger(const JsonValue& Value, double Minimum, double Maximum) {
    return Value.isNumber() && std::isfinite(Value.asNumber()) &&
        Value.asNumber() >= Minimum && Value.asNumber() <= Maximum &&
        std::floor(Value.asNumber()) == Value.asNumber();
}

// 用途：拒绝未知参数键；流程：start/turn 解析；用法：传对象与稳定白名单；返回：键全部合法时为真。
bool mfHasOnlyKeys(const JsonValue& Value, std::initializer_list<std::string_view> Keys) {
    if (!Value.isObject()) return false;
    for (const auto& [Key, Field] : Value.asObject()) {
        static_cast<void>(Field);
        if (std::find(Keys.begin(), Keys.end(), Key) == Keys.end()) return false;
    }
    return true;
}

// 用途：读取受限字符串；流程：外部 ID 解析；用法：传字段；返回：合法非空短字符串时为真。
bool mfIsIdentifier(const JsonValue& Value) {
    return Value.isString() && !Value.asString().empty() && Value.asString().size() <= MaximumInputStringBytes;
}

// 用途：编码选择；流程：state/config；用法：传值对象；返回：稳定 JSON 键，种子保持 uint32 精度。
JsonValue mfSelectionView(const Selection& Selected) {
    JsonValue View(json::Object{});
    View.set("buildId", Selected.BuildId);
    View.set("stage", Selected.Stage);
    View.set("opponentId", Selected.OpponentId);
    View.set("seed", static_cast<double>(Selected.Seed));
    View.set("tier", Selected.LoadoutTier);
    return View;
}

// 用途：编码神通状态数值；流程：手牌完整预览；用法：传真实预览；返回：状态数组，无规则重算。
JsonValue mfStatusView(const Preview& CardPreview) {
    json::Array Entries;
    for (const auto& Status : CardPreview.statuses) {
        JsonValue Entry(json::Object{});
        Entry.set("id", Status.id);
        Entry.set("name", Status.name);
        Entry.set("target", Status.target);
        Entry.set("amount", Status.amount);
        Entry.set("total", Status.total);
        Entry.set("turns", Status.turns);
        Entry.set("perTurn", Status.perTurn);
        Entries.push_back(std::move(Entry));
    }
    return JsonValue(std::move(Entries));
}

// 用途：在普通战况协议上补齐手牌和双方持续状态；流程：试炼只读战况；用法：传只读游戏；返回：JSON，不修改公用 mobile 协议。
JsonValue mfBattleView(const Game& GameState) {
    JsonValue View = api::battleView(GameState);
    if (!GameState.player.arts.battle) return View;
    const auto& BattleState = *GameState.player.arts.battle;
    auto Hands = View["hand"].asArray();
    for (auto& Hand : Hands) {
        const Preview CardPreview = battlePreview(GameState.player, BattleState, Hand["id"].asString());
        Hand.set("rawDamage", CardPreview.RawDamage);
        Hand.set("elementalGenerationReady", CardPreview.ElementalGenerationReady);
        Hand.set("energy", CardPreview.energy);
        Hand.set("manaGain", CardPreview.manaGain);
        Hand.set("draw", CardPreview.draw);
        Hand.set("drawNext", CardPreview.drawNext);
        Hand.set("healPotential", CardPreview.healPotential);
        Hand.set("statuses", mfStatusView(CardPreview));
    }
    View.set("hand", JsonValue(std::move(Hands)));
    View.set("elementalSwordSequence", BattleState.ElementalSwordSequence);
    View.set("elementalSwordEnhancementLayers", BattleState.ElementalSwordEnhancementLayers);
    View.set("elementalSwordEnhancementLimit", ElementalSword::MaximumEnhancementLayers);
    View.set("elementalSwordEnhancementPercent", jsRound((ElementalSword::mfEnhancementMultiplier(talentMods(GameState.player), BattleState) - 1.0) * 100));
    View.set("enemyElementalSwordEnhancementLayers", BattleState.enemyCards.ElementalSwordEnhancementLayers);
    View.set("handLimit", BattleCards::MaximumHandSize);
    View.set("enemyElementalSwordSequence", BattleState.enemyCards.ElementalSwordSequence);
    // 妖兽使用独立妖力预算；旧档冻结单意图在下一动作开轮前仍显示一次行动。
    View.set("enemyActionLimit", BattleState.enemyCards.enabled
        ? mfBattleEnemyActionLimit(GameState, BattleState) : BattleState.BeastActions.empty()
            ? 1 : BeastCombat::mfActionLimit(BattleState.enemyStage));
    View.set("playerBurn", BattleState.enemyCards.burn);
    View.set("playerBurnTurns", BattleState.enemyCards.burnTurns);
    View.set("poisonDamage", BattleState.poisonDamage >= 0 ? BattleState.poisonDamage :
        jsRound(mfBattleDamageBasis(GameState.player.stage) * LegacyPoisonTickBasisCoefficient * BattleState.poison));
    // 局部回合预演仅消费副本 RNG，便于区别本轮零伤害和下一轮新施加的毒火。
    Game Forecast = GameState;
    const TurnResult ForecastTurn = battleTurn(Forecast, "end");
    json::Array FutureStatuses;
    if (ForecastTurn.ok && Forecast.player.arts.battle) {
        const auto& FutureBattle = *Forecast.player.arts.battle;
        if (FutureBattle.playerPoison > 0) {
            JsonValue Status(json::Object{});
            Status.set("id", "poison");
            Status.set("name", "毒伤");
            Status.set("perTurn", FutureBattle.playerPoison);
            Status.set("turns", FutureBattle.playerPoisonTurns);
            FutureStatuses.push_back(std::move(Status));
        }
        if (FutureBattle.enemyCards.burn > 0) {
            JsonValue Status(json::Object{});
            Status.set("id", "burn");
            Status.set("name", "灼烧");
            Status.set("perTurn", FutureBattle.enemyCards.burn);
            Status.set("turns", FutureBattle.enemyCards.burnTurns);
            FutureStatuses.push_back(std::move(Status));
        }
    }
    View.set("enemyIntentStatuses", JsonValue(std::move(FutureStatuses)));
    return View;
}

// 用途：编码已配装的真实功法、神通和节点；流程：试炼构筑展示；用法：传会话；返回：拥有 JSON，不补全或研习功法。
JsonValue mfLoadoutView(const Session& LabSession) {
    JsonValue View(json::Object{});
    json::Array Manuals, Spells, Nodes, Unavailable;
    const Arts& Loadout = LabSession.CurrentGame().player.arts;
    if (LabSession.HasMatch) {
        for (const auto& [ManualId, Level] : Loadout.manuals) {
            ManualInfo Definition;
            if (!findManualInfo(ManualId, Definition)) continue;
            JsonValue Entry(json::Object{});
            Entry.set("id", ManualId);
            Entry.set("name", Definition.name);
            Entry.set("level", Level);
            Entry.set("role", ManualId == Loadout.main ? "主修" : "辅修");
            Entry.set("description", Definition.effect);
            Manuals.push_back(std::move(Entry));
        }
        for (const auto& SpellId : Loadout.spells) {
            const auto* Definition = battleSpell(SpellId);
            if (!Definition) continue;
            JsonValue Entry(json::Object{});
            Entry.set("id", SpellId);
            Entry.set("name", Definition->name);
            Spells.push_back(std::move(Entry));
        }
        for (const auto& NodeId : Loadout.nodes) {
            const auto* Definition = findNode(NodeId);
            if (!Definition) continue;
            JsonValue Entry(json::Object{});
            Entry.set("id", NodeId);
            Entry.set("name", Definition->name);
            Entry.set("description", Definition->desc);
            Nodes.push_back(std::move(Entry));
        }
    }
    for (const auto& Reason : LabSession.Unavailable) Unavailable.emplace_back(Reason);
    View.set("manuals", JsonValue(std::move(Manuals)));
    View.set("spells", JsonValue(std::move(Spells)));
    View.set("nodes", JsonValue(std::move(Nodes)));
    View.set("unavailable", JsonValue(std::move(Unavailable)));
    View.set("notes", LabSession.CurrentSelection.LoadoutTier == "advanced"
        ? "完整传承取统一高级构筑目录；功法五层、建议悟道含前置，仍按境界和槽位过滤。无隐藏伤害、天赋、装备或丹药加成；真实随机发牌。普通攻防各五张固定携带。"
        : "初始流派取普通构筑建议；功法五层、建议悟道含前置，按境界和槽位过滤。无天赋、装备或丹药加成；真实随机发牌。普通攻防各五张固定携带。");
    return View;
}

} // namespace

// 用途：输出真实流派和白名单对手；流程：配置 GET；用法：无参数；返回：拥有目录快照，无会话副作用。
json::Value mfConfigurationView() {
    JsonValue View(json::Object{});
    json::Array Builds, Realms, Opponents, Tiers;
    for (const auto& Build : artBuilds()) {
        JsonValue Entry(json::Object{});
        Entry.set("id", Build.id);
        Entry.set("name", Build.name);
        Entry.set("description", Build.desc);
        const auto* Advanced = content::mfFindAdvancedBuild(Build.id);
        Entry.set("advancedName", Advanced ? Advanced->name : Build.name);
        Entry.set("advancedDescription", Advanced ? Advanced->desc : "");
        Builds.push_back(std::move(Entry));
    }
    for (const int Stage : mfStages()) {
        JsonValue Entry(json::Object{});
        Entry.set("stage", Stage);
        Entry.set("name", realmName(Stage));
        Realms.push_back(std::move(Entry));
    }
    for (const auto& Opponent : mfOpponents()) {
        JsonValue Entry(json::Object{});
        Entry.set("id", Opponent.Id);
        Entry.set("name", Opponent.Name);
        Opponents.push_back(std::move(Entry));
    }
    View.set("builds", JsonValue(std::move(Builds)));
    View.set("realms", JsonValue(std::move(Realms)));
    View.set("opponents", JsonValue(std::move(Opponents)));
    for (const auto& [TierId, TierName] : {std::pair{"initial", "初始流派"}, std::pair{"advanced", "完整传承"}}) {
        JsonValue Entry(json::Object{});
        Entry.set("id", TierId);
        Entry.set("name", TierName);
        Tiers.push_back(std::move(Entry));
    }
    View.set("tiers", JsonValue(std::move(Tiers)));
    View.set("defaultSelection", mfSelectionView(Selection{}));
    return View;
}

// 用途：输出可恢复的完整试炼快照；流程：GET/动作响应；用法：传稳定只读 Session；返回：JSON，不改变 RNG、时钟、NPC 或存档。
json::Value mfStateView(const Session& LabSession) {
    JsonValue View(json::Object{});
    View.set("revision", LabSession.Revision);
    View.set("selection", mfSelectionView(LabSession.CurrentSelection));
    View.set("battle", LabSession.HasMatch ? mfBattleView(LabSession.CurrentGame()) : JsonValue());
    JsonValue PlayerView(json::Object{});
    if (LabSession.HasMatch) {
        const auto& PlayerState = LabSession.CurrentGame().player;
        const Battle Reference = battleReferenceState(PlayerState);
        const auto& Active = PlayerState.arts.battle;
        PlayerView.set("name", PlayerState.name);
        PlayerView.set("portrait", playerPortrait(PlayerState));
        PlayerView.set("realmName", realmName(PlayerState.stage));
        PlayerView.set("hp", Active ? Active->hp : Reference.hp);
        PlayerView.set("max", Active ? Active->max : Reference.max);
    }
    View.set("player", std::move(PlayerView));
    View.set("loadout", mfLoadoutView(LabSession));
    json::Array Log;
    for (const auto& Message : LabSession.CombatLog) Log.emplace_back(Message);
    View.set("log", JsonValue(std::move(Log)));
    JsonValue ResultView;
    if (LabSession.MatchResult) {
        ResultView.set("won", LabSession.MatchResult->Won);
        ResultView.set("turns", LabSession.MatchResult->Turns);
        ResultView.set("text", LabSession.MatchResult->Text);
    }
    View.set("result", std::move(ResultView));
    return View;
}

// 用途：翻译和串行提交试炼请求；流程：独立 loopback HTTP；用法：传拥有会话/已解析 Request；返回：状态 JSON 或明确错误。
http::Response mfHandle(Session& LabSession, const http::Request& Request) {
    static_assert(std::is_nothrow_swappable_v<Session>);
    try {
        if (Request.method == "GET" && Request.path == "/api/lab/config")
            return mfJsonResponse(200, mfConfigurationView());
        if (Request.method == "GET" && Request.path == "/api/lab/state")
            return mfJsonResponse(200, mfStateView(LabSession));
        if (Request.path != "/api/lab/start" && Request.path != "/api/lab/turn")
            return mfErrorResponse(LabSession, "未知试炼接口。", 404);
        if (Request.method != "POST") return mfErrorResponse(LabSession, "此试炼接口需要 POST。", 405);
        if (Request.body.size() > MaximumBodyBytes) return mfErrorResponse(LabSession, "请求内容过大。", 413);
        std::string ParseError;
        const JsonValue Input = JsonValue::parse(Request.body, &ParseError);
        if (!ParseError.empty() || !Input.isObject()) return mfErrorResponse(LabSession, "需要合法 JSON 对象。 ");
        Session Candidate = LabSession;
        ActionResult Result;
        if (Request.path == "/api/lab/start") {
            if (!mfHasOnlyKeys(Input, {"buildId", "stage", "opponentId", "seed", "revision", "tier"}) ||
                !mfIsIdentifier(Input["buildId"]) || !mfIsIdentifier(Input["opponentId"]) ||
                !mfIsInteger(Input["stage"], 0, 12) ||
                !mfIsInteger(Input["seed"], 0, std::numeric_limits<std::uint32_t>::max()))
                return mfErrorResponse(LabSession, "开局参数无效。 ");
            if (Input.asObject().count("tier") && !mfIsIdentifier(Input["tier"]))
                return mfErrorResponse(LabSession, "构筑层级参数无效。 ");
            std::optional<int> Revision;
            if (Input.asObject().count("revision")) {
                if (!mfIsInteger(Input["revision"], 0, std::numeric_limits<int>::max()))
                    return mfErrorResponse(LabSession, "战况版本无效。 ");
                Revision = Input["revision"].asInt();
            }
            if (Revision && *Revision != LabSession.Revision)
                return mfErrorResponse(LabSession, "战况已更新，请刷新后再试。", 409);
            Selection Requested;
            Requested.BuildId = Input["buildId"].asString();
            Requested.Stage = Input["stage"].asInt();
            Requested.OpponentId = Input["opponentId"].asString();
            Requested.Seed = static_cast<std::uint32_t>(Input["seed"].asNumber());
            if (Input.asObject().count("tier")) Requested.LoadoutTier = Input["tier"].asString();
            Result = mfStart(Candidate, Requested, Revision);
        } else {
            if (!mfHasOnlyKeys(Input, {"action", "spell", "target", "revision"}) ||
                !mfIsIdentifier(Input["action"]) ||
                !mfIsInteger(Input["revision"], 0, std::numeric_limits<int>::max()))
                return mfErrorResponse(LabSession, "行动参数无效。 ");
            for (const char* Field : {"spell", "target"})
                if (Input.asObject().count(Field) && (!Input[Field].isString() || Input[Field].asString().size() > MaximumInputStringBytes))
                    return mfErrorResponse(LabSession, "神通或目标参数无效。 ");
            const int Revision = Input["revision"].asInt();
            if (Revision != LabSession.Revision)
                return mfErrorResponse(LabSession, "战况已更新，请刷新后再试。", 409);
            Result = mfTurn(Candidate, Input["action"].asString(), Input["spell"].asString(), Input["target"].asString(), Revision);
        }
        if (!Result.Succeeded) return mfErrorResponse(LabSession, Result.Error);
        // 先成功序列化响应再提交，技术失败不会让客户端误以为未执行而重复扣费。
        http::Response Response = mfJsonResponse(200, mfStateView(Candidate));
        using std::swap;
        swap(LabSession, Candidate);
        return Response;
    } catch (const std::exception&) {
        http::Response Response;
        Response.status = 500;
        Response.body = "{\"error\":\"试炼请求未能完成，请刷新战况后重试。\"}";
        return Response;
    }
}

} // namespace nz::BattleLab
