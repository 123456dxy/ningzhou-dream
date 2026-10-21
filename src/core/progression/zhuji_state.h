#pragma once
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace nz {
enum class ZhujiRoute { Elements, Meridians };
enum class ZhujiStatus { Preparing, Active, Success, Failed, Withdrawn };

// Progression owns this value snapshot. Game commands run serially; queries only borrow it.
// Persistent local RNG is private to this attempt and never returned in a public view.
struct ZhujiNodeState {
    bool Opened = false;
    bool Revealed = false;
    int AttemptsLeft = 0;
};
struct ZhujiReceipt {
    std::string RequestId;
    std::string Signature;
};
struct ZhujiRun {
    int RulesVersion = 1;
    ZhujiRoute Route = ZhujiRoute::Elements;
    ZhujiStatus Status = ZhujiStatus::Active;
    int Revision = 0;
    bool Settled = false;
    int PillCount = 0;
    bool PreparedCore = false;
    bool DaoBody = false;
    bool Steady = false;
    int ElementRoot = -1;
    int Score = 0;
    int RawScore = 0;
    int Round = 1;
    int ActionPoints = 3;
    int PendingDrawCount = 0;
    bool Converted = false;
    bool Gathered = false;
    std::array<int, 5> Resources{};
    std::vector<int> PillDraws;
    std::array<std::uint32_t, 4> RandomStates{};
    int Qi = 100;
    int QiInitial = 100;
    std::string PositionId = "start";
    std::array<ZhujiNodeState, 36> Nodes{};
    bool CoreAvailable = false;
    std::string LastFailedId;
    std::string LastFailurePositionId;
    bool HasBuff = false;
    std::string BuffTargetId;
    double FailureLoss = 0.20;
    std::vector<std::string> History;
    std::vector<ZhujiReceipt> Receipts;
};
struct ZhujiAction {
    std::string Kind;
    std::vector<int> Spend;
    std::string TargetId;
    std::optional<std::vector<int>> Keep;
};
struct ZhujiQuote {
    bool Enabled = false;
    std::string Reason;
    int QiCost = 0;
    int SuccessPercent = 100;
    int Points = 0;
    int NextDrawCount = 0;
    int ScoreAfter = 0;
    std::string Effect;
};
}
