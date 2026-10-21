#include "core/progression/zhuji_rules.h"
#include "core/progression/zhuji_board.h"
#include "core/model.h"
#include "core/sim.h"
#include "core/talents.h"
#include "core/combat/battle_scaling.h"
#include <algorithm>
#include <queue>
#include <stdexcept>

namespace nz {
namespace {
constexpr int Generates[5] = {2, 3, 1, 4, 0};
constexpr int NaturalCounts[8] = {6, 6, 8, 4, 6, 8, 4, 6};
bool mfHas(const std::vector<std::string>& Traits, const std::string& Id) {
    return std::find(Traits.begin(), Traits.end(), Id) != Traits.end();
}
void mfNote(ZhujiRun& Run, const std::string& Text) {
    Run.History.push_back(Text);
    if (Run.History.size() > 256) Run.History.erase(Run.History.begin());
}
int mfDrawElement(std::uint32_t& State) {
    std::uint32_t Word;
    do {
        State += 0x9e3779b9u;
        Word = (State ^ (State >> 16)) * 0x21f0aaadu;
        Word = (Word ^ (Word >> 15)) * 0x735a2d97u;
        Word ^= Word >> 15;
    } while (Word == 0xffffffffu);
    return static_cast<int>(Word % 5);
}
int mfResourceCount(const ZhujiRun& Run) {
    int Count = 0;
    for (int Value : Run.Resources) Count += Value;
    return Count;
}
void mfReceiveElements(ZhujiRun& Run) {
    for (int Count = 0; Count < NaturalCounts[Run.Round - 1]; ++Count)
        ++Run.Resources[mfDrawElement(Run.RandomStates[1])];
    if (Run.PreparedCore && Run.Round == 4)
        for (int Count = 0; Count < 4; ++Count) ++Run.Resources[mfDrawElement(Run.RandomStates[2])];
    for (int Count = 0; Count < Run.PendingDrawCount; ++Count)
        ++Run.Resources[mfDrawElement(Run.RandomStates[3])];
    Run.PendingDrawCount = 0;
}
bool mfEdgeOpen(const ZhujiRun& Run, const ZhujiEdge& Edge) {
    const int UnlockIndex = mfZhujiNodeIndex(Edge.UnlockedBy);
    return UnlockIndex < 0 || Run.Nodes[UnlockIndex].Opened;
}
std::vector<int> mfNeighborsAt(const ZhujiRun& Run, const std::string& Position) {
    std::vector<int> Result;
    for (const auto& Edge : mfZhujiEdges()) {
        if (!mfEdgeOpen(Run, Edge)) continue;
        if (Position == Edge.From) Result.push_back(mfZhujiNodeIndex(Edge.To));
        else if (Position == Edge.To) Result.push_back(mfZhujiNodeIndex(Edge.From));
    }
    return Result;
}
void mfReveal(ZhujiRun& Run, int Origin, int Depth) {
    std::array<int, 36> Distances;
    Distances.fill(-1);
    std::queue<int> Frontier;
    Distances[Origin] = 0;
    Frontier.push(Origin);
    while (!Frontier.empty()) {
        const int Current = Frontier.front(); Frontier.pop();
        Run.Nodes[Current].Revealed = true;
        if (Distances[Current] == Depth) continue;
        for (int Neighbor : mfNeighborsAt(Run, mfZhujiNodes()[Current].Id)) {
            if (Distances[Neighbor] >= 0) continue;
            Distances[Neighbor] = Distances[Current] + 1;
            Frontier.push(Neighbor);
        }
    }
}
bool mfAllGates(const ZhujiRun& Run) {
    for (const char* Gate : {"ren", "du", "chong"}) {
        bool Opened = false;
        for (int Index = 0; Index < 36; ++Index)
            if (Gate == std::string(mfZhujiNodes()[Index].GateId) && Run.Nodes[Index].Opened) Opened = true;
        if (!Opened) return false;
    }
    return true;
}
std::string mfSpend(ZhujiRun& Run, const std::vector<int>& Tokens, int Count, std::array<int, 5>& Payment) {
    if (static_cast<int>(Tokens.size()) != Count) return "所选灵气数量不正确。";
    for (int Element : Tokens) {
        if (Element < 0 || Element >= 5) return "未知五行。";
        ++Payment[Element];
    }
    for (int Element = 0; Element < 5; ++Element)
        if (Payment[Element] > Run.Resources[Element]) return "所选灵气不足。";
    for (int Element = 0; Element < 5; ++Element) Run.Resources[Element] -= Payment[Element];
    return "";
}
std::string mfElementsAction(ZhujiRun& Run, const ZhujiAction& Action) {
    const auto& Kind = Action.Kind;
    if (Kind == "withdraw") { Run.Status = ZhujiStatus::Withdrawn; mfNote(Run, "收功离关，本次没有永久奖励，准备与闭关时间不退。"); return ""; }
    if (Kind == "seal") {
        if (Run.Score < 60) return "至少60分才能定型。";
        Run.Status = ZhujiStatus::Success; mfNote(Run, "五行道基永久定型，不可补修。"); return "";
    }
    if (Kind == "next") {
        if (Run.Round == 8) {
            Run.Status = Run.Score >= 60 ? ZhujiStatus::Success : ZhujiStatus::Failed;
            mfNote(Run, Run.Score >= 60 ? "八轮功成，道基永久定型。" : "八轮结束，凝练不足60分，本次未成。");
            return "";
        }
        const int Limit = Run.DaoBody ? 10 : 8;
        if (Action.Keep) {
            if (static_cast<int>(Action.Keep->size()) > Limit) return "保留灵气超过跨轮上限。";
            std::array<int, 5> Payment{};
            if (auto Error = mfSpend(Run, *Action.Keep, static_cast<int>(Action.Keep->size()), Payment); !Error.empty()) return Error;
            Run.Resources = Payment;
        } else if (mfResourceCount(Run) > Limit) return "余气超过上限，请明确选择保留量。";
        ++Run.Round; Run.ActionPoints = 3; Run.Converted = false; Run.Gathered = false;
        mfReceiveElements(Run);
        mfNote(Run, "进入第" + std::to_string(Run.Round) + "轮，随机灵气已抽定。");
        return "";
    }
    if (Run.ActionPoints <= 0) return "本轮操作已用尽，请进入下一轮或定型。";
    std::array<int, 5> Payment{};
    int Count = 0, Gain = 0;
    if (Kind == "basic") { Count = 2; Gain = 3; }
    else if (Kind == "same") { Count = 3; Gain = 6; }
    else if (Kind == "generate") { Count = 4; Gain = 8; }
    else if (Kind == "five") { Count = 5; Gain = 12; }
    else if (Kind == "gather") {
        if (Run.Gathered) return "每轮只可聚气一次。";
        if (Run.Round == 8) return "最后一轮没有下轮可聚气。";
        Count = 2;
    } else if (Kind == "convert") {
        if (Run.Converted) return "每轮只可调息一次。";
        Count = static_cast<int>(Action.Spend.size());
        if (Count != 1 && Count != 2) return "普通调息1换1，真实本系灵根可2换2。";
    } else return "未知五行操作。";
    if (auto Error = mfSpend(Run, Action.Spend, Count, Payment); !Error.empty()) return Error;
    const int Largest = *std::max_element(Payment.begin(), Payment.end());
    if ((Kind == "basic" || Kind == "same" || Kind == "gather") && Largest != Count) return "需要同系灵气。";
    if (Kind == "generate") {
        bool Valid = false;
        for (int Element = 0; Element < 5; ++Element) if (Payment[Element] == 2 && Payment[Generates[Element]] == 2) Valid = true;
        if (!Valid) return "相生凝练需要相生两系各2点。";
    }
    if (Kind == "five" && !std::all_of(Payment.begin(), Payment.end(), [](int Value) { return Value == 1; })) return "五行合一需要每系各1点。";
    if (Kind == "convert") {
        const char* Ids[] = {"gold", "wood", "water", "fire", "earth"};
        int Target = -1;
        for (int Index = 0; Index < 5; ++Index) if (Action.TargetId == Ids[Index]) Target = Index;
        if (Target < 0 || Payment[Target]) return "调息目标必须是不同的合法五行。";
        if (Count == 2 && (Run.ElementRoot != Target || Largest != 2)) return "2换2需要真实本系灵根与同系来源。";
        Run.Resources[Target] += Count; Run.Converted = true;
    }
    if (Kind == "gather") { Run.PendingDrawCount += 3; Run.Gathered = true; }
    Run.Score = std::min(120, Run.Score + Gain); Run.RawScore = Run.Score;
    --Run.ActionPoints;
    mfNote(Run, Kind == "gather" ? "聚气待发，下轮均匀随机抽3点。" : Kind == "convert" ? "调息换气完成。" : "凝练增加" + std::to_string(Gain) + "分。");
    return "";
}
ZhujiQuote mfMeridianQuote(const ZhujiRun& Run, const ZhujiAction& Action) {
    ZhujiQuote Quote;
    const auto Deny = [&](const std::string& Reason) { auto Result = Quote; Result.Reason = Reason; return Result; };
    if (Action.Kind == "withdraw") { Quote.Enabled = true; Quote.Effect = "收功离关，无永久奖励，资源与闭关时间不退。"; return Quote; }
    if (Action.Kind == "restore") {
        if (!Run.CoreAvailable) return Deny("没有可用灵核。");
        if (Run.LastFailedId.empty() || Run.LastFailurePositionId != Run.PositionId) return Deny("只能恢复刚失败且尚未离位或改冲的穴位。");
        Quote.Enabled = true; Quote.Effect = "消耗灵核恢复刚失败穴位的次数，不返还真气。"; return Quote;
    }
    if (Action.Kind == "seal") {
        if (!mfAllGates(Run)) return Deny("任、督、冲三关尚未全部贯通。");
        if (Run.PositionId != "start") return Deny("请沿已通经脉回到丹田。");
        Quote.Enabled = true; Quote.Points = 20; Quote.ScoreAfter = std::min(120, Run.RawScore + 20);
        Quote.Effect = "归元额外20分，道基永久定型，不可补修。"; return Quote;
    }
    if (Action.Kind != "strike" && Action.Kind != "move") return Deny("未知周天操作。");
    const int Target = mfZhujiNodeIndex(Action.TargetId);
    if (Target < 0) return Deny("未知穴位ID。");
    const auto& Node = mfZhujiNodes()[Target]; const auto& State = Run.Nodes[Target];
    if (!State.Revealed) return Deny("尚未照见此穴详情。");
    const auto Adjacent = mfNeighborsAt(Run, Run.PositionId);
    if (std::find(Adjacent.begin(), Adjacent.end(), Target) == Adjacent.end()) return Deny("只能前往相邻且连线已开的穴位。");
    if (Action.Kind == "move") {
        if (!State.Opened) return Deny("此穴尚未贯通。");
        Quote.QiCost = 1; Quote.Effect = "沿已通经脉移动，耗1气，不重复奖励。";
    } else {
        if (State.Opened) return Deny("此穴已经贯通，请沿经脉移动。");
        if (!State.AttemptsLeft) return Deny("此穴冲击次数已耗尽。");
        Quote.QiCost = std::max(1, Node.Cost - (Run.DaoBody ? 1 : 0));
        Quote.SuccessPercent = mfZhujiNodeSuccessPercent(Run, Target);
        Quote.Points = Node.Points;
        Quote.Effect = std::string(Node.Label) + "：" + mfZhujiNodeEffect(Target) + "失败扣气和次数，留在原穴。";
    }
    if (Run.Qi < Quote.QiCost) return Deny("真气不足以执行此操作。");
    Quote.Enabled = true; Quote.ScoreAfter = std::min(120, Run.RawScore + Quote.Points); return Quote;
}
void mfMeridianAction(ZhujiRun& Run, const ZhujiAction& Action, const ZhujiQuote& Quote) {
    Run.Qi -= Quote.QiCost;
    if (Action.Kind == "strike") {
        const int Index = mfZhujiNodeIndex(Action.TargetId); const auto& Node = mfZhujiNodes()[Index];
        if (Run.HasBuff && !Run.BuffTargetId.empty() && Run.BuffTargetId != Action.TargetId) {
            Run.HasBuff = false; Run.BuffTargetId.clear();
        }
        if (Run.HasBuff && Run.BuffTargetId.empty()) Run.BuffTargetId = Action.TargetId;
        auto& Random = Run.RandomStates[0]; Random = Random * 1664525u + 1013904223u;
        if (static_cast<double>(Random) / 4294967296.0 < Quote.SuccessPercent / 100.0) {
            Run.Nodes[Index].Opened = true; Run.PositionId = Action.TargetId; Run.RawScore += Node.Points;
            Run.Score = std::min(120, Run.RawScore); Run.LastFailedId.clear(); Run.LastFailurePositionId.clear();
            Run.HasBuff = std::string(Node.Kind) == "boost"; Run.BuffTargetId.clear();
            if (std::string(Node.Kind) == "gather") Run.Qi += 12;
            mfReveal(Run, Index, std::string(Node.Kind) == "peek" ? 2 : 1);
            mfNote(Run, std::string(Node.Label) + "贯通，得" + std::to_string(Node.Points) + "分。");
        } else {
            --Run.Nodes[Index].AttemptsLeft; Run.LastFailedId = Action.TargetId; Run.LastFailurePositionId = Run.PositionId;
            mfNote(Run, std::string(Node.Label) + "冲击未成，消耗" + std::to_string(Quote.QiCost) + "真气，剩余" + std::to_string(Run.Nodes[Index].AttemptsLeft) + "次。");
        }
    } else if (Action.Kind == "move") {
        Run.PositionId = Action.TargetId; Run.LastFailedId.clear(); Run.LastFailurePositionId.clear(); mfNote(Run, "沿已通经脉移动，耗1真气。");
    } else if (Action.Kind == "restore") {
        const int Index = mfZhujiNodeIndex(Run.LastFailedId);
        Run.Nodes[Index].AttemptsLeft = mfZhujiNodes()[Index].MaxAttempts; Run.CoreAvailable = false;
        mfNote(Run, "灵核恢复失败穴位剩余次数，真气不退。");
    } else if (Action.Kind == "seal") {
        Run.RawScore += 20; Run.Score = std::min(120, Run.RawScore); Run.Status = ZhujiStatus::Success;
        mfNote(Run, "三关周天归元，道基永久定型。");
    } else { Run.Status = ZhujiStatus::Withdrawn; mfNote(Run, "收功离关，无永久奖励，准备不退。"); }
}
}

int mfZhujiHealthReward(int Score) { return std::max(0, std::min(120, Score) - 100); }
std::string mfZhujiNodeEffect(int NodeIndex) {
    if (NodeIndex < 0 || NodeIndex >= 36) return "";
    const auto& Node = mfZhujiNodes()[NodeIndex];
    const std::string Kind = Node.Kind;
    if (Kind == "start") return "任、督、冲三关皆通后回丹田定型。";
    const std::string Points = "首次成功得" + std::to_string(Node.Points) + "分。";
    if (Kind == "peek") return Points + "照见此穴两跳以内的穴位详情。";
    if (Kind == "boost") return Points + "下一冲穴目标成功率+20个百分点，同穴重试和灵核恢复保留，成功或改冲后结束，不叠加。";
    if (Kind == "gather") return Points + "回收12真气，可超过初始真气。";
    if (Kind == "link") return Points + "打开图上虚线捷径。";
    if (Kind == "gate") return Points + "本关区任选一处主关贯通即可。";
    return Points;
}
int mfZhujiNodeSuccessPercent(const ZhujiRun& Run, int NodeIndex) {
    if (NodeIndex < 0 || NodeIndex >= 36) return 0;
    const auto& Node = mfZhujiNodes()[NodeIndex];
    return std::min(100, Node.SuccessPercent + (Run.Steady && std::string(Node.Kind) == "gate" ? 10 : 0)
        + (Run.HasBuff && (Run.BuffTargetId.empty() || Run.BuffTargetId == Node.Id) ? 20 : 0));
}
std::vector<int> mfZhujiNeighbors(const ZhujiRun& Run) { return mfNeighborsAt(Run, Run.PositionId); }
ZhujiRun mfCreateZhujiRun(ZhujiRoute Route, int PillCount, bool Core,
                         const std::vector<std::string>& Traits, std::uint32_t Seed) {
    if (PillCount < 0 || PillCount > 6) throw std::invalid_argument("筑基丹数量须为0至6。");
    if (Route != ZhujiRoute::Elements && Route != ZhujiRoute::Meridians) throw std::invalid_argument("未知筑基路线。");
    ZhujiRun Run;
    Run.Route = Route; Run.PillCount = PillCount; Run.PreparedCore = Core; Run.CoreAvailable = Core;
    Run.Qi = Run.QiInitial = 100 + PillCount * 15;
    Run.DaoBody = mfHas(Traits, "daoBody"); Run.Steady = mfHas(Traits, "steady");
    const char* Roots[] = {"gold", "wood", "water", "fire", "earth"};
    for (int Index = 0; Index < 5; ++Index) if (mfHas(Traits, Roots[Index])) Run.ElementRoot = Index;
    Run.RandomStates = {Seed, Seed ^ 0xa511e9b3u, Seed ^ 0x63d83595u, Seed ^ 0x7f4a7c15u};
    for (int Index = 0; Index < 36; ++Index) {
        Run.Nodes[Index].AttemptsLeft = mfZhujiNodes()[Index].MaxAttempts;
        Run.Nodes[Index].Revealed = std::string(mfZhujiNodes()[Index].Kind) == "gate";
    }
    Run.Nodes[0].Opened = true;
    if (Route == ZhujiRoute::Elements) {
        mfReceiveElements(Run);
        for (int Count = 0; Count < PillCount * 2; ++Count) {
            const int Element = mfDrawElement(Run.RandomStates[0]); Run.PillDraws.push_back(Element); ++Run.Resources[Element];
        }
        mfNote(Run, "五行入关，首轮灵气与丹药随机抽气已冻结。");
    } else {
        Run.RandomStates[0] = Seed; Run.Qi = Run.QiInitial = 100 + PillCount * 15;
        mfReveal(Run, 0, 2); mfNote(Run, "真气归于丹田，任、督、冲各通一关后回丹田归元。");
    }
    return Run;
}
ZhujiQuote mfQuoteZhujiAction(const ZhujiRun& Run, const ZhujiAction& Action) {
    if (Run.Status != ZhujiStatus::Active) { ZhujiQuote Quote; Quote.Reason = "本次筑基不在施术阶段，已定型的根基不能补修。"; return Quote; }
    const bool UsesSpend = Run.Route == ZhujiRoute::Elements && (Action.Kind == "basic" || Action.Kind == "same" || Action.Kind == "generate" ||
        Action.Kind == "five" || Action.Kind == "convert" || Action.Kind == "gather");
    const bool UsesTarget = Run.Route == ZhujiRoute::Elements ? Action.Kind == "convert" : Action.Kind == "strike" || Action.Kind == "move";
    if ((!UsesSpend && !Action.Spend.empty()) || (Action.Keep && (Run.Route != ZhujiRoute::Elements || Action.Kind != "next")) ||
        (!UsesTarget && !Action.TargetId.empty())) { ZhujiQuote Quote; Quote.Reason = "动作包含不适用参数。"; return Quote; }
    if (Run.Route == ZhujiRoute::Meridians) return mfMeridianQuote(Run, Action);
    ZhujiRun Copy = Run;
    ZhujiQuote Quote;
    Quote.Reason = mfElementsAction(Copy, Action);
    Quote.Enabled = Quote.Reason.empty(); Quote.ScoreAfter = Copy.Score;
    if (Quote.Enabled) Quote.Effect = Copy.History.back();
    if (Action.Kind == "next" && Run.Round < 8)
        Quote.NextDrawCount = NaturalCounts[Run.Round] + (Run.PreparedCore && Run.Round + 1 == 4 ? 4 : 0) + Run.PendingDrawCount;
    return Quote;
}
std::string mfApplyZhujiAction(ZhujiRun& Run, const ZhujiAction& Action) {
    const auto Quote = mfQuoteZhujiAction(Run, Action);
    if (!Quote.Enabled) return Quote.Reason;
    ZhujiRun Next = Run;
    if (Next.Route == ZhujiRoute::Elements) {
        if (const auto Error = mfElementsAction(Next, Action); !Error.empty()) return Error;
    } else mfMeridianAction(Next, Action, Quote);
    ++Next.Revision; Run = std::move(Next); return "";
}
std::string mfZhujiRunError(const ZhujiRun& Run) {
    if (Run.RulesVersion != 1 || Run.Revision < 0 || Run.Revision > 100000 || Run.PillCount < 0 || Run.PillCount > 6 ||
        Run.Round < 1 || Run.Round > 8 || Run.ActionPoints < 0 || Run.ActionPoints > 3 || Run.Score < 0 || Run.Score > 120 ||
        Run.RawScore < 0 || Run.RawScore > 400 || Run.PendingDrawCount < 0 || Run.PendingDrawCount > 3 ||
        Run.ElementRoot < -1 || Run.ElementRoot > 4 || Run.Qi < 0 || Run.Qi > 400 || Run.QiInitial != 100 + Run.PillCount * 15 ||
        !std::isfinite(Run.FailureLoss) || Run.FailureLoss < 0 || Run.FailureLoss > 1 || Run.History.size() > 256 || Run.Receipts.size() > 256 ||
        (Run.CoreAvailable && !Run.PreparedCore)) return "筑基状态范围无效。";
    if (Run.Route != ZhujiRoute::Elements && Run.Route != ZhujiRoute::Meridians) return "未知筑基路线。";
    if (Run.Status != ZhujiStatus::Preparing && Run.Status != ZhujiStatus::Active && Run.Status != ZhujiStatus::Success && Run.Status != ZhujiStatus::Failed && Run.Status != ZhujiStatus::Withdrawn) return "未知筑基阶段。";
    for (int Value : Run.Resources) if (Value < 0 || Value > 64) return "五行资源无效。";
    for (int Value : Run.PillDraws) if (Value < 0 || Value > 4) return "丹药抽气记录无效。";
    if (Run.Route == ZhujiRoute::Elements && Run.PillDraws.size() != static_cast<std::size_t>(Run.PillCount * 2)) return "丹药抽气数量不符。";
    if (Run.Score != std::min(120, Run.RawScore)) return "筑基原始分数不一致。";
    if (Run.Route == ZhujiRoute::Meridians && !Run.PillDraws.empty()) return "周天准备无效。";
    const int Position = mfZhujiNodeIndex(Run.PositionId);
    if (Position < 0 || !Run.Nodes[Position].Opened || !Run.Nodes[0].Opened) return "真气位置无效。";
    for (int Index = 0; Index < 36; ++Index) {
        const auto& Node = Run.Nodes[Index];
        if (Node.AttemptsLeft < 0 || Node.AttemptsLeft > mfZhujiNodes()[Index].MaxAttempts ||
            (Node.Opened && !Node.Revealed && Run.Route == ZhujiRoute::Meridians) ||
            (Index > 0 && Node.Opened && Node.AttemptsLeft == 0)) return "穴位状态无效。";
    }
    if (Run.Route == ZhujiRoute::Meridians) {
        int EarnedScore = Run.Status == ZhujiStatus::Success ? 20 : 0;
        int MaximumQi = Run.QiInitial;
        for (int Index = 0; Index < 36; ++Index) if (Run.Nodes[Index].Opened) {
            EarnedScore += mfZhujiNodes()[Index].Points;
            if (std::string(mfZhujiNodes()[Index].Kind) == "gather") MaximumQi += 12;
        }
        if (EarnedScore != Run.RawScore || Run.Qi > MaximumQi) return "周天得分或真气与已通穴位不一致。";
        std::array<bool, 36> Connected{};
        std::queue<int> Frontier;
        Connected[0] = true; Frontier.push(0);
        while (!Frontier.empty()) {
            const int Current = Frontier.front(); Frontier.pop();
            for (int Neighbor : mfNeighborsAt(Run, mfZhujiNodes()[Current].Id)) if (Run.Nodes[Neighbor].Opened && !Connected[Neighbor]) {
                Connected[Neighbor] = true; Frontier.push(Neighbor);
            }
        }
        for (int Index = 0; Index < 36; ++Index)
            if (Run.Nodes[Index].Opened && !Connected[Index]) return "已通经脉必须与丹田连通。";
    }
    if ((!Run.LastFailedId.empty() && mfZhujiNodeIndex(Run.LastFailedId) < 0) ||
        (!Run.LastFailurePositionId.empty() && mfZhujiNodeIndex(Run.LastFailurePositionId) < 0) ||
        (!Run.BuffTargetId.empty() && mfZhujiNodeIndex(Run.BuffTargetId) < 0) ||
        Run.LastFailedId.empty() != Run.LastFailurePositionId.empty() || (!Run.HasBuff && !Run.BuffTargetId.empty())) return "穴位引用无效。";
    if (!Run.LastFailedId.empty() && (Run.LastFailurePositionId != Run.PositionId || Run.Nodes[mfZhujiNodeIndex(Run.LastFailedId)].Opened))
        return "刚失败穴位与当前位置不一致。";
    if (Run.Status == ZhujiStatus::Success && (Run.Route == ZhujiRoute::Elements ? Run.Score < 60 : !mfAllGates(Run) || Run.PositionId != "start")) return "成功筑基条件无效。";
    if (Run.Settled && (Run.Status == ZhujiStatus::Preparing || Run.Status == ZhujiStatus::Active)) return "未结束筑基不能结算。";
    for (const auto& Receipt : Run.Receipts) if (Receipt.RequestId.empty() || Receipt.RequestId.size() > 100 || Receipt.Signature.size() > 4096) return "筑基动作收据无效。";
    for (const auto& Text : Run.History) if (Text.size() > 1024) return "筑基记录过长。";
    return "";
}
bool mfZhujiBlocksActions(const Game& State) {
    return State.player.Zhuji && (State.player.Zhuji->Status == ZhujiStatus::Preparing || State.player.Zhuji->Status == ZhujiStatus::Active);
}
std::string mfStartZhuji(Game& State, ZhujiRoute Route, int PillCount, bool Core) {
    const auto Info = breakthroughInfo(State);
    if (State.player.stage != 2 || (State.player.Zhuji &&
        (State.player.Zhuji->Status == ZhujiStatus::Success || !State.player.Zhuji->Settled))) return "本世无法再次开启筑基或补修根基。";
    if (State.ended || State.job || State.pending || State.player.arts.battleActive() || State.dungeons.run) return "请先结束当前行动。";
    if (!Info.ready) return "修为尚未圆满。";
    if (State.player.health < HealthGateMinimum) return "气血不足30点，请先疗养。";
    const bool PreparedPill = State.player.arts.breakBuff == "stage" && State.player.arts.BreakthroughPillStage == 2;
    if (PillCount < 0 || PillCount > (PreparedPill ? 5 : 6) || State.player.pills < PillCount || (Core && State.player.cores < 1)) return "所选丹药或灵核不足，已服准备也计入6份上限。";
    if (State.player.stones < Info.cost) return "灵石不足。";
    auto Traits = State.player.traits;
    if (!State.player.talent.empty()) Traits.push_back(State.player.talent);
    const std::uint32_t Seed = State.rng * 1664525u + 1013904223u;
    auto Run = mfCreateZhujiRun(Route, PillCount + (PreparedPill ? 1 : 0), Core, Traits, Seed);
    Run.Status = ZhujiStatus::Preparing; Run.FailureLoss = Info.failureLoss;
    Run.Revision = State.player.Zhuji ? State.player.Zhuji->Revision + 1 : 1;
    if (State.player.Zhuji) Run.Receipts = State.player.Zhuji->Receipts;
    State.rng = Seed; State.player.stones -= Info.cost; State.player.pills -= PillCount;
    if (Core) --State.player.cores;
    if (PreparedPill) { State.player.arts.breakBuff.clear(); State.player.arts.BreakthroughPillStage = -1; }
    State.player.Zhuji = std::move(Run);
    Job Preparation;
    Preparation.actionType = "breakthrough"; Preparation.name = "筑基入关准备";
    Preparation.total = Preparation.remaining = 90; Preparation.started = State.clock.days;
    Preparation.cost = Info.cost; Preparation.paid = true; Preparation.breakthroughPrepared = true;
    Preparation.breakthroughMajor = true; Preparation.breakthroughFailureLoss = Info.failureLoss;
    State.job = Preparation;
    return "";
}
void mfCompleteZhujiPreparation(Game& State) {
    if (!State.player.Zhuji || State.player.Zhuji->Status != ZhujiStatus::Preparing || State.player.stage != 2) return;
    State.player.Zhuji->Status = ZhujiStatus::Active; ++State.player.Zhuji->Revision;
    logEvent(State, "self", "九十日入关准备已成，开始" + std::string(State.player.Zhuji->Route == ZhujiRoute::Elements ? "五行凝练。" : "周天冲穴。"), true);
}
void mfSettleZhujiRun(Game& State) {
    if (!State.player.Zhuji || State.player.Zhuji->Settled) return;
    auto& Run = *State.player.Zhuji;
    if (Run.Status == ZhujiStatus::Preparing || Run.Status == ZhujiStatus::Active) return;
    auto& PlayerState = State.player;
    if (PlayerState.stage != 2) return;
    if (Run.Status == ZhujiStatus::Success) {
        ++PlayerState.stage; PlayerState.highestStage = std::max(PlayerState.highestStage, PlayerState.stage);
        PlayerState.xp = 0; State.clock.carry["xp"] = 0;
        PlayerState.lifespan = realmOf(PlayerState.stage).life + PlayerState.lifeBonus;
        PlayerState.arts.FoundationHealthBonus += mfZhujiHealthReward(Run.Score);
        PlayerState.arts.LifeboundKind = Run.Route == ZhujiRoute::Elements ? "innate" : "retain";
        PlayerState.health = std::min(mfPlayerHealthMaximum(PlayerState), PlayerState.health + 15);
        PlayerState.renown += 12; PlayerState.achievements.emplace_back(realmName(PlayerState.stage), State.year);
        talentBreakthrough(State, true);
        logEvent(State, "self", "筑基功成，根基" + std::to_string(Run.Score) + "分，永久基础气血+" + std::to_string(mfZhujiHealthReward(Run.Score)) + "，不可补修。", true);
    } else {
        PlayerState.xp = jsFloor(PlayerState.xp * (1.0 - Run.FailureLoss));
        logEvent(State, "self", "本次筑基收功，损失" + std::to_string(jsRound(Run.FailureLoss * 100)) + "%修为，准备与时间不退。", true);
    }
    Run.Settled = true;
}
}
