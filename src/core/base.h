// 宁州一梦 · C++ 版 — 基础类型与工具
// 这一层只放最底层的定义，不依赖任何游戏规则。
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <optional>
#include <cmath>
#include <algorithm>

namespace nz {

using std::string;
using std::vector;
using std::map;
using std::optional;

// 游戏存档版本。与 JS 版 VERSION 对齐（Demo 07 对应 7）。
inline constexpr int VERSION = 7;

// ---- 随机数 ----
// JS 版用的是 LCG：rng = (rng * 1664525 + 1013904223) >>> 0
// 这里原样复刻，保证同一个种子产生同一串随机数，
// 这样 C++ 版和 JS 版的存档/演算结果可以直接对照。
struct Rng {
    uint32_t state = 0;

    explicit Rng(uint32_t seed = 20261004) : state(seed) {}

    // 返回 [0, 1)
    double next() {
        state = static_cast<uint32_t>(state * 1664525u + 1013904223u);
        return static_cast<double>(state) / 4294967296.0;
    }
};

// 把种子打散再交给 Rng。
//
// 为什么需要：LCG 的第一个输出是 seed 的线性函数，
// 于是「种子 100 / 101 / 102」会掷出几乎相同的资质（实测都是 76）。
// 玩家换种子时如果种子只差一点，就看不出任何变化。
// 先用整数哈希把高位混进低位，相邻种子才会给出无关的结果。
inline uint32_t mixSeed(uint32_t seed) {
    uint32_t h = seed + 0x9E3779B9u;
    h ^= h >> 16;
    h = static_cast<uint32_t>(h * 0x7feb352du);
    h ^= h >> 15;
    h = static_cast<uint32_t>(h * 0x846ca68bu);
    h ^= h >> 16;
    return h;
}

// ---- 数值工具 ----
inline double clamp(double n, double lo, double hi) {    return n < lo ? lo : (n > hi ? hi : n);
}
inline int clampInt(int n, int lo, int hi) {
    return n < lo ? lo : (n > hi ? hi : n);
}
// JS 的 Math.round：.5 一律向上取整（与 C 的 round 一致，但要注意负数）
inline int jsRound(double n) {
    return static_cast<int>(std::floor(n + 0.5));
}
inline int jsFloor(double n) {
    return static_cast<int>(std::floor(n));
}
inline double jsSqrt(double n) {
    return n <= 0.0 ? 0.0 : std::sqrt(n);
}

// 从数组里按均匀分布取一个元素（对应 JS 的 pick）。
template <typename T>
const T& pick(Rng& r, const vector<T>& items) {
    return items[static_cast<size_t>(r.next() * static_cast<double>(items.size()))];
}
// 取 [a, b] 闭区间整数（对应 JS 的 int）。
inline int randInt(Rng& r, int a, int b) {
    return a + static_cast<int>(r.next() * static_cast<double>(b - a + 1));
}

// ---- 结构化小类型 ----
struct ItemStack {
    string id;
    int count = 0;
};

// 一条纪事（对应 JS 的 s.logs 元素）
struct LogEntry {
    int id = 0;
    int year = 0;
    int age = 0;
    string category;   // self / npc / sect / world
    string text;
    bool important = false;
    optional<string> npcId;
    string delta;
    optional<int> days;
};

// 人物的一段经历
struct HistoryEntry {
    int year = 0;
    string text;
};

} // namespace nz
