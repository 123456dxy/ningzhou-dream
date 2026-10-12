// Disposable, deterministic Store::handle benchmark. No production data or private API.
// Build: g++ -std=c++20 -O2 -I src -I . tools/tianji-rollback-benchmark.cpp build/libnzcore.a -pthread -o build/tianji-rollback-benchmark
// Run from any directory. Each run creates/removes its own temporary fixtures.
// Heap metrics count C++ allocation payloads (not allocator overhead/RSS), with the
// same allocator instrumentation for both versions. File I/O and response rendering
// are included. Setup, load, and fixture generation are excluded from measurements.
#include "core/tianji.h"
#include "core/battle.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>
#include <stdexcept>

namespace allocation {
struct alignas(std::max_align_t) Header { std::size_t bytes; };
std::size_t live = 0, peak = 0, volume = 0;
bool enabled = false;
void* allocate(std::size_t bytes) {
    auto* header = static_cast<Header*>(std::malloc(sizeof(Header) + bytes));
    if (!header) throw std::bad_alloc();
    header->bytes = bytes; live += bytes;
    if (enabled) { volume += bytes; peak = std::max(peak, live); }
    return header + 1;
}
void release(void* pointer) noexcept {
    if (!pointer) return;
    auto* header = static_cast<Header*>(pointer) - 1;
    live -= header->bytes; std::free(header);
}
}
void* operator new(std::size_t bytes) { return allocation::allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocation::allocate(bytes); }
void operator delete(void* pointer) noexcept { allocation::release(pointer); }
void operator delete[](void* pointer) noexcept { allocation::release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { allocation::release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { allocation::release(pointer); }

namespace {
using namespace nz;
using json::Value;
namespace fs = std::filesystem;
Value object() { return Value(json::Object{}); }
Value array() { return Value(json::Array{}); }
std::string owner(int number) { std::ostringstream s; s << std::hex << std::setw(8) << std::setfill('0') << number + 1; return s.str(); }
http::Response call(tianji::Store& store, const Game& game, const std::string& route, const Value& body) {
    http::Request request; request.method = "POST"; request.path = "/api/tianji/" + route; request.body = body.dump();
    return store.handle(request, owner(0), "benchmark", &game, 1000);
}
void write(const fs::path& file, const std::string& bytes) { std::ofstream out(file); out << bytes; if (!out) throw std::runtime_error("fixture write failed"); }
struct Sample { double us; std::size_t peak, allocated; };
Sample measure(tianji::Store& store, const Game& game, const http::Request& request, int status) {
    const auto base = allocation::live; allocation::peak = base; allocation::volume = 0; allocation::enabled = true;
    const auto start = std::chrono::steady_clock::now();
    const auto response = store.handle(request, owner(0), "benchmark", &game, 1000);
    const auto stop = std::chrono::steady_clock::now();
    allocation::enabled = false;
    if (response.status != status) throw std::runtime_error("unexpected HTTP " + std::to_string(response.status) + ": " + response.body);
    return {std::chrono::duration<double, std::micro>(stop-start).count(), allocation::peak-base, allocation::volume};
}
void report(int count, const std::string& name, std::vector<Sample> samples) {
    std::sort(samples.begin(), samples.end(), [](const auto& a, const auto& b) { return a.us < b.us; });
    const auto& s = samples[samples.size()/2];
    std::cout << count << ',' << name << ',' << samples.size() << ',' << std::fixed << std::setprecision(2) << s.us << ',' << s.peak << ',' << s.allocated << '\n';
}
}
int main() {
    try {
        const auto root = fs::temp_directory_path() / ("nz-tianji-bench-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(root / "seed");
        Game source = createGame(); source.player.name = "基准修士"; source.player.stage = 3; source.player.lifespan = 200;
        source.year = 49; source.clock.days = 49 * 360; source.player.age = source.clock.ageBase + 49;
        source.pending.reset(); source.job.reset(); source.player.arts.battle.reset(); source.dungeons.run.reset();
        {
            tianji::Store seed((root / "seed").string());
            auto join = object(); join.set("requestId", "join");
            if (call(seed, source, "join", join).status != 200) throw std::runtime_error("seed registration failed");
        }
        std::ifstream input(root / "seed" / "store.json");
        auto base = Value::parse(std::string(std::istreambuf_iterator<char>(input), {}));
        const auto snapshot = base["entries"][0];
        Player frozen; std::string why;
        if (!tianji::freezePlayer(source.player, frozen, why)) throw std::runtime_error(why);
        Game duel; battleStartShadow(duel, frozen, frozen, 123456u);
        // Representative retained history: 32 log lines and 16 receipts, well below caps.
        const auto originalLog = duel.player.arts.battle->log;
        while (duel.player.arts.battle->log.size() < 32)
            duel.player.arts.battle->log.push_back(originalLog[duel.player.arts.battle->log.size() % originalLog.size()]);
        const auto savedDuel = Value::parse(saveToJson(duel));
        std::cout << "entries,operation,repetitions,median_us,peak_extra_heap_bytes,allocated_bytes\n";
        for (const int count : {10, 100, 512}) {
            const auto dir = root / std::to_string(count); fs::create_directories(dir);
            auto fixture = base, entries = array(), encounters = array(); fixture.set("nextId", count * 2 + 1);
            for (int i = 0; i < count; ++i) {
                const auto id = "shadow-" + std::to_string(i+1), target = "shadow-" + std::to_string((i+1)%count+1);
                auto entry = snapshot; entry.set("id", id); entry.set("owner", owner(i));
                auto attempts = object(), attempt = object(); attempt.set("result", "pending"); attempt.set("requestId", "challenge"); attempts.set(target, attempt); entry.set("attempts", attempts); entries.push(entry);
                auto e = object(); e.set("id", "duel-" + std::to_string(count+i+1)); e.set("owner", owner(i)); e.set("entryId", id); e.set("targetId", target); e.set("requestId", "challenge");
                e.set("result", ""); e.set("revision", 16); e.set("scoreDelta", 0); e.set("startedAt", 1000); e.set("finishedAt", 0); e.set("game", savedDuel);
                auto receipts = object(); for (int r = 0; r < 16; ++r) { auto receipt = object(); receipt.set("requestId", "history-" + std::to_string(r)); receipt.set("encounterId", e["id"]); receipt.set("expectedRevision", r); receipt.set("id", "end"); receipts.set("history-" + std::to_string(r), receipt.dump()); }
                e.set("receipts", receipts); encounters.push(e);
            }
            fixture.set("entries", entries); fixture.set("encounters", encounters); const auto bytes = fixture.dump(); write(dir / "store.json", bytes);
            std::cerr << "fixture entries=" << count << " encounters=" << count << " bytes=" << bytes.size() << '\n';
            tianji::Store store(dir.string()); if (!store.healthy()) throw std::runtime_error(store.error());
            auto action = object(); action.set("requestId", "measure"); action.set("encounterId", "duel-"+std::to_string(count+1)); action.set("expectedRevision", 16); action.set("id", "end");
            auto request = [&](const Value& body) { http::Request r; r.method="POST"; r.path="/api/tianji/action"; r.body=body.dump(); return r; };
            auto stale = action; stale.set("expectedRevision", 15); const auto staleRequest = request(stale);
            std::vector<Sample> samples;
            for (int i=0; i<101; ++i) samples.push_back(measure(store, source, staleRequest, 409));
            report(count,"stale_rejection",samples);
            auto illegal = action; illegal.set("id", "unknown-card"); const auto illegalRequest = request(illegal); samples.clear();
            for (int i=0; i<31; ++i) samples.push_back(measure(store, source, illegalRequest, 400));
            report(count,"illegal_action",samples);
            // Atomic rename failure after serialization, confined to this fixture only.
            fs::rename(dir / "store.json", dir / "store.backup"); fs::create_directory(dir / "store.json"); samples.clear();
            for (int i=0; i<7; ++i) samples.push_back(measure(store, source, request(action), 503));
            report(count,"turn_rollback",samples);
            auto terminal = action; terminal.set("id", "forfeit"); const auto terminalRequest = request(terminal); samples.clear();
            for (int i=0; i<7; ++i) samples.push_back(measure(store, source, terminalRequest, 503));
            report(count,"terminal_rollback",samples);
            fs::remove(dir / "store.json"); fs::rename(dir / "store.backup", dir / "store.json");
            for (const auto& operation : {std::pair{"turn_commit", request(action)}, std::pair{"terminal_commit", terminalRequest}}) {
                samples.clear();
                for (int i=0; i<7; ++i) {
                    write(dir / "store.json", bytes); tianji::Store fresh(dir.string()); if (!fresh.healthy()) throw std::runtime_error(fresh.error());
                    samples.push_back(measure(fresh, source, operation.second, 200));
                }
                report(count,operation.first,samples);
            }
        }
        fs::remove_all(root);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
