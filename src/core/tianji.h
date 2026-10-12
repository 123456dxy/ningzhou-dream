// 天机大比：账号所属的服务端留影、挑战记录和荣誉榜。绝不写入可导入的人生存档。
#pragma once
#include "core/http.h"
#include "core/json.h"
#include "core/model.h"
#include <map>

namespace nz::tianji {
inline constexpr int SNAPSHOT_VERSION = 1;
inline constexpr int RULES_VERSION = 1;
inline constexpr int BOARD_REFRESH_SECONDS = 30;
struct Window {
    int year = 1, eventId = 0, startYear = 0, endYear = 0, nextStartYear = 50;
    bool open = false;
};
Window windowFor(const Game& game);
// Copy only permanent combat inputs. Temporary preparation, inventory, NPCs and world are excluded.
bool freezePlayer(const Player& source, Player& frozen, std::string& error);
json::Value buildView(const Player& player);

class Store {
public:
    explicit Store(const std::string& directory);
    bool healthy() const { return error_.empty(); }
    const std::string& error() const { return error_; }
    bool active(const std::string& owner) const;
    // Monotone closure of entries as the owner's displayed year passes the window.
    bool observe(const std::string& owner, const Game* source);
    json::Value hint(const std::string& owner, const Game* source) const;
    json::Value status(const std::string& owner, const Game* source) const;
    json::Value board(long long now);
    http::Response handle(const http::Request& request, const std::string& owner,
                          const std::string& nickname, const Game* source, long long now);
private:
    struct Attempt { std::string result, requestId; };
    struct Entry {
        std::string id, owner, nickname, sourceLife;
        int eventId = 0, registeredYear = 0, group = 0;
        long long createdAt = 0;
        bool closed = false;
        Game snapshot;
        std::map<std::string, Attempt> attempts;
    };
    struct Encounter {
        std::string id, owner, entryId, targetId, requestId, result;
        int revision = 0, scoreDelta = 0;
        long long startedAt = 0, finishedAt = 0;
        Game game;
        std::map<std::string, std::string> receipts;
    };
    std::string directory_, error_;
    unsigned nextId_ = 1;
    std::map<std::string, Entry> entries_;
    std::map<std::string, Encounter> encounters_; // one most recent encounter per owner
    long long boardAt_ = 0;
    json::Value boardCache_;
    Entry* entryFor(const std::string& owner, int eventId);
    const Entry* entryFor(const std::string& owner, int eventId) const;
    std::string eligibility(const std::string& owner, const Game* source) const;
    json::Value entryView(const Entry& entry, bool build) const;
    json::Value encounterView(const Encounter& encounter) const;
    void settle(Encounter& encounter, long long now);
    bool persist(bool terminalReserve = false);
    void load();
    json::Value serialize() const;
};
} // namespace nz::tianji
