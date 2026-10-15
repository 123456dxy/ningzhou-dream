#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include "core/model.h"

namespace nz::BattleLab {

// 试炼选择值：仅使用后端目录 ID，无外部资源，可复制；不接受存档或任意属性。
struct Selection {
    std::string BuildId = "metal-discard";
    int Stage = 3;
    std::string OpponentId = "beast-body";
    std::uint32_t Seed = 20261014;
    std::string LoadoutTier = "initial";
};

// 对手目录值：归斗法模块，表示普通 PvE 的真实妖兽、散修或宗门传承。
struct OpponentDefinition {
    std::string Id;
    std::string Name;
    std::string Dao;
    std::string Sect;
    std::string ProfileId;
    bool IsBeast = false;
};

// 结局快照：来自真实普通斗法的胜负计数与回合；领域收口收益仅留在可丢弃内存 Game，不写任何人生存档。
struct Result {
    bool Won = false;
    int Turns = 0;
    std::string Text;
};

// 独立试炼会话拥有完整内存 Game；HTTP 层串行调用，无账号、磁盘或玩家存档引用。
// 空会话为合法初态；Revision 单调增长，失败保留整个会话与 RNG。
struct Session {
private:
    std::unique_ptr<Game> OwnedGame;

public:
    // 用途：创建合法空会话；流程：独立服务/候选初始化；用法：默认构造；返回：拥有独立 Game，分配失败不产生会话。
    Session();
    // 用途：深拷贝试炼候选；流程：动作事务；用法：传未被移走的会话；返回：独立 Game/容器，失败不改原会话。
    Session(const Session& Source);
    // 用途：强异常保证地替换会话副本；流程：测试/拥有者赋值；用法：传有效源；返回：自身引用，复制失败保持自身完整。
    Session& operator=(const Session& Source);
    // 用途：无分配转移所有权；流程：候选提交；用法：移动有效会话；返回：所有权移入，移走的对象仅可销毁或赋值重用。
    Session(Session&& Source) noexcept = default;
    // 用途：无分配转移并释放旧状态；流程：候选提交；用法：移动有效会话；返回：自身引用。
    Session& operator=(Session&& Source) noexcept = default;
    // 用途：借用试炼 Game；流程：领域动作/隔离测试；用法：有效会话调用；返回：生命周期受会话提交限制的引用，已移动对象明确抛错。
    Game& CurrentGame();
    // 用途：只读借用试炼 Game；流程：快照/报价；用法：有效会话调用；返回：借用常引用，已移动对象明确抛错。
    const Game& CurrentGame() const;

    Selection CurrentSelection;
    int Revision = 0;
    bool HasMatch = false;
    std::optional<Result> MatchResult;
    std::vector<std::string> CombatLog;
    std::vector<std::string> Unavailable;
};

// 命令结果：Succeeded 为已提交领域动作；拒绝时 Error 非空且会话不变。
struct ActionResult {
    bool Succeeded = false;
    std::string Error;
};

// 用途：读取不可变对手目录；流程：试炼配置/开局校验；用法：无参数；返回：借用静态目录，不推进 RNG。
std::span<const OpponentDefinition> mfOpponents();
// 用途：读取五个可测试大境界；流程：配置/开局校验；用法：无参数；返回：0/3/6/9/12 静态视图。
std::span<const int> mfStages();
// 用途：按合法流派、境界和种子新建普通 PvE；流程：试炼开局；用法：可附预期 Revision；返回：成功提交或拒绝原因。
// 失败保证：先校验并在新候选 Game 开战，整体无抛出交换提交；不会访问或写任何人生存档。
[[nodiscard]] ActionResult mfStart(Session& LabSession, const Selection& Requested,
                                   std::optional<int> ExpectedRevision = std::nullopt);
// 用途：执行真实手牌/结束回合；流程：独立试炼动作；用法：Action 为 spell/end，SpellUid 必须是当前手牌 UID，移除目标使用卡牌 UID。
// 兼容：旧神通 ID 交由核心选择首张有效牌；每次仅移除一张，前端统一提交明确 UID。
// 返回：成功提交或拒绝原因；副作用仅候选会话；拒绝/过期版本不改变 RNG、手牌或气血。
[[nodiscard]] ActionResult mfTurn(Session& LabSession, const std::string& Action,
                                  const std::string& SpellUid, const std::string& Target,
                                  int ExpectedRevision);

} // namespace nz::BattleLab
