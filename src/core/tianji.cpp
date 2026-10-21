#include "core/tianji.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <tuple>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace nz::tianji {
namespace {
using json::Value;
namespace fs = std::filesystem;
constexpr size_t MAX_ENTRIES = 512, MAX_STORE_BYTES = 32 * 1024 * 1024;
constexpr size_t MAX_SNAPSHOT_BYTES = 64 * 1024, MAX_ENCOUNTER_BYTES = 256 * 1024;
constexpr size_t MAX_STEPS = 1024;
Value obj() { return Value(json::Object{}); }
Value arr() { return Value(json::Array{}); }
Value at(long long v) { return Value(static_cast<double>(v)); }
bool identifier(const std::string& s, size_t max = 64) {
    return !s.empty() && s.size() <= max && std::all_of(s.begin(), s.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
    });
}
bool ownerId(const std::string& s) {
    return !s.empty() && s.size() <= 128 && s.size() % 2 == 0 &&
        std::all_of(s.begin(), s.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
http::Response response(int code, Value v) { http::Response r; r.status = code; r.body = v.dump(); return r; }
http::Response failure(int code, const std::string& text, const std::string& kind) {
    auto v = obj(); v.set("ok", false); v.set("error", text); v.set("code", kind); return response(code, v);
}
Value rules() {
    auto r = obj(); r.set("version", RULES_VERSION); r.set("win", 3); r.set("draw", 1); r.set("loss", 0);
    r.set("turnLimit", 24); r.set("attemptsPerTarget", 1); r.set("fullHealth", true);
    r.set("description", "每个道号每届仅保留一份报名留影，按报名时大境界分组；挑战其他真人留影，每名对手仅一次。胜3分，24回合未分胜负平局1分，败或认输0分；不发放灵石、修为或物品。榜单按积分、胜场、较少败场排序，相同成绩并列。不同届次和组别的可挑战人数不同，积分代表参与成果，不是公平实力评级。临时丹药准备不带入，双方满气血开局。");
    r.set("integrityNotice", "角色存档仍允许导入，报名构筑取自服务器当前存档并冻结；这不能证明此前养成合法，也不能阻止多个账号。荣誉榜不是严格反作弊竞技榜。");
    return r;
}
Value windowView(const Window& w) {
    auto v = obj(); v.set("year", w.year); v.set("eventId", w.eventId); v.set("startYear", w.startYear);
    v.set("endYear", w.endYear); v.set("nextStartYear", w.nextStartYear); v.set("open", w.open); return v;
}
Game snapshotGame(const Player& p) {
    Game g; g.player = p; g.clock.ageBase = p.age; g.lifeId = "tianji-snapshot-v1";
    g.checkpoint.enabled = false; return g;
}
bool atomicWrite(const fs::path& path, const std::string& data) {
    const fs::path temp = path.string() + ".tmp";
#ifndef _WIN32
    int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    size_t written = 0;
    while (written < data.size()) {
        const auto n = ::write(fd, data.data() + written, data.size() - written);
        if (n <= 0) { ::close(fd); return false; }
        written += static_cast<size_t>(n);
    }
    const bool synced = ::fsync(fd) == 0;
    const bool closed = ::close(fd) == 0;
    if (!synced || !closed) return false;
    if (::rename(temp.c_str(), path.c_str()) != 0) return false;
    // Once rename succeeds the transaction is committed; directory fsync improves crash durability.
    const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (dir >= 0) { ::fsync(dir); ::close(dir); }
    return true;
#else
    { std::ofstream f(temp, std::ios::binary | std::ios::trunc); f << data; f.flush(); if (!f) return false; }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#endif
}
std::string groupName(int group) { static const char* names[] = {"炼气", "筑基", "金丹", "元婴", "化神"}; return names[std::clamp(group, 0, 4)]; }
}

Window windowFor(const Game& game) {
    Window w; w.year = calendarDate(game).year;
    if (w.year < 50) return w;
    w.eventId = w.year / 50; w.startYear = w.eventId * 50; w.endYear = w.startYear + 9;
    w.open = w.year <= w.endYear; w.nextStartYear = w.startYear + 50; return w;
}

bool freezePlayer(const Player& s, Player& p, std::string& error) {
    auto bad = [&](const char* text) { error = text; return false; };
    if (s.stage < 0 || s.stage > 12 || s.name.empty() || s.name.size() > 128 ||
        s.traits.size() > 64 || s.arts.nodes.size() > 60 || s.arts.manuals.size() > 128 ||
        s.arts.aux.size() > 4 || s.arts.exp.size() > 12 || s.arts.equipped.size() > 3)
        return bad("构筑大小或境界无效，无法生成留影。");
    for (int n : {s.body, s.arts.mind, s.arts.hpBonus, s.arts.speed, s.arts.BaseHealthBonus, s.arts.FoundationHealthBonus})
        if (n < 0 || n > 100000) return bad("永久战斗属性超出留影支持范围。");
    std::set<std::string> unique;
    for (const auto& id : s.traits) if (!findTalent(id) || !unique.insert(id).second) return bad("天赋无效或重复。");
    unique.clear();
    for (const auto& id : s.arts.nodes) if (!findNode(id) || !unique.insert(id).second) return bad("悟道节点无效或重复。");
    for (const auto& [id, level] : s.arts.manuals) {
        ManualInfo m; if (!findManualInfo(id, m) || level < 1 || level > 5 || m.stage > s.stage) return bad("功法或修习层数无效。");
    }
    if (!s.arts.manuals.count(s.arts.main)) return bad("主修功法未修习。");
    unique.clear();
    for (const auto& id : s.arts.aux)
        if (!s.arts.manuals.count(id) || id == s.arts.main || !unique.insert(id).second) return bad("辅修功法无效或重复。");
    unique.clear(); int extras = 0;
    for (const auto& id : s.arts.spells) {
        const auto* sp = battleSpell(id);
        if (!sp || sp->stage > s.stage || !unique.insert(id).second) return bad("携带神通无效、重复或境界不足。");
        if (id != "strike" && id != "defend") ++extras;
    }
    if (extras > battleSpellSlots(s)) return bad("携带神通超出槽位。");
    for (const auto& [dao, exp] : s.arts.exp)
        if (daoIndex(dao) < 0 || exp < 0 || exp > 100000000) return bad("大道经验无效。");
    p = Player{};
    p.name = s.name; p.gender = s.gender; p.age = s.age; p.lifespan = s.lifespan;
    p.stage = s.stage; p.xp = s.xp; p.apt = s.apt; p.luck = s.luck; p.insight = s.insight;
    p.sword = s.sword; p.alchemy = s.alchemy; p.body = s.body; p.lifeBonus = s.lifeBonus;
    p.PillLifeBonus = s.PillLifeBonus;
    p.talent = s.talent; p.traits = s.traits; p.build = s.build; p.sect = s.sect;
    p.arts.focus = s.arts.focus; p.arts.exp = s.arts.exp; p.arts.nodes = s.arts.nodes;
    p.arts.manuals = s.arts.manuals; p.arts.main = s.arts.main; p.arts.aux = s.arts.aux;
    p.arts.spells = s.arts.spells; p.arts.knownSpells = s.arts.spells;
    p.arts.mind = s.arts.mind; p.arts.speed = s.arts.speed; p.arts.hpBonus = s.arts.hpBonus; p.arts.heart = s.arts.heart;
    p.arts.BaseHealthBonus = s.arts.BaseHealthBonus;
    p.arts.FoundationHealthBonus = s.arts.FoundationHealthBonus;
    p.arts.LifeboundKind = s.arts.LifeboundKind;
    p.arts.LifeboundSpell = s.arts.LifeboundSpell;
    if (!p.arts.LifeboundSpell.empty() && std::find(p.arts.knownSpells.begin(), p.arts.knownSpells.end(), p.arts.LifeboundSpell) == p.arts.knownSpells.end())
        p.arts.knownSpells.push_back(p.arts.LifeboundSpell);
    for (const auto& [slot, id] : s.arts.equipped) {
        if (id.empty()) continue;
        if (slot != "weapon" && slot != "armor" && slot != "accessory") return bad("法宝槽位无效。");
        const auto it = std::find_if(s.arts.gear.begin(), s.arts.gear.end(), [&](const Gear& g) { return g.id == id && g.slot == slot; });
        if (it == s.arts.gear.end() || id.size() > 128 || it->name.size() > 256 || it->dimensions.size() > 5 ||
            it->grade < 1 || it->grade > 6 || !std::isfinite(it->bonus) || it->bonus < 0 || it->bonus > 10)
            return bad("已装备法宝无效。");
        for (int dimension : it->dimensions) if (dimension < 0 || dimension > 100000) return bad("法宝属性无效。");
        p.arts.gear.push_back(*it); p.arts.equipped[slot] = id;
    }
    p.health = mfPlayerHealthMaximum(p);
    const auto text = saveToJson(snapshotGame(p));
    if (text.size() > MAX_SNAPSHOT_BYTES) return bad("留影内容过大。");
    Game check; std::string validation;
    if (!loadDetachedFromJson(text, check, &validation)) { error = "留影校验失败：" + validation; return false; }
    const auto ref = battleReferenceState(p);
    if (ref.max <= 0 || ref.max > 100000000 || battleEnergy(p) > 30) return bad("构筑数值超出当前斗法引擎安全范围。");
    return true;
}

Value buildView(const Player& p) {
    auto v = obj(), traits = arr(), nodes = arr(), spells = arr(), manuals = arr(), aux = arr(), gear = arr();
    for (const auto& id : p.traits) { auto x = obj(); const auto* def = findTalent(id); x.set("id", id); x.set("name", def ? def->name : id); traits.push(x); }
    auto manual = [&](const std::string& id) { auto x = obj(); ManualInfo m; findManualInfo(id, m); x.set("id", id); x.set("name", m.found ? m.name : id); auto it = p.arts.manuals.find(id); x.set("level", it == p.arts.manuals.end() ? 0 : it->second); return x; };
    for (const auto& [id, level] : p.arts.manuals) { (void)level; manuals.push(manual(id)); }
    for (const auto& id : p.arts.aux) aux.push(manual(id));
    for (const auto& id : p.arts.nodes) { auto x = obj(); const auto* n = findNode(id); x.set("id", id); x.set("name", n ? n->name : id); nodes.push(x); }
    for (const auto& id : p.arts.spells) { auto x = obj(); const auto* sp = battleSpell(id); x.set("id", id); x.set("name", sp ? sp->name : id); x.set("dao", sp ? sp->dao : ""); spells.push(x); }
    for (const auto& g : p.arts.gear) { auto x = api::equipmentView(p, g); x.set("id", g.id); x.set("name", g.name); x.set("slot", g.slot); x.set("grade", g.grade); x.set("shape", g.shape); x.set("element", g.element); x.set("polarity", g.polarity); x.set("bonus", g.bonus); auto d = arr(); for (auto n : g.dimensions) d.push(n); x.set("dimensions", d); gear.push(x); }
    auto stats = obj(); const auto ref = battleReferenceState(p);
    stats.set("stage", p.stage); stats.set("realm", realmName(p.stage)); stats.set("body", p.body); stats.set("mind", p.arts.mind);
    stats.set("speed", battleSpeed(p)); stats.set("hp", ref.max); stats.set("mana", battleEnergy(p)); stats.set("power", BATTLE_POWER[p.stage]);
    v.set("traits", traits); v.set("main", manual(p.arts.main)); v.set("aux", aux); v.set("manuals", manuals); v.set("nodes", nodes);
    v.set("spells", spells); v.set("gear", gear); v.set("stats", stats); return v;
}

Store::Store(const std::string& directory) : directory_(directory) { load(); }
Store::Entry* Store::entryFor(const std::string& owner, int eventId) {
    for (auto& [id, entry] : entries_) { (void)id; if (entry.owner == owner && entry.eventId == eventId) return &entry; } return nullptr;
}
const Store::Entry* Store::entryFor(const std::string& owner, int eventId) const {
    for (const auto& [id, entry] : entries_) { (void)id; if (entry.owner == owner && entry.eventId == eventId) return &entry; } return nullptr;
}
bool Store::active(const std::string& owner) const { auto it = encounters_.find(owner); return it != encounters_.end() && it->second.result.empty(); }
std::string Store::eligibility(const std::string& owner, const Game* source) const {
    if (!healthy()) return "天机大比记录暂不可用，请联系服务器管理员。";
    if (!source) return "请先创建或读取角色。";
    const Window w = windowFor(*source);
    if (!w.open) return "天机大比在每位修士自己的入道第50至59年、100至109年等十年窗口开放。";
    if (source->ended) return "这一世已经结束，不能报名或发起挑战。";
    if (active(owner)) return "请先完成正在进行的留影挑战。";
    const auto* entry = entryFor(owner, w.eventId);
    if (entry && entry->closed) return "本届窗口已经结束；回退存档不能重新开放。";
    if (source->player.arts.battle || source->pending || source->job || source->dungeons.run)
        return "请先结束当前斗法、抉择、安排或秘境探索。";
    return "";
}
Value Store::entryView(const Entry& e, bool build) const {
    auto v = obj(); int wins = 0, draws = 0, losses = 0, pending = 0;
    for (const auto& [id, a] : e.attempts) { (void)id; if (a.result == "win") ++wins; else if (a.result == "draw") ++draws; else if (a.result == "pending") ++pending; else ++losses; }
    v.set("id", e.id); v.set("nickname", e.nickname); v.set("name", e.snapshot.player.name);
    v.set("realm", realmName(e.snapshot.player.stage)); v.set("stage", e.snapshot.player.stage);
    v.set("group", groupName(e.group)); v.set("groupId", e.group); v.set("eventId", e.eventId);
    v.set("registeredYear", e.registeredYear); v.set("snapshotVersion", SNAPSHOT_VERSION); v.set("rulesVersion", RULES_VERSION);
    v.set("createdAt", at(e.createdAt)); v.set("closed", e.closed); v.set("score", wins * 3 + draws); v.set("wins", wins);
    v.set("draws", draws); v.set("losses", losses); v.set("attempts", static_cast<int>(e.attempts.size())); v.set("pending", pending);
    if (build) v.set("build", buildView(e.snapshot.player)); return v;
}
Value Store::encounterView(const Encounter& e) const {
    auto v = obj(); v.set("id", e.id); v.set("revision", e.revision); v.set("state", e.result.empty() ? "active" : "settled");
    auto target = entries_.find(e.targetId); if (target != entries_.end()) v.set("target", entryView(target->second, true));
    v.set("startedAt", at(e.startedAt)); v.set("finishedAt", at(e.finishedAt)); v.set("result", e.result.empty() ? Value() : Value(e.result));
    v.set("scoreDelta", e.scoreDelta); auto b = api::battleView(e.game); v.set("battle", b); v.set("log", b["log"]);
    return v;
}
Value Store::hint(const std::string& owner, const Game* source) const {
    auto v = obj(); const Window w = source ? windowFor(*source) : Window{}; v.set("window", windowView(w));
    const auto reason = eligibility(owner, source); v.set("reason", reason); v.set("hasEncounter", active(owner));
    v.set("phase", !source ? "unavailable" : active(owner) ? "fighting" : !w.open ? "closed" : reason.empty() ? "eligible" : "unavailable"); return v;
}
Value Store::status(const std::string& owner, const Game* source) const {
    auto v = hint(owner, source); v.set("ok", healthy()); v.set("rules", rules());
    const Window w = source ? windowFor(*source) : Window{}; const auto* entry = entryFor(owner, w.eventId);
    const auto reason = eligibility(owner, source); auto opponents = arr(); int available = 0;
    if (entry) {
        v.set("entry", entryView(*entry, true));
        v.set("entryNotice", "本道号本届已报名，保留原留影与成绩；不能通过换存档重新报名。");
        for (const auto& [id, e] : entries_) {
            if (e.owner == owner || e.eventId != entry->eventId || e.group != entry->group) continue;
            auto x = entryView(e, true); auto attempted = entry->attempts.find(id);
            const bool can = reason.empty() && attempted == entry->attempts.end();
            x.set("canChallenge", can); x.set("reason", attempted != entry->attempts.end() ? "本届已挑战过此留影。" : reason);
            if (attempted != entry->attempts.end()) x.set("result", attempted->second.result);
            if (can) ++available; opponents.push(x);
        }
    } else v.set("entry", Value());
    v.set("opponents", opponents); v.set("canJoin", !entry && reason.empty() && entries_.size() < MAX_ENTRIES);
    if (!entry && reason.empty() && entries_.size() >= MAX_ENTRIES) v.set("reason", "本服留影存储已达上限，请联系管理员扩容。");
    if (!entry && reason.empty() && entries_.size() < MAX_ENTRIES) { Player frozen; std::string error; if (freezePlayer(source->player, frozen, error)) v.set("previewBuild", buildView(frozen)); else { v.set("canJoin", false); v.set("reason", error); } }
    const auto encounter = encounters_.find(owner);
    v.set("encounter", encounter == encounters_.end() ? Value() : encounterView(encounter->second));
    if (entry && reason.empty()) {
        v.set("phase", available ? "ready" : opponents.asArray().empty() ? "waiting" : "settled");
        if (opponents.asArray().empty()) v.set("reason", "本届同境界尚无其他真人报名，等待其他修士留下留影。不会用NPC代替真人。");
        else if (!available) v.set("reason", "本届当前可见的真人留影均已挑战，等待新的报名者。");
    }
    if (active(owner)) v.set("phase", "fighting");
    if (!healthy()) { v.set("ok", false); v.set("error", "天机大比记录暂不可用，请联系服务器管理员。"); }
    return v;
}
bool Store::observe(const std::string& owner, const Game* source) {
    if (!healthy()) return false;
    if (!source) return true;
    const int year = windowFor(*source).year;
    std::vector<std::string> changed;
    for (auto& [id, e] : entries_) if (e.owner == owner && !e.closed && year > e.eventId * 50 + 9) { e.closed = true; changed.push_back(id); }
    if (changed.empty()) return true;
    if (persist()) return true;
    for (const auto& id : changed) entries_[id].closed = false;
    return false;
}
void Store::settle(Encounter& e, long long now) {
    // Keep the recent combat report bounded even for 24-action draw/combo builds.
    if (e.game.player.arts.battle) {
        auto& log = e.game.player.arts.battle->log;
        if (log.size() > 256) log.erase(log.begin(), log.end() - 256);
    }
    const auto outcome = battleShadowOutcome(e.game);
    if (outcome.empty() || !e.result.empty()) return;
    // Terminal compaction guarantees that a near-cap report cannot prevent exit.
    auto& finalLog = e.game.player.arts.battle->log;
    if (finalLog.size() > 64) finalLog.erase(finalLog.begin(), finalLog.end() - 64);
    e.result = outcome == "retreat" ? "forfeit" : outcome; e.finishedAt = now;
    e.scoreDelta = e.result == "win" ? 3 : e.result == "draw" ? 1 : 0;
    auto& entry = entries_.at(e.entryId); entry.attempts.at(e.targetId).result = e.result;
}
Value Store::board(long long now) {
    if (!healthy()) { auto v = obj(); v.set("ok", false); v.set("error", "天机榜记录暂不可用。"); return v; }
    if (boardAt_ && now >= boardAt_ && now - boardAt_ < BOARD_REFRESH_SECONDS) return boardCache_;
    std::vector<Value> rows; for (const auto& [id, e] : entries_) { (void)id; rows.push_back(entryView(e, false)); }
    auto key = [](const Value& x) { return std::make_tuple(-x["score"].asInt(), -x["wins"].asInt(), x["losses"].asInt()); };
    std::sort(rows.begin(), rows.end(), [&](const Value& a, const Value& b) { return key(a) == key(b) ? a["id"].asString() < b["id"].asString() : key(a) < key(b); });
    auto list = arr(); int rank = 0;
    for (size_t i = 0; i < rows.size(); ++i) { if (!i || key(rows[i]) != key(rows[i-1])) rank = static_cast<int>(i) + 1; rows[i].set("rank", rank); list.push(rows[i]); }
    boardAt_ = now; boardCache_ = obj(); boardCache_.set("ok", true); boardCache_.set("updatedAt", at(now));
    boardCache_.set("nextRefreshAt", at(now + BOARD_REFRESH_SECONDS)); boardCache_.set("refreshSeconds", BOARD_REFRESH_SECONDS);
    boardCache_.set("total", static_cast<int>(rows.size())); boardCache_.set("list", list); boardCache_.set("rules", rules()); return boardCache_;
}

http::Response Store::handle(const http::Request& req, const std::string& owner, const std::string& nickname, const Game* source, long long now) {
    if (!healthy()) return failure(503, "天机大比记录暂不可用，请联系服务器管理员。", "store_unavailable");
    if (req.path == "/api/tianji/board") return req.method == "GET" ? response(200, board(now)) : failure(405, "此操作需要GET请求。", "method");
    if (!ownerId(owner)) return failure(401, "请先登录。", "auth");
    if (req.path == "/api/tianji/status") {
        if (req.method != "GET") return failure(405, "此操作需要GET请求。", "method");
        if (!observe(owner, source)) return failure(503, "天机大比记录写入失败，请稍后重试。", "storage");
        return response(200, status(owner, source));
    }
    if (req.method != "POST") return failure(405, "此操作需要POST请求。", "method");
    if (req.body.size() > 4096) return failure(413, "天机大比请求过大。", "payload");
    std::string parseError; const auto in = Value::parse(req.body, &parseError);
    if (!parseError.empty() || !in.isObject()) return failure(400, "请求正文必须是JSON对象。", "payload");
    const bool joining = req.path == "/api/tianji/join", challenging = req.path == "/api/tianji/challenge", acting = req.path == "/api/tianji/action";
    if (!joining && !challenging && !acting) return failure(404, "没有这个天机大比操作。", "route");
    const std::set<std::string> allowed = joining ? std::set<std::string>{"requestId"} : challenging ? std::set<std::string>{"requestId", "targetId"} : std::set<std::string>{"requestId", "encounterId", "expectedRevision", "id", "target"};
    for (const auto& [key, value] : in.asObject()) { (void)value; if (!allowed.count(key)) return failure(400, "不接受客户端提供的报名属性、归属或成绩。", "field"); }
    const auto requestId = in["requestId"].asString();
    if (!identifier(requestId)) return failure(400, "requestId需为1至64位字母、数字或下划线、短横线、点。", "request_id");
    if (!observe(owner, source)) return failure(503, "天机大比记录写入失败，请稍后重试。", "storage");
    // Validate first, then retain only the records this operation can change.
    // Other owners' snapshots/encounters never need a rollback copy.
    auto commit = [&](auto&& rollback, bool terminalReserve = false) {
        if (persist(terminalReserve)) return response(200, status(owner, source));
        rollback();
        return failure(503, "天机大比记录写入失败，本次操作未提交，请使用相同requestId重试。", "storage");
    };
    const Window w = source ? windowFor(*source) : Window{};
    auto* entry = entryFor(owner, w.eventId);
    if (joining && entry) return response(200, status(owner, source));
    if (joining || challenging) {
        if (const auto why = eligibility(owner, source); !why.empty()) {
            if (challenging) { const auto it = encounters_.find(owner); if (it != encounters_.end() && it->second.requestId == requestId && it->second.targetId == in["targetId"].asString()) return response(200, status(owner, source)); }
            return failure(409, why, "ineligible");
        }
    }
    if (joining) {
        if (entries_.size() >= MAX_ENTRIES) return failure(503, "本服留影存储已达上限，请联系管理员扩容。", "capacity");
        Player frozen; std::string why; if (!freezePlayer(source->player, frozen, why)) return failure(400, why, "build");
        const auto nextBefore = nextId_;
        Entry e; e.id = "shadow-" + std::to_string(nextId_++); e.owner = owner; e.nickname = nickname;
        e.sourceLife = source->lifeId.substr(0, 128); e.eventId = w.eventId; e.registeredYear = w.year; e.group = std::min(4, frozen.stage / 3);
        e.createdAt = now; e.snapshot = snapshotGame(frozen);
        const auto id = e.id;
        if (!entries_.emplace(id, std::move(e)).second) {
            nextId_ = nextBefore;
            return failure(503, "天机大比留影编号冲突，请联系服务器管理员。", "store_unavailable");
        }
        return commit([&] { entries_.erase(id); nextId_ = nextBefore; });
    }
    if (challenging) {
        if (!entry) return failure(409, "请先报名并留下构筑留影。", "not_joined");
        const auto targetId = in["targetId"].asString();
        if (!identifier(targetId)) return failure(400, "对手留影标识无效。", "target");
        auto target = entries_.find(targetId);
        if (target == entries_.end() || target->second.owner == owner || target->second.eventId != entry->eventId || target->second.group != entry->group)
            return failure(409, "只能挑战同届同组的其他真人留影。", "target");
        for (const auto& [id, a] : entry->attempts) if (a.requestId == requestId && id != targetId) return failure(409, "requestId已用于另一场挑战。", "request_conflict");
        const auto prior = entry->attempts.find(targetId);
        if (prior != entry->attempts.end()) {
            if (prior->second.requestId == requestId) return response(200, status(owner, source));
            return failure(409, "本届对同一留影仅能挑战一次。", "already_attempted");
        }
        const auto nextBefore = nextId_;
        Encounter e; e.id = "duel-" + std::to_string(nextId_++); e.owner = owner; e.entryId = entry->id; e.targetId = targetId;
        e.requestId = requestId; e.startedAt = now; std::random_device random;
        battleStartShadow(e.game, entry->snapshot.player, target->second.snapshot.player, random());
        // Keep the previous (settled) encounter node intact until the write commits.
        auto previous = encounters_.extract(owner);
        entry->attempts[targetId] = {"pending", requestId}; encounters_.emplace(owner, std::move(e)); settle(encounters_.at(owner), now);
        return commit([&] {
            encounters_.erase(owner);
            if (!previous.empty()) encounters_.insert(std::move(previous));
            entry->attempts.erase(targetId); nextId_ = nextBefore;
        });
    }
    auto it = encounters_.find(owner);
    if (it == encounters_.end() || it->second.id != in["encounterId"].asString()) return failure(409, "此挑战不属于当前账号或已不是当前挑战。", "encounter");
    auto& e = it->second;
    const auto action = in["id"].asString(), target = in["target"].asString();
    if (action.empty() || action.size() > 128 || target.size() > 128 || (!in["target"].isNull() && !in["target"].isString())) return failure(400, "斗法行动参数无效。", "action");
    const auto revision = in["expectedRevision"];
    if (!revision.isNumber() || revision.asNumber() != revision.asInt(-1) || revision.asInt(-1) < 0) return failure(400, "expectedRevision必须为非负整数。", "revision");
    const auto fingerprint = in.dump(); auto receipt = e.receipts.find(requestId);
    if (receipt != e.receipts.end()) return receipt->second == fingerprint ? response(200, status(owner, source)) : failure(409, "requestId已用于不同的行动。", "request_conflict");
    if (revision.asInt() != e.revision) return failure(409, "挑战状态已更新，请重新读取状态后操作。", "stale_revision");
    if (!e.result.empty()) return failure(409, "此挑战已经结算。", "settled");
    if (e.receipts.size() >= MAX_STEPS || (e.receipts.size() >= MAX_STEPS - 1 && action != "forfeit")) return failure(409, "行动次数已达安全上限，请认输结束。", "step_limit");
    auto before = e;
    auto& attempt = entries_.at(e.entryId).attempts.at(e.targetId);
    auto resultBefore = attempt.result;
    auto rollback = [&] { e = std::move(before); attempt.result = std::move(resultBefore); };
    if (action == "forfeit") battleFinish(e.game, false, true);
    else { const auto result = battleTurn(e.game, action, target); if (!result.ok) { rollback(); return failure(400, result.error, "illegal_action"); } }
    ++e.revision; e.receipts[requestId] = fingerprint; settle(e, now); return commit(rollback, !e.result.empty());
}

Value Store::serialize() const {
    auto root = obj(); root.set("format", "ningzhou-tianji-server"); root.set("version", 1); root.set("rulesVersion", RULES_VERSION); root.set("snapshotVersion", SNAPSHOT_VERSION); root.set("nextId", static_cast<double>(nextId_));
    auto entries = arr();
    for (const auto& [id, e] : entries_) {
        auto x = obj(); x.set("id", id); x.set("owner", e.owner); x.set("nickname", e.nickname); x.set("sourceLife", e.sourceLife);
        x.set("eventId", e.eventId); x.set("registeredYear", e.registeredYear); x.set("group", e.group); x.set("createdAt", at(e.createdAt)); x.set("closed", e.closed);
        x.set("snapshot", Value::parse(saveToJson(e.snapshot))); auto attempts = obj();
        for (const auto& [target, a] : e.attempts) { auto value = obj(); value.set("result", a.result); value.set("requestId", a.requestId); attempts.set(target, value); }
        x.set("attempts", attempts); entries.push(x);
    }
    root.set("entries", entries); auto encounters = arr();
    for (const auto& [owner, e] : encounters_) {
        auto x = obj(); x.set("id", e.id); x.set("owner", owner); x.set("entryId", e.entryId); x.set("targetId", e.targetId); x.set("requestId", e.requestId);
        x.set("result", e.result); x.set("revision", e.revision); x.set("scoreDelta", e.scoreDelta); x.set("startedAt", at(e.startedAt)); x.set("finishedAt", at(e.finishedAt));
        x.set("game", Value::parse(saveToJson(e.game))); auto receipts = obj(); for (const auto& [id, value] : e.receipts) receipts.set(id, value); x.set("receipts", receipts); encounters.push(x);
    }
    root.set("encounters", encounters); return root;
}
bool Store::persist(bool terminalReserve) {
    // A full store must still permit bounded existing encounters to finish.
    // New/ongoing writes leave 1 MiB for final log/result/receipt growth.
    const auto text = serialize().dump();
    if (text.size() > (terminalReserve ? MAX_STORE_BYTES : MAX_STORE_BYTES - 1024 * 1024)) return false;
    for (const auto& [owner, e] : encounters_) { (void)owner; if (saveToJson(e.game).size() > MAX_ENCOUNTER_BYTES) return false; }
    std::error_code ec; fs::create_directories(fs::u8path(directory_), ec); if (ec) return false;
    return atomicWrite(fs::u8path(directory_) / "store.json", text);
}
void Store::load() {
    const auto path = fs::u8path(directory_) / "store.json"; std::error_code ec;
    if (!fs::exists(path, ec)) { if (ec) error_ = "无法读取天机大比目录"; return; }
    auto fail = [&]() { entries_.clear(); encounters_.clear(); error_ = "天机大比存储损坏或版本不兼容；为防止重复计分，已暂停该功能"; };
    if (fs::file_size(path, ec) > MAX_STORE_BYTES || ec) { fail(); return; }
    std::ifstream file(path, std::ios::binary); std::ostringstream text; text << file.rdbuf(); if (!file.good() && !file.eof()) { fail(); return; }
    std::string why; const auto root = Value::parse(text.str(), &why);
    if (!why.empty() || root["format"].asString() != "ningzhou-tianji-server" || root["version"].asInt() != 1 ||
        root["rulesVersion"].asInt() != RULES_VERSION || root["snapshotVersion"].asInt() != SNAPSHOT_VERSION ||
        !root["entries"].isArray() || root["entries"].asArray().size() > MAX_ENTRIES || !root["encounters"].isArray() || root["encounters"].asArray().size() > MAX_ENTRIES ||
        root["nextId"].asNumber() < 1 || root["nextId"].asNumber() > 1000000000) { fail(); return; }
    nextId_ = static_cast<unsigned>(root["nextId"].asNumber()); std::set<std::pair<std::string, int>> unique;
    for (const auto& x : root["entries"].asArray()) {
        Entry e; e.id = x["id"].asString(); e.owner = x["owner"].asString(); e.nickname = x["nickname"].asString(); e.sourceLife = x["sourceLife"].asString();
        e.eventId = x["eventId"].asInt(); e.registeredYear = x["registeredYear"].asInt(); e.group = x["group"].asInt(-1); e.createdAt = static_cast<long long>(x["createdAt"].asNumber()); e.closed = x["closed"].asBool();
        const auto snapshot = x["snapshot"].dump();
        if (!identifier(e.id) || !ownerId(e.owner) || e.nickname.empty() || e.nickname.size() > 128 || e.sourceLife.size() > 128 ||
            e.eventId < 1 || e.eventId > 100000 || e.registeredYear < e.eventId * 50 || e.registeredYear > e.eventId * 50 + 9 ||
            !unique.insert({e.owner, e.eventId}).second || entries_.count(e.id) || snapshot.size() > MAX_SNAPSHOT_BYTES ||
            !loadDetachedFromJson(snapshot, e.snapshot, &why) || e.snapshot.player.arts.battle || e.group != std::min(4, e.snapshot.player.stage / 3) ||
            !x["attempts"].isObject() || x["attempts"].asObject().size() > MAX_ENTRIES) { fail(); return; }
        for (const auto& [target, a] : x["attempts"].asObject()) {
            const auto result = a["result"].asString(); const auto id = a["requestId"].asString();
            if (!identifier(target) || !identifier(id) || (result != "win" && result != "loss" && result != "draw" && result != "forfeit" && result != "pending")) { fail(); return; }
            e.attempts[target] = {result, id};
        }
        entries_.emplace(e.id, std::move(e));
    }
    for (const auto& [id, e] : entries_) for (const auto& [target, a] : e.attempts) {
        (void)a; const auto t = entries_.find(target); if (t == entries_.end() || t->second.owner == e.owner || t->second.eventId != e.eventId || t->second.group != e.group) { fail(); return; }
    }
    for (const auto& x : root["encounters"].asArray()) {
        Encounter e; e.id = x["id"].asString(); e.owner = x["owner"].asString(); e.entryId = x["entryId"].asString(); e.targetId = x["targetId"].asString();
        e.requestId = x["requestId"].asString(); e.result = x["result"].asString(); e.revision = x["revision"].asInt(-1); e.scoreDelta = x["scoreDelta"].asInt(-1);
        e.startedAt = static_cast<long long>(x["startedAt"].asNumber()); e.finishedAt = static_cast<long long>(x["finishedAt"].asNumber());
        const auto game = x["game"].dump(); const auto own = entries_.find(e.entryId), target = entries_.find(e.targetId);
        if (!identifier(e.id) || !ownerId(e.owner) || !identifier(e.requestId) || encounters_.count(e.owner) || own == entries_.end() || own->second.owner != e.owner ||
            target == entries_.end() || target->second.owner == e.owner || !own->second.attempts.count(e.targetId) || e.revision < 0 || e.revision > static_cast<int>(MAX_STEPS) ||
            game.size() > MAX_ENCOUNTER_BYTES || !loadDetachedFromJson(game, e.game, &why) || !e.game.player.arts.battle || !e.game.player.arts.battle->shadowPlayer ||
            !x["receipts"].isObject() || x["receipts"].asObject().size() > MAX_STEPS) { fail(); return; }
        const auto actual = battleShadowOutcome(e.game); const auto result = actual == "retreat" ? "forfeit" : actual;
        if (e.result != result || own->second.attempts.at(e.targetId).result != (result.empty() ? "pending" : result) ||
            e.scoreDelta != (result == "win" ? 3 : result == "draw" ? 1 : 0)) { fail(); return; }
        for (const auto& [id, value] : x["receipts"].asObject()) {
            if (!identifier(id) || !value.isString() || value.asString().size() > 4096) { fail(); return; } e.receipts[id] = value.asString();
        }
        if (e.revision != static_cast<int>(e.receipts.size())) { fail(); return; }
        encounters_.emplace(e.owner, std::move(e));
    }
    for (const auto& [id, e] : entries_) for (const auto& [target, a] : e.attempts) if (a.result == "pending") {
        const auto duel = encounters_.find(e.owner); if (duel == encounters_.end() || duel->second.entryId != id || duel->second.targetId != target || !duel->second.result.empty()) { fail(); return; }
    }
}
} // namespace nz::tianji
