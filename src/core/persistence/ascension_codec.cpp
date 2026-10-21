#include "core/persistence/ascension_codec.h"

#include <cmath>
#include <set>

namespace nz::persistence {
namespace {
bool mfTournamentInteger(const json::Value& Value, int Minimum, int Maximum) {
    return Value.isNumber() && std::isfinite(Value.asNumber()) &&
        Value.asNumber() == std::floor(Value.asNumber()) &&
        Value.asNumber() >= Minimum && Value.asNumber() <= Maximum;
}
} // namespace

json::Value mfAscensionTournamentToJson(const AscensionTournamentState& State) {
    json::Value Output(json::Object{});
    Output.set("firstYear", State.FirstYear);
    Output.set("offeredYear", State.OfferedYear);
    Output.set("attemptedYear", State.AttemptedYear);
    Output.set("round", State.Round);
    Output.set("active", State.Active);
    json::Array Sects;
    for (const auto& SectId : State.OpponentSects) Sects.emplace_back(SectId);
    Output.set("opponentSects", json::Value(std::move(Sects)));
    return Output;
}

bool mfAscensionTournamentShapeValid(const json::Value& Input) {
    static const std::set<std::string> Keys = {
        "firstYear", "offeredYear", "attemptedYear", "round", "active", "opponentSects"};
    if (!Input.isObject() || Input.asObject().size() != Keys.size()) return false;
    for (const auto& [Key, Value] : Input.asObject()) if (!Keys.contains(Key)) return false;
    for (const auto* Key : {"firstYear", "offeredYear", "attemptedYear"})
        if (!mfTournamentInteger(Input[Key], -1, 100000)) return false;
    if (!mfTournamentInteger(Input["round"], 0, 4) || !Input["active"].isBool() ||
        !Input["opponentSects"].isArray() || Input["opponentSects"].asArray().size() > 3) return false;
    for (const auto& SectId : Input["opponentSects"].asArray())
        if (!SectId.isString() || SectId.asString().size() > 32) return false;
    return true;
}

bool mfAscensionTournamentFromJson(const json::Value& Input, AscensionTournamentState& Output,
    std::string& Error) {
    if (!mfAscensionTournamentShapeValid(Input)) {
        Error = "升仙大会存档字段、数值或类型无效。";
        return false;
    }
    AscensionTournamentState Candidate;
    Candidate.FirstYear = Input["firstYear"].asInt();
    Candidate.OfferedYear = Input["offeredYear"].asInt();
    Candidate.AttemptedYear = Input["attemptedYear"].asInt();
    Candidate.Round = Input["round"].asInt();
    Candidate.Active = Input["active"].asBool();
    for (const auto& SectId : Input["opponentSects"].asArray())
        Candidate.OpponentSects.push_back(SectId.asString());
    Output = std::move(Candidate);
    Error.clear();
    return true;
}
} // namespace nz::persistence
