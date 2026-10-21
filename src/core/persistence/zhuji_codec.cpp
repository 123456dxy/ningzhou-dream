#include "core/persistence/zhuji_codec.h"
#include "core/progression/zhuji_rules.h"
#include <algorithm>
#include <cmath>
#include <set>

namespace nz::persistence {
namespace {
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
bool mfInteger(const json::Value& Value, double Minimum, double Maximum) {
    return Value.isNumber() && std::isfinite(Value.asNumber()) && Value.asNumber() == std::floor(Value.asNumber()) &&
        Value.asNumber() >= Minimum && Value.asNumber() <= Maximum;
}
bool mfKeys(const json::Value& Value, const std::set<std::string>& Allowed) {
    if (!Value.isObject() || Value.asObject().size() != Allowed.size()) return false;
    for (const auto& [Key, Entry] : Value.asObject()) if (!Allowed.contains(Key)) return false;
    return true;
}
}
json::Value mfZhujiToJson(const ZhujiRun& Run) {
    json::Value Output(json::Object{});
    Output.set("rulesVersion", Run.RulesVersion);
    Output.set("route", Run.Route == ZhujiRoute::Elements ? "elements" : "meridians");
    Output.set("status", mfStatusName(Run.Status));
    Output.set("revision", Run.Revision); Output.set("settled", Run.Settled);
    Output.set("pills", Run.PillCount); Output.set("core", Run.PreparedCore);
    Output.set("daoBody", Run.DaoBody); Output.set("steady", Run.Steady); Output.set("elementRoot", Run.ElementRoot);
    Output.set("score", Run.Score); Output.set("rawScore", Run.RawScore); Output.set("round", Run.Round);
    Output.set("actionPoints", Run.ActionPoints); Output.set("pendingDrawCount", Run.PendingDrawCount);
    Output.set("converted", Run.Converted); Output.set("gathered", Run.Gathered);
    json::Array Resources, Draws, RandomStates, Nodes, History, Receipts;
    for (int Value : Run.Resources) Resources.emplace_back(Value);
    for (int Value : Run.PillDraws) Draws.emplace_back(Value);
    for (std::uint32_t Value : Run.RandomStates) RandomStates.emplace_back(static_cast<double>(Value));
    for (const auto& Node : Run.Nodes) {
        json::Value Item(json::Object{}); Item.set("opened", Node.Opened); Item.set("revealed", Node.Revealed);
        Item.set("attemptsLeft", Node.AttemptsLeft); Nodes.push_back(std::move(Item));
    }
    for (const auto& Text : Run.History) History.emplace_back(Text);
    for (const auto& Receipt : Run.Receipts) {
        json::Value Item(json::Object{}); Item.set("requestId", Receipt.RequestId); Item.set("signature", Receipt.Signature);
        Receipts.push_back(std::move(Item));
    }
    Output.set("resources", json::Value(std::move(Resources))); Output.set("pillDraws", json::Value(std::move(Draws)));
    Output.set("rng", json::Value(std::move(RandomStates))); Output.set("nodes", json::Value(std::move(Nodes)));
    Output.set("history", json::Value(std::move(History))); Output.set("receipts", json::Value(std::move(Receipts)));
    Output.set("qi", Run.Qi); Output.set("qiInitial", Run.QiInitial); Output.set("positionId", Run.PositionId);
    Output.set("coreAvailable", Run.CoreAvailable); Output.set("lastFailedId", Run.LastFailedId);
    Output.set("lastFailurePositionId", Run.LastFailurePositionId); Output.set("hasBuff", Run.HasBuff);
    Output.set("buffTargetId", Run.BuffTargetId); Output.set("breakthroughFailureLoss", Run.FailureLoss);
    return Output;
}
bool mfZhujiFromJson(const json::Value& Input, ZhujiRun& Output, std::string& Error) {
    const auto Fail = [&](const std::string& Reason) { Error = Reason; return false; };
    static const std::set<std::string> Keys = {"rulesVersion", "route", "status", "revision", "settled", "pills", "core", "daoBody", "steady", "elementRoot",
        "score", "rawScore", "round", "actionPoints", "pendingDrawCount", "converted", "gathered", "resources", "pillDraws", "rng", "nodes", "history", "receipts",
        "qi", "qiInitial", "positionId", "coreAvailable", "lastFailedId", "lastFailurePositionId", "hasBuff", "buffTargetId", "breakthroughFailureLoss"};
    if (!mfKeys(Input, Keys)) return Fail("筑基存档字段缺失或含未知字段。");
    for (const char* Key : {"settled", "core", "daoBody", "steady", "converted", "gathered", "coreAvailable", "hasBuff"})
        if (!Input[Key].isBool()) return Fail("筑基布尔字段无效。");
    for (const char* Key : {"rulesVersion", "revision", "pills", "score", "rawScore", "round", "actionPoints", "pendingDrawCount", "qi", "qiInitial"})
        if (!mfInteger(Input[Key], 0, 100000)) return Fail("筑基整数无效。");
    if (!mfInteger(Input["elementRoot"], -1, 4)) return Fail("筑基灵根无效。");
    for (const char* Key : {"route", "status", "positionId", "lastFailedId", "lastFailurePositionId", "buffTargetId"})
        if (!Input[Key].isString() || Input[Key].asString().size() > 100) return Fail("筑基ID无效。");
    if (!Input["breakthroughFailureLoss"].isNumber() || !std::isfinite(Input["breakthroughFailureLoss"].asNumber()) ||
        Input["breakthroughFailureLoss"].asNumber() < 0 || Input["breakthroughFailureLoss"].asNumber() > 1) return Fail("筑基失败损失无效。");
    for (const char* Key : {"resources", "pillDraws", "rng", "nodes", "history", "receipts"})
        if (!Input[Key].isArray()) return Fail("筑基数组无效。");
    if (Input["resources"].asArray().size() != 5 || Input["rng"].asArray().size() != 4 || Input["nodes"].asArray().size() != 36 ||
        Input["pillDraws"].asArray().size() > 12 || Input["history"].asArray().size() > 256 || Input["receipts"].asArray().size() > 256) return Fail("筑基数组长度无效。");
    ZhujiRun Next;
    if (Input["route"].asString() == "elements") Next.Route = ZhujiRoute::Elements;
    else if (Input["route"].asString() == "meridians") Next.Route = ZhujiRoute::Meridians;
    else return Fail("未知筑基路线。");
    const auto& Status = Input["status"].asString();
    if (Status == "preparing") Next.Status = ZhujiStatus::Preparing;
    else if (Status == "active") Next.Status = ZhujiStatus::Active;
    else if (Status == "success") Next.Status = ZhujiStatus::Success;
    else if (Status == "failed") Next.Status = ZhujiStatus::Failed;
    else if (Status == "withdrawn") Next.Status = ZhujiStatus::Withdrawn;
    else return Fail("未知筑基状态。");
    Next.RulesVersion = Input["rulesVersion"].asInt(); Next.Revision = Input["revision"].asInt(); Next.Settled = Input["settled"].asBool();
    Next.PillCount = Input["pills"].asInt(); Next.PreparedCore = Input["core"].asBool(); Next.DaoBody = Input["daoBody"].asBool();
    Next.Steady = Input["steady"].asBool(); Next.ElementRoot = Input["elementRoot"].asInt(); Next.Score = Input["score"].asInt();
    Next.RawScore = Input["rawScore"].asInt(); Next.Round = Input["round"].asInt(); Next.ActionPoints = Input["actionPoints"].asInt();
    Next.PendingDrawCount = Input["pendingDrawCount"].asInt(); Next.Converted = Input["converted"].asBool(); Next.Gathered = Input["gathered"].asBool();
    Next.Qi = Input["qi"].asInt(); Next.QiInitial = Input["qiInitial"].asInt(); Next.PositionId = Input["positionId"].asString();
    Next.CoreAvailable = Input["coreAvailable"].asBool(); Next.LastFailedId = Input["lastFailedId"].asString();
    Next.LastFailurePositionId = Input["lastFailurePositionId"].asString(); Next.HasBuff = Input["hasBuff"].asBool();
    Next.BuffTargetId = Input["buffTargetId"].asString(); Next.FailureLoss = Input["breakthroughFailureLoss"].asNumber();
    for (int Index = 0; Index < 5; ++Index) {
        if (!mfInteger(Input["resources"][Index], 0, 64)) return Fail("五行资源无效。");
        Next.Resources[Index] = Input["resources"][Index].asInt();
    }
    for (int Index = 0; Index < 4; ++Index) {
        if (!mfInteger(Input["rng"][Index], 0, 4294967295.0)) return Fail("筑基随机状态无效。");
        Next.RandomStates[Index] = static_cast<std::uint32_t>(Input["rng"][Index].asNumber());
    }
    for (const auto& Value : Input["pillDraws"].asArray()) {
        if (!mfInteger(Value, 0, 4)) return Fail("丹药抽气无效。");
        Next.PillDraws.push_back(Value.asInt());
    }
    for (int Index = 0; Index < 36; ++Index) {
        const auto& Node = Input["nodes"][Index];
        if (!mfKeys(Node, {"opened", "revealed", "attemptsLeft"}) || !Node["opened"].isBool() || !Node["revealed"].isBool() ||
            !mfInteger(Node["attemptsLeft"], 0, 3)) return Fail("穴位记录无效。");
        Next.Nodes[Index] = {Node["opened"].asBool(), Node["revealed"].asBool(), Node["attemptsLeft"].asInt()};
    }
    for (const auto& Value : Input["history"].asArray()) {
        if (!Value.isString() || Value.asString().size() > 1024) return Fail("筑基记录无效。");
        Next.History.push_back(Value.asString());
    }
    std::set<std::string> RequestIds;
    for (const auto& Value : Input["receipts"].asArray()) {
        if (!mfKeys(Value, {"requestId", "signature"}) || !Value["requestId"].isString() || !Value["signature"].isString() ||
            Value["requestId"].asString().empty() || Value["requestId"].asString().size() > 100 ||
            Value["signature"].asString().size() > 4096 || !RequestIds.insert(Value["requestId"].asString()).second) return Fail("筑基动作收据无效。");
        Next.Receipts.push_back({Value["requestId"].asString(), Value["signature"].asString()});
    }
    if (const auto Reason = mfZhujiRunError(Next); !Reason.empty()) return Fail(Reason);
    Output = std::move(Next); Error.clear(); return true;
}
}
