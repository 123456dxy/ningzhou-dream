#include "core/interface/zhuji_api.h"
#include "core/progression/lifebound_rules.h"
#include "core/progression/zhuji_rules.h"
#include "core/progression/zhuji_board.h"
#include "core/combat/battle_scaling.h"
#include "core/sim.h"
#include "core/battle.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace nz::api {
namespace {
const char* ElementIds[5] = {"gold", "wood", "water", "fire", "earth"};
const char* ElementNames[5] = {"金", "木", "水", "火", "土"};
const char* mfStatusName(ZhujiStatus Status) {
    switch (Status) {
    case ZhujiStatus::Preparing: return "preparing";
    case ZhujiStatus::Active: return "active";
    case ZhujiStatus::Success: return "success";
    case ZhujiStatus::Failed: return "failed";
    case ZhujiStatus::Withdrawn: return "withdrawn";
    }
    return "invalid";
}
bool mfInteger(const json::Value& Value, int Minimum, int Maximum) {
    return Value.isNumber() && std::isfinite(Value.asNumber()) && Value.asNumber() == std::floor(Value.asNumber()) &&
        Value.asNumber() >= Minimum && Value.asNumber() <= Maximum;
}
bool mfAllowed(const json::Value& Value, const std::set<std::string>& Keys) {
    if (!Value.isObject()) return false;
    for (const auto& [Key, Entry] : Value.asObject()) if (!Keys.contains(Key)) return false;
    return true;
}
json::Value mfReward(int Score) {
    json::Value Reward(json::Object{}); const int Gain = mfZhujiHealthReward(Score);
    Reward.set("baseHpGain", Gain); Reward.set("battleHpEquivalent", Gain * 2.4); return Reward;
}
json::Value mfQuoteJson(const ZhujiQuote& Quote) {
    json::Value Output(json::Object{}); Output.set("enabled", Quote.Enabled); Output.set("reason", Quote.Reason);
    Output.set("qiCost", Quote.QiCost); Output.set("successPercent", Quote.SuccessPercent); Output.set("points", Quote.Points);
    Output.set("nextDrawCount", Quote.NextDrawCount); Output.set("scoreAfter", Quote.ScoreAfter); Output.set("effectText", Quote.Effect);
    Output.set("reward", mfReward(Quote.ScoreAfter)); return Output;
}
json::Value mfActionJson(const ZhujiAction& Action) {
    json::Value Output(json::Object{}); Output.set("kind", Action.Kind);
    if (!Action.TargetId.empty()) Output.set("targetId", Action.TargetId);
    if (!Action.Spend.empty()) {
        json::Array Spend; for (int Index : Action.Spend) Spend.emplace_back(ElementIds[Index]); Output.set("spend", json::Value(std::move(Spend)));
    }
    return Output;
}
std::string mfParseTokens(const json::Value& Input, std::vector<int>& Output, int Limit) {
    if (!Input.isArray() || Input.asArray().size() > static_cast<std::size_t>(Limit)) return "灵气选择必须是合法数组。";
    for (const auto& Entry : Input.asArray()) {
        if (!Entry.isString()) return "五行ID必须是字符串。";
        int Element = -1;
        for (int Index = 0; Index < 5; ++Index) if (Entry.asString() == ElementIds[Index]) Element = Index;
        if (Element < 0) return "未知五行ID。";
        Output.push_back(Element);
    }
    return "";
}
std::string mfParseAction(const ZhujiRun& Run, const json::Value& Input, ZhujiAction& Output) {
    if (!Input.isObject() || !Input["kind"].isString()) return "筑基动作必须包含kind。";
    Output.Kind = Input["kind"].asString();
    std::set<std::string> Keys = {"kind"};
    if (Run.Route == ZhujiRoute::Elements) {
        if (Output.Kind == "convert") Keys = {"kind", "spend", "targetId"};
        else if (Output.Kind == "basic" || Output.Kind == "same" || Output.Kind == "generate" || Output.Kind == "five" || Output.Kind == "gather") Keys = {"kind", "spend"};
        else if (Output.Kind == "next") Keys = {"kind", "keep"};
        else if (Output.Kind != "seal" && Output.Kind != "withdraw") return "未知五行动作。";
        if (Keys.contains("spend")) {
            if (auto Error = mfParseTokens(Input["spend"], Output.Spend, 5); !Error.empty()) return Error;
        }
        if (Input.asObject().contains("keep")) {
            Output.Keep.emplace();
            if (auto Error = mfParseTokens(Input["keep"], *Output.Keep, 10); !Error.empty()) return Error;
        }
        if (Output.Kind == "convert") {
            if (!Input["targetId"].isString()) return "调息缺少目标五行。";
            Output.TargetId = Input["targetId"].asString();
        }
    } else {
        if (Output.Kind == "strike" || Output.Kind == "move") {
            Keys = {"kind", "targetId"};
            if (!Input["targetId"].isString() || mfZhujiNodeIndex(Input["targetId"].asString()) < 0) return "未知穴位ID。";
            Output.TargetId = Input["targetId"].asString();
        } else if (Output.Kind != "restore" && Output.Kind != "seal" && Output.Kind != "withdraw") return "未知周天动作。";
    }
    if (!mfAllowed(Input, Keys)) return "筑基动作含未知字段。";
    return "";
}
}

namespace view {
json::Value mfZhujiView(const Game& State) {
    json::Value Output(json::Object{});
    Output.set("lifeId", State.lifeId);
    const auto Info = breakthroughInfo(State);
    Output.set("available", State.player.stage == 2);
    Output.set("revision", State.player.Zhuji ? State.player.Zhuji->Revision : 0);
    Output.set("cost", Info.cost); Output.set("days", 90); Output.set("pillAvailable", State.player.pills); Output.set("coreAvailableCount", State.player.cores);
    const bool PreparedPill = State.player.arts.breakBuff == "stage" && State.player.arts.BreakthroughPillStage == 2;
    Output.set("pillMaximum", PreparedPill ? 5 : 6); Output.set("preparedPill", PreparedPill);
    const bool CanStart = State.player.stage == 2 && !State.ended && !State.job && !State.pending && !State.player.arts.battleActive() && !State.dungeons.run &&
        Info.ready && State.player.stones >= Info.cost && State.player.health >= HealthGateMinimum && (!State.player.Zhuji || State.player.Zhuji->Settled);
    Output.set("canStart", CanStart);
    Output.set("startReason", CanStart ? "" : State.player.stage != 2 ? "仅炼气后期可选择筑基方式。" :
        !Info.ready ? "修为尚未圆满。" : State.player.stones < Info.cost ? "灵石不足。" : State.player.health < HealthGateMinimum ? "请先休养至30气血。" : "请先完成当前行动。");
    if (!State.player.Zhuji) { Output.set("status", "unstarted"); return Output; }
    const auto& Run = *State.player.Zhuji;
    Output.set("route", Run.Route == ZhujiRoute::Elements ? "elements" : "meridians"); Output.set("status", mfStatusName(Run.Status));
    Output.set("score", Run.Score); Output.set("rawScore", Run.RawScore); Output.set("pills", Run.PillCount); Output.set("core", Run.PreparedCore);
    Output.set("permanent", Run.Status == ZhujiStatus::Success); Output.set("settled", Run.Settled);
    Output.set("reward", Run.Status == ZhujiStatus::Success ? mfReward(Run.Score) : json::Value());
    Output.set("projectedReward", mfReward(Run.Score));
    Output.set("lifeboundKind", State.player.arts.LifeboundKind);
    Output.set("lifeboundSpell", State.player.arts.LifeboundSpell);
    const auto* BoundSpell = battleSpell(State.player.arts.LifeboundSpell);
    Output.set("lifeboundName", BoundSpell ? BoundSpell->name : "");
    json::Array LifeboundChoices;
    if (Run.Status == ZhujiStatus::Success && Run.Settled) {
        std::set<string> Available(State.player.arts.knownSpells.begin(), State.player.arts.knownSpells.end());
        Available.insert("strike"); Available.insert("defend");
        for (const auto& Id : Available) {
            const auto* Spell = battleSpell(Id);
            if (!Spell || State.player.stage < Spell->stage) continue;
            const auto Quote = mfQuoteLifeboundSpell(State, Id);
            json::Value Choice(json::Object{}); Choice.set("id", Id); Choice.set("name", Spell->name);
            Choice.set("selected", Quote.IsCurrent); Choice.set("enabled", Quote.Enabled); Choice.set("reason", Quote.Reason);
            LifeboundChoices.push_back(std::move(Choice));
        }
    }
    Output.set("lifeboundChoices", json::Value(std::move(LifeboundChoices)));
    json::Array History; for (const auto& Entry : Run.History) History.emplace_back(Entry); Output.set("history", json::Value(std::move(History)));
    if (Run.Status == ZhujiStatus::Preparing) return Output;
    json::Array Choices;
    const auto Add = [&](const ZhujiAction& Action, const std::string& Label) {
        json::Value Choice(json::Object{}); Choice.set("action", mfActionJson(Action)); Choice.set("label", Label);
        Choice.set("quote", mfQuoteJson(mfQuoteZhujiAction(Run, Action))); Choices.push_back(std::move(Choice));
    };
    if (Run.Route == ZhujiRoute::Elements) {
        Output.set("round", Run.Round); Output.set("maxRounds", 8); Output.set("actionPoints", Run.ActionPoints);
        Output.set("carryLimit", Run.DaoBody ? 10 : 8); Output.set("pendingDrawCount", Run.PendingDrawCount);
        json::Value Resources(json::Object{}); for (int Index = 0; Index < 5; ++Index) Resources.set(ElementIds[Index], Run.Resources[Index]); Output.set("resources", std::move(Resources));
        json::Array Draws; for (int Index : Run.PillDraws) Draws.emplace_back(ElementIds[Index]); Output.set("pillDraws", json::Value(std::move(Draws)));
        constexpr int Generates[5] = {2, 3, 1, 4, 0};
        for (int Index = 0; Index < 5; ++Index) {
            Add({"basic", {Index, Index}}, std::string(ElementNames[Index]) + "·基础凝液");
            Add({"same", {Index, Index, Index}}, std::string(ElementNames[Index]) + "·同源凝练");
            Add({"generate", {Index, Index, Generates[Index], Generates[Index]}}, std::string(ElementNames[Index]) + "·相生合炼");
            Add({"gather", {Index, Index}}, std::string(ElementNames[Index]) + "·聚气待发");
            for (int Target = 0; Target < 5; ++Target) if (Index != Target) {
                const int Count = Run.ElementRoot == Target && Run.Resources[Index] >= 2 ? 2 : 1;
                Add({"convert", std::vector<int>(Count, Index), ElementIds[Target]}, std::string(ElementNames[Index]) + "→" + ElementNames[Target]);
            }
        }
        Add({"five", {0, 1, 2, 3, 4}}, "五行合一");
        Add({"next"}, Run.Round == 8 ? "八轮定型" : "进入下一轮");
    } else {
        Output.set("qi", Run.Qi); Output.set("qiInitial", Run.QiInitial); Output.set("positionId", Run.PositionId);
        Output.set("coreAvailable", Run.CoreAvailable);
        const auto Adjacent = mfZhujiNeighbors(Run);
        json::Array Nodes, Edges, Gates;
        for (int Index = 0; Index < 36; ++Index) {
            const auto& Node = mfZhujiNodes()[Index]; const auto& NodeState = Run.Nodes[Index];
            const bool IsAdjacent = std::find(Adjacent.begin(), Adjacent.end(), Index) != Adjacent.end();
            json::Value Item(json::Object{}); Item.set("id", Node.Id); Item.set("x", Node.X); Item.set("y", Node.Y);
            Item.set("label", NodeState.Revealed ? Node.Label : "未明穴"); Item.set("opened", NodeState.Opened); Item.set("revealed", NodeState.Revealed);
            Item.set("isCurrent", Run.PositionId == Node.Id); Item.set("isAdjacent", IsAdjacent); Item.set("blocked", !NodeState.Opened && !NodeState.AttemptsLeft);
            if (NodeState.Revealed) {
                Item.set("kind", Node.Kind); Item.set("points", Node.Points); Item.set("attemptsLeft", NodeState.AttemptsLeft); Item.set("maxAttempts", Node.MaxAttempts);
                Item.set("cost", Node.Cost == 0 ? 0 : std::max(1, Node.Cost - (Run.DaoBody ? 1 : 0)));
                Item.set("successPercent", mfZhujiNodeSuccessPercent(Run, Index)); Item.set("effectText", mfZhujiNodeEffect(Index));
            }
            Nodes.push_back(std::move(Item));
            if (IsAdjacent) Add({NodeState.Opened ? "move" : "strike", {}, Node.Id}, std::string(NodeState.Opened ? "回走·" : "冲穴·") + (NodeState.Revealed ? Node.Label : "未明穴"));
        }
        for (const auto& Edge : mfZhujiEdges()) {
            json::Value Item(json::Object{}); Item.set("from", Edge.From); Item.set("to", Edge.To);
            const int Unlock = mfZhujiNodeIndex(Edge.UnlockedBy); Item.set("locked", Unlock >= 0 && !Run.Nodes[Unlock].Opened); Edges.push_back(std::move(Item));
        }
        for (const char* Gate : {"ren", "du", "chong"}) {
            bool Opened = false;
            for (int Index = 0; Index < 36; ++Index) if (Gate == std::string(mfZhujiNodes()[Index].GateId) && Run.Nodes[Index].Opened) Opened = true;
            json::Value Item(json::Object{}); Item.set("id", Gate); Item.set("opened", Opened); Gates.push_back(std::move(Item));
        }
        Output.set("nodes", json::Value(std::move(Nodes))); Output.set("edges", json::Value(std::move(Edges))); Output.set("gates", json::Value(std::move(Gates)));
        Add({"restore"}, "使用灵核");
    }
    Add({"seal"}, Run.Route == ZhujiRoute::Elements ? "提前定型" : "归元定型"); Add({"withdraw"}, "收功离关");
    Output.set("choices", json::Value(std::move(Choices)));
    return Output;
}
}

std::string mfExecuteZhujiCommand(Game& State, const json::Value& Input, json::Value& Output) {
    if (!Input.isObject() || !Input["kind"].isString()) return "筑基请求缺少kind。";
    const auto& Kind = Input["kind"].asString();
    const bool IsQuote = Kind == "quote"; const bool IsStart = Kind == "start"; const bool IsBind = Kind == "bind";
    if (!IsQuote && !IsStart && !IsBind && Kind != "action") return "未知筑基请求。";
    const std::set<std::string> Keys = IsStart ? std::set<std::string>{"kind", "route", "pills", "core", "revision", "requestId"}
        : IsQuote ? std::set<std::string>{"kind", "revision", "action"} : IsBind ? std::set<std::string>{"kind", "revision", "requestId", "spell"}
        : std::set<std::string>{"kind", "revision", "requestId", "action"};
    if (!mfAllowed(Input, Keys) || !mfInteger(Input["revision"], 0, 100000)) return "筑基请求字段或修订号无效。";
    const int Revision = State.player.Zhuji ? State.player.Zhuji->Revision : 0;
    const auto Signature = Input.dump();
    const auto RequestId = Input["requestId"].asString();
    if (!IsQuote) {
        if (!Input["requestId"].isString() || RequestId.empty() || RequestId.size() > 100) return "缺少有效动作标识。";
        if (State.player.Zhuji) for (const auto& Receipt : State.player.Zhuji->Receipts) if (Receipt.RequestId == RequestId) {
            if (Receipt.Signature != Signature) return "重复动作内容不一致。";
            Output.set("replayed", true); Output.set("zhuji", view::mfZhujiView(State)); return "";
        }
    }
    if (Input["revision"].asInt() != Revision) return "筑基局面已变化，请刷新后重试。";
    if (State.ended) return "这一世已经结束。";
    if (IsStart) {
        if (!Input["route"].isString() || (Input["route"].asString() != "elements" && Input["route"].asString() != "meridians") ||
            !mfInteger(Input["pills"], 0, 6) || !Input["core"].isBool()) return "请选择合法路线、0至6枚丹药与灵核准备。";
        if (const auto Error = mfStartZhuji(State, Input["route"].asString() == "elements" ? ZhujiRoute::Elements : ZhujiRoute::Meridians,
            Input["pills"].asInt(), Input["core"].asBool()); !Error.empty()) return Error;
        Output.set("started", true);
    } else if (IsBind) {
        if (!State.player.Zhuji || State.player.Zhuji->Status != ZhujiStatus::Success || !State.player.Zhuji->Settled)
            return "请先完成筑基。";
        if (!Input["spell"].isString()) return "请选择本命神通。";
        if (const auto Error = mfSetLifeboundSpell(State, Input["spell"].asString()); !Error.empty()) return Error;
    } else {
        if (!State.player.Zhuji) return "尚未开始筑基。";
        if (State.job || State.pending || State.player.arts.battleActive() || State.dungeons.run) return "请先结束当前行动。";
        ZhujiAction Action;
        if (const auto Error = mfParseAction(*State.player.Zhuji, Input["action"], Action); !Error.empty()) return Error;
        if (IsQuote) {
            Output.set("quote", mfQuoteJson(mfQuoteZhujiAction(*State.player.Zhuji, Action))); Output.set("revision", Revision); return "";
        }
        if (const auto Error = mfApplyZhujiAction(*State.player.Zhuji, Action); !Error.empty()) return Error;
        mfSettleZhujiRun(State);
    }
    State.player.Zhuji->Receipts.push_back({RequestId, Signature});
    if (State.player.Zhuji->Receipts.size() > 256) State.player.Zhuji->Receipts.erase(State.player.Zhuji->Receipts.begin());
    Output.set("zhuji", view::mfZhujiView(State)); return "";
}
}
