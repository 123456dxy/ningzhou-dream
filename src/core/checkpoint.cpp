#include "core/checkpoint.h"

#include <algorithm>
#include <limits>

#include "core/save.h"
#include "core/sim.h"

namespace nz {
namespace {

bool alive(const Game& g) {
    return !g.ended && g.player.health > 0 && g.player.age < g.player.lifespan;
}

bool validWork(const Game& g) {
    return !g.job || g.job->remaining > 0;
}

int elapsedDecade(const Game& g) {
    return std::max(0, g.clock.days / 3600) * 10;
}

string checkpointSnapshot(const Game& g) {
    Game snapshot = g;
    // 只留一份状态，绝不能让一次自动存档包住上次完整自动存档。
    snapshot.checkpoint = AutoCheckpoint{};
    return saveToJson(snapshot);
}

} // namespace

void checkpointEnable(Game& g) {
    if (g.checkpoint.enabled && !g.checkpoint.snapshot.empty()) return;
    if (!alive(g) || !validWork(g)) return;
    AutoCheckpoint next = g.checkpoint;
    next.enabled = true;
    next.days = g.clock.days;
    // 首次打开较晚年份的旧存档，从当前十年段开始计时。
    next.lastAutoYear = elapsedDecade(g);
    next.slot = 4;
    next.source = "auto";
    next.snapshot = checkpointSnapshot(g);
    g.checkpoint = std::move(next);
}

bool checkpointTick(Game& g) {
    if (!g.checkpoint.enabled || !alive(g) || !validWork(g) ||
        g.player.arts.battle || g.pending) return false;
    const int decade = elapsedDecade(g);
    if (decade <= g.checkpoint.lastAutoYear) return false;
    const string snapshot = checkpointSnapshot(g);
    g.checkpoint.snapshot = snapshot;
    g.checkpoint.days = g.clock.days;
    g.checkpoint.lastAutoYear = decade;
    g.checkpoint.slot = 4;
    g.checkpoint.source = "auto";
    return true;
}

void checkpointManual(Game& g, int slot) {
    if (slot < 1 || slot > 3 || !alive(g) || !validWork(g)) return;
    if (!g.checkpoint.enabled) checkpointEnable(g);
    if (!g.checkpoint.enabled) return;
    g.checkpoint.snapshot = checkpointSnapshot(g);
    g.checkpoint.days = g.clock.days;
    g.checkpoint.slot = slot;
    g.checkpoint.source = "manual";
}

bool checkpointRestore(Game& g, const string& reason) {
    if (!g.checkpoint.enabled || g.checkpoint.snapshot.empty()) return false;
    Game restored;
    string error;
    if (!loadFromJson(g.checkpoint.snapshot, restored, &error) ||
        !alive(restored) || !validWork(restored) || restored.lifeId != g.lifeId ||
        restored.clock.days != g.checkpoint.days || restored.clock.days > g.clock.days) return false;
    AutoCheckpoint recent = g.checkpoint;
    recent.restores = static_cast<int>(std::min(static_cast<long long>(std::numeric_limits<int>::max()),
                                               static_cast<long long>(recent.restores) + 1));
    recent.lastReason = reason;
    restored.checkpoint = std::move(recent);
    g = std::move(restored);
    logEvent(g, "self", "因「" + reason + "」恢复到最近存档，回到入道第" +
                       std::to_string(g.year + 1) + "年的进度。", true);
    return true;
}

} // namespace nz
