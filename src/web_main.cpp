// 宁州一梦 · C++ 版 — Web 服务入口（多人版）
//
//   /            前端界面
//   /api/<动作>  游戏规则（由 C++ core 提供）
//
// 多人：玩家输入道号（昵称）登录，服务器发放会话 cookie；
// 同一道号 = 同一份存档（按道号分目录：<数据目录>/players/<道号的 UTF-8 十六进制>/）。
//
// 用法：
//   ningzhou_web [--host 0.0.0.0] [--port 8106] [--web DIR] [--data DIR] [--idle-minutes 30] [--no-open]
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <map>
#include <vector>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/api.h"
#include "core/http.h"
#include "core/json.h"
#include "core/save.h"
#include "core/pwhash.h"
#include "core/calendar.h"
#include "core/model.h"

namespace {

namespace fs = std::filesystem;
using nz::http::Request;
using nz::http::Response;

// 与可执行文件同级的 web/ 目录
std::string defaultWebDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
        const auto path = (fs::path(std::wstring(buf, n)).parent_path() / L"web").u8string();
        return std::string(reinterpret_cast<const char*>(path.data()), path.size());
    }
#endif
    return "web";
}

fs::path u8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

long long nowSec() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

bool writeAtomic(const std::string& path, const std::string& content) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(u8(tmp), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << content;
        if (!f.good()) return false;
    }
    std::error_code ec;
    fs::rename(u8(tmp), u8(path), ec);
    return !ec;
}

bool readAll(const std::string& path, std::string& out) {
    std::ifstream f(u8(path), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// ---------------- 道号校验 ----------------
// 1~16 个字符；只允许汉字、英文字母、数字、下划线。
bool validNickname(const std::string& s, std::string& why) {
    size_t i = 0, count = 0;
    if (s.empty()) { why = "道号不能为空。"; return false; }
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0; int len = 0;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { why = "道号编码无效。"; return false; }
        if (i + len > s.size()) { why = "道号编码无效。"; return false; }
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { why = "道号编码无效。"; return false; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        const bool overlong = (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000);
        if (overlong) { why = "道号编码无效。"; return false; }
        const bool ok =
            (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' ||
            (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
            (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0x20000 && cp <= 0x2FA1F);
        if (!ok) { why = "道号只能包含汉字、英文字母、数字和下划线。"; return false; }
        i += len;
        ++count;
    }
    if (count > 16) { why = "道号最多 16 个字。"; return false; }
    return true;
}

std::string hexId(const std::string& s) {
    static const char* d = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) { out += d[c >> 4]; out += d[c & 15]; }
    return out;
}

std::string randomSid() {
    std::random_device rd;
    static const char* d = "0123456789abcdef";
    std::string out;
    for (int i = 0; i < 8; ++i) {
        uint32_t v = rd();
        for (int k = 0; k < 8; ++k) { out += d[v & 15]; v >>= 4; }
    }
    return out;
}

// ---------------- 玩家与会话 ----------------
struct Player {
    nz::api::Session s;
    std::string id, nickname, dir;
    long long lastSeen = 0;
    std::string lastPersisted;
};

struct SessionInfo { std::string id; std::string nickname; long long lastUsed = 0; };

std::string g_dataDir = "saves";
std::string g_feedbackDir = "feedback";
long long g_idleSeconds = 30 * 60;
std::map<std::string, std::unique_ptr<Player>> g_players;   // id -> player
std::map<std::string, SessionInfo> g_sessions;              // sid -> info
bool g_sessionsDirty = false;
long long g_lastSessionsFlush = 0;
constexpr long long kSessionTtl = 30LL * 24 * 3600;
constexpr size_t kMaxSessions = 20000;

std::string sessionsPath() { return g_dataDir + "/sessions.tsv"; }

void loadSessions() {
    std::string text;
    if (!readAll(sessionsPath(), text)) return;
    std::istringstream in(text);
    std::string line;
    const long long now = nowSec();
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string sid, id, nick, used;
        if (!std::getline(ls, sid, '\t') || !std::getline(ls, id, '\t') ||
            !std::getline(ls, nick, '\t') || !std::getline(ls, used)) continue;
        std::string why;
        if (sid.size() != 64 || !validNickname(nick, why) || hexId(nick) != id) continue;
        const long long t = std::atoll(used.c_str());
        if (now - t > kSessionTtl) continue;
        // 无密码时代发出的会话不再承认：老号需重新登录并设置密码
        std::error_code pec;
        if (!fs::exists(u8(g_dataDir + "/players/" + id + "/password.txt"), pec)) continue;
        g_sessions[sid] = SessionInfo{id, nick, t};
    }
}

void saveSessions() {
    std::ostringstream out;
    for (const auto& [sid, info] : g_sessions)
        out << sid << '\t' << info.id << '\t' << info.nickname << '\t' << info.lastUsed << '\n';
    std::error_code ec;
    fs::create_directories(u8(g_dataDir), ec);
    if (writeAtomic(sessionsPath(), out.str())) g_sessionsDirty = false;
}

void persist(Player& p) {
    if (!p.s.started) return;
    const std::string json = nz::saveToJson(p.s.g);
    if (json == p.lastPersisted) return;
    if (writeAtomic(p.dir + "/session.json", json)) p.lastPersisted = json;
    else std::fprintf(stderr, "[%s] 续玩数据写入失败\n", p.nickname.c_str());
}

Player& playerFor(const std::string& id, const std::string& nickname) {
    auto it = g_players.find(id);
    if (it != g_players.end()) {
        it->second->lastSeen = nowSec();
        return *it->second;
    }
    auto p = std::make_unique<Player>();
    p->id = id;
    p->nickname = nickname;
    p->dir = g_dataDir + "/players/" + id;
    p->lastSeen = nowSec();
    std::error_code ec;
    fs::create_directories(u8(p->dir), ec);
    writeAtomic(p->dir + "/nickname.txt", nickname + "\n");
    std::string text;
    if (readAll(p->dir + "/session.json", text)) {
        std::string err;
        if (nz::api::restoreGame(p->s, text, &err)) p->lastPersisted = text;
        else std::fprintf(stderr, "[%s] 续玩数据读取失败：%s\n", nickname.c_str(), err.c_str());
    }
    Player& ref = *p;
    g_players[id] = std::move(p);
    return ref;
}

std::string cookieSid(const Request& req) {
    const auto it = req.headers.find("cookie");
    if (it == req.headers.end()) return {};
    const std::string& c = it->second;
    size_t pos = 0;
    while ((pos = c.find("nz_sid=", pos)) != std::string::npos) {
        if (pos == 0 || c[pos - 1] == ' ' || c[pos - 1] == ';') {
            const size_t start = pos + 7;
            const size_t end = c.find(';', start);
            std::string v = c.substr(start, end == std::string::npos ? std::string::npos : end - start);
            while (!v.empty() && v.back() == ' ') v.pop_back();
            return v;
        }
        pos += 7;
    }
    return {};
}

Response jsonResp(int status, nz::json::Value v) {
    Response r;
    r.status = status;
    r.body = v.dump();
    return r;
}

Response errorResp(int status, const std::string& msg, bool needLogin = false) {
    nz::json::Value o(nz::json::Object{});
    o.set("ok", false);
    o.set("error", msg);
    if (needLogin) o.set("needLogin", true);
    return jsonResp(status, o);
}

// ---------------- 排行榜 ----------------
// 只从服务器端的存档（内存中的在线玩家 + 磁盘上的 session.json）计算，不接受客户端提交分数。
// 排序：境界（stage）> 修为（xp）> 声望（renown）。结果缓存 30 秒。
struct BoardRow { std::string nickname, name, realm; int stage = 0, xp = 0, renown = 0, age = 0, year = 0; bool ended = false; };

BoardRow rowOf(const std::string& nick, const nz::Game& g) {
    BoardRow r;
    r.nickname = nick; r.name = g.player.name; r.stage = g.player.stage; r.xp = g.player.xp;
    r.renown = g.player.renown; r.age = g.player.age; r.year = g.year; r.ended = g.ended;
    r.realm = nz::realmName(g.player.stage);
    return r;
}

std::vector<BoardRow> g_board;
long long g_boardAt = -1000;

const std::vector<BoardRow>& leaderboard() {
    const long long now = nowSec();
    if (now - g_boardAt < 30) return g_board;
    g_boardAt = now;
    std::vector<BoardRow> rows;
    std::error_code ec;
    for (fs::directory_iterator it(u8(g_dataDir + "/players"), ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_directory(ec)) continue;
        const auto u = it->path().filename().u8string();
        const std::string id(reinterpret_cast<const char*>(u.data()), u.size());
        std::string nick;
        if (!readAll(g_dataDir + "/players/" + id + "/nickname.txt", nick)) continue;
        while (!nick.empty() && (nick.back() == '\n' || nick.back() == '\r')) nick.pop_back();
        std::string why;
        if (!validNickname(nick, why) || hexId(nick) != id) continue;
        auto pit = g_players.find(id);
        if (pit != g_players.end()) {
            if (pit->second->s.started) rows.push_back(rowOf(nick, pit->second->s.g));
            continue;
        }
        std::string text, err;
        if (!readAll(g_dataDir + "/players/" + id + "/session.json", text)) continue;
        nz::Game g;
        if (!nz::loadFromJson(text, g, &err)) continue;
        if (g.player.name.empty()) continue;
        rows.push_back(rowOf(nick, g));
    }
    std::sort(rows.begin(), rows.end(), [](const BoardRow& a, const BoardRow& b) {
        if (a.stage != b.stage) return a.stage > b.stage;
        if (a.xp != b.xp) return a.xp > b.xp;
        if (a.renown != b.renown) return a.renown > b.renown;
        return a.nickname < b.nickname;
    });
    g_board = std::move(rows);
    return g_board;
}

Response leaderboardResp(const Request& req) {
    int limit = 20;
    const size_t q = req.query.find("limit=");
    if (q != std::string::npos) limit = std::clamp(std::atoi(req.query.c_str() + q + 6), 1, 100);
    const auto& rows = leaderboard();
    nz::json::Value list(nz::json::Array{});
    for (size_t i = 0; i < rows.size() && (int)i < limit; ++i) {
        const BoardRow& r = rows[i];
        nz::json::Value o(nz::json::Object{});
        o.set("rank", (double)(i + 1));
        o.set("nickname", r.nickname);
        o.set("name", r.name);
        o.set("realm", r.realm);
        o.set("stage", (double)r.stage);
        o.set("xp", (double)r.xp);
        o.set("renown", (double)r.renown);
        o.set("age", (double)r.age);
        o.set("ended", r.ended);
        list.push(o);
    }
    nz::json::Value o(nz::json::Object{});
    o.set("ok", true);
    o.set("total", (double)rows.size());
    o.set("updatedAt", (double)g_boardAt);
    o.set("list", list);
    return jsonResp(200, o);
}

// ---------------- 反馈箱 ----------------
// 追加写入 <feedback 目录>/feedback.jsonl；不提供读取接口（管理员通过面板读文件）。
// 限频：每道号、每 IP 各自 每分钟 1 条、每天 30 条。
struct RateBucket { long long lastAt = 0; long long dayStart = 0; int dayCount = 0; };
std::map<std::string, RateBucket> g_fbRate;

std::string fbRateCheck(const std::string& key, long long now) {
    auto it = g_fbRate.find(key);
    if (it == g_fbRate.end()) return {};
    const RateBucket& b = it->second;
    if (now - b.lastAt < 60) return "提交太频繁，请 " + std::to_string(60 - (now - b.lastAt)) + " 秒后再试。";
    if (now - b.dayStart < 86400 && b.dayCount >= 30) return "今天的反馈已达 30 条上限，明天再来吧。";
    return {};
}

void fbRateNote(const std::string& key, long long now) {
    if (g_fbRate.size() > 50000) {
        for (auto it = g_fbRate.begin(); it != g_fbRate.end();)
            it = (now - it->second.dayStart >= 86400 && now - it->second.lastAt >= 60) ? g_fbRate.erase(it) : std::next(it);
    }
    RateBucket& b = g_fbRate[key];
    if (now - b.dayStart >= 86400) { b.dayStart = now; b.dayCount = 0; }
    b.lastAt = now;
    ++b.dayCount;
}

size_t utf8Len(const std::string& s) {
    size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool validUtf8(const std::string& s) {
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = s[i];
        int len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (!len || i + len > s.size()) return false;
        for (int k = 1; k < len; ++k) if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        i += len;
    }
    return true;
}

std::string isoNow() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[40];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %z", &tm);
    return buf;
}

// ---------------- 密码与登录限速 ----------------
std::string pwPath(const std::string& id) { return g_dataDir + "/players/" + id + "/password.txt"; }

bool readPwRecord(const std::string& id, std::string& rec) {
    if (!readAll(pwPath(id), rec)) return false;
    while (!rec.empty() && (rec.back() == '\n' || rec.back() == '\r')) rec.pop_back();
    return !rec.empty();
}

bool writePwRecord(const std::string& id, const std::string& pass) {
    std::error_code ec;
    fs::create_directories(u8(g_dataDir + "/players/" + id), ec);
    const bool ok = writeAtomic(pwPath(id), nz::pw::makeRecord(pass) + "\n");
#ifndef _WIN32
    if (ok) fs::permissions(u8(pwPath(id)), fs::perms::owner_read | fs::perms::owner_write, ec);
#endif
    return ok;
}

bool validPassword(const std::string& p, std::string& why) {
    size_t chars = 0;
    for (unsigned char c : p) if ((c & 0xC0) != 0x80) ++chars;
    if (chars < 4) { why = "密码至少 4 位。"; return false; }
    if (p.size() > 128) { why = "密码太长（最多 128 字节）。"; return false; }
    return true;
}

// 按 IP+道号 计失败次数：10 分钟内失败 5 次即锁定到窗口结束
struct FailInfo { int count = 0; long long first = 0; };
std::map<std::string, FailInfo> g_fails;
constexpr int kMaxFails = 5;
constexpr long long kFailWindow = 600;

long long lockedFor(const std::string& key) {
    auto it = g_fails.find(key);
    if (it == g_fails.end()) return 0;
    const long long now = nowSec();
    if (now - it->second.first >= kFailWindow) { g_fails.erase(it); return 0; }
    return it->second.count >= kMaxFails ? kFailWindow - (now - it->second.first) : 0;
}

void noteFail(const std::string& key) {
    const long long now = nowSec();
    if (g_fails.size() > 50000) {
        for (auto it = g_fails.begin(); it != g_fails.end();)
            it = (now - it->second.first >= kFailWindow) ? g_fails.erase(it) : std::next(it);
    }
    auto& f = g_fails[key];
    if (f.count == 0 || now - f.first >= kFailWindow) { f.count = 0; f.first = now; }
    ++f.count;
}

void dropSessionsOf(const std::string& id, const std::string& keepSid) {
    for (auto it = g_sessions.begin(); it != g_sessions.end();)
        if (it->second.id == id && it->first != keepSid) { it = g_sessions.erase(it); g_sessionsDirty = true; }
        else ++it;
}

std::string queryParam(const std::string& q, const std::string& key) {
    size_t pos = 0;
    while (pos <= q.size()) {
        size_t amp = q.find('&', pos);
        if (amp == std::string::npos) amp = q.size();
        const std::string kv = q.substr(pos, amp - pos);
        if (kv.rfind(key + "=", 0) == 0) {
            std::string v = kv.substr(key.size() + 1), out;
            for (size_t i = 0; i < v.size(); ++i) {
                if (v[i] == '%' && i + 2 < v.size()) { out += (char)std::strtol(v.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
                else if (v[i] == '+') out += ' ';
                else out += v[i];
            }
            return out;
        }
        pos = amp + 1;
    }
    return {};
}

Response handle(const Request& req) {
    const std::string& path = req.path;

    // 查询道号是否已设密码（登录框据此提示“首次设置密码”）
    if (path == "/api/nickinfo") {
        const std::string nick = queryParam(req.query, "nickname");
        std::string why, rec;
        if (!validNickname(nick, why)) return errorResp(400, why);
        const std::string id = hexId(nick);
        std::error_code ec;
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        o.set("exists", fs::exists(u8(g_dataDir + "/players/" + id), ec));
        o.set("hasPassword", readPwRecord(id, rec));
        return jsonResp(200, o);
    }

    if (path == "/api/leaderboard") {
        if (req.method != "GET") return errorResp(405, "此操作需要 GET 请求。");
        return leaderboardResp(req);
    }

    if (path == "/api/login") {
        if (req.method != "POST") return errorResp(405, "此操作需要 POST 请求。");
        std::string err;
        const nz::json::Value in = nz::json::Value::parse(req.body, &err);
        if (!err.empty() || !in.isObject() || !in["nickname"].isString())
            return errorResp(400, "请输入道号。");
        std::string nick = in["nickname"].asString();
        while (!nick.empty() && nick.back() == ' ') nick.pop_back();
        while (!nick.empty() && nick.front() == ' ') nick.erase(0, 1);
        std::string why;
        if (!validNickname(nick, why)) return errorResp(400, why);
        const std::string pass = in["password"].isString() ? in["password"].asString() : std::string();
        if (!validPassword(pass, why)) return errorResp(400, why);
        const std::string uid = hexId(nick);
        const std::string failKey = req.clientIp + "|" + uid;
        if (const long long wait = lockedFor(failKey); wait > 0)
            return errorResp(429, "密码错误次数过多，请 " + std::to_string((wait + 59) / 60) + " 分钟后再试。");
        std::string rec;
        bool firstSet = false;
        if (readPwRecord(uid, rec)) {
            if (!nz::pw::verifyRecord(rec, pass)) { noteFail(failKey); return errorResp(401, "道号或密码不对。"); }
            g_fails.erase(failKey);
        } else {
            if (!writePwRecord(uid, pass)) return errorResp(500, "密码保存失败，请稍后再试。");
            firstSet = true;
        }
        if (g_sessions.size() >= kMaxSessions) {
            // 清掉最久未用的一半
            std::vector<std::pair<long long, std::string>> v;
            for (const auto& [sid, info] : g_sessions) v.push_back({info.lastUsed, sid});
            std::sort(v.begin(), v.end());
            for (size_t i = 0; i < v.size() / 2; ++i) g_sessions.erase(v[i].second);
        }
        const std::string id = hexId(nick);
        const std::string sid = randomSid();
        // 首次设密码：作废这个道号此前（无密码时代）发出的其他会话
        if (firstSet) dropSessionsOf(id, "");
        g_sessions[sid] = SessionInfo{id, nick, nowSec()};
        g_sessionsDirty = true;
        Player& p = playerFor(id, nick);
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        o.set("nickname", nick);
        o.set("hasGame", p.s.started);
        o.set("passwordSet", firstSet);
        Response r = jsonResp(200, o);
        r.extraHeaders.push_back({"Set-Cookie",
            "nz_sid=" + sid + "; Path=/; Max-Age=2592000; HttpOnly; SameSite=Lax"});
        return r;
    }

    const std::string sid = cookieSid(req);
    auto sit = sid.empty() ? g_sessions.end() : g_sessions.find(sid);

    if (path == "/api/logout") {
        if (sit != g_sessions.end()) { g_sessions.erase(sit); g_sessionsDirty = true; }
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        Response r = jsonResp(200, o);
        r.extraHeaders.push_back({"Set-Cookie", "nz_sid=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax"});
        return r;
    }

    if (sit == g_sessions.end()) return errorResp(401, "请先输入道号登录。", true);
    SessionInfo& info = sit->second;
    if (nowSec() - info.lastUsed > 3600) { info.lastUsed = nowSec(); g_sessionsDirty = true; }
    Player& p = playerFor(info.id, info.nickname);

    if (path == "/api/whoami") {
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        o.set("nickname", p.nickname);
        o.set("hasGame", p.s.started);
        return jsonResp(200, o);
    }

    if (path == "/api/password") {
        if (req.method != "POST") return errorResp(405, "此操作需要 POST 请求。");
        std::string err, why, rec;
        const nz::json::Value in = nz::json::Value::parse(req.body, &err);
        if (!err.empty() || !in.isObject() || !in["old"].isString() || !in["new"].isString())
            return errorResp(400, "请填写原密码和新密码。");
        const std::string failKey = req.clientIp + "|" + p.id;
        if (const long long wait = lockedFor(failKey); wait > 0)
            return errorResp(429, "密码错误次数过多，请 " + std::to_string((wait + 59) / 60) + " 分钟后再试。");
        if (readPwRecord(p.id, rec) && !nz::pw::verifyRecord(rec, in["old"].asString())) {
            noteFail(failKey);
            return errorResp(403, "原密码不对。");
        }
        if (!validPassword(in["new"].asString(), why)) return errorResp(400, why);
        if (!writePwRecord(p.id, in["new"].asString())) return errorResp(500, "密码保存失败。");
        dropSessionsOf(p.id, sid);   // 其他设备需用新密码重新登录
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        return jsonResp(200, o);
    }

    if (path == "/api/feedback") {
        if (req.method != "POST") return errorResp(405, "此操作需要 POST 请求。");
        std::string err;
        const nz::json::Value in = nz::json::Value::parse(req.body, &err);
        if (!err.empty() || !in.isObject() || !in["text"].isString()) return errorResp(400, "请填写反馈内容。");
        const std::string type = in["type"].isString() ? in["type"].asString() : "";
        if (type != "bug" && type != "suggest" && type != "other") return errorResp(400, "请选择反馈类型。");
        std::string text = in["text"].asString();
        while (!text.empty() && (text.back() == ' ' || text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (text.empty()) return errorResp(400, "请填写反馈内容。");
        if (!validUtf8(text)) return errorResp(400, "反馈内容编码无效。");
        if (utf8Len(text) > 2000) return errorResp(400, "反馈最多 2000 字。");
        const long long now = nowSec();
        const std::string kNick = "n|" + p.id, kIp = "i|" + req.clientIp;
        if (std::string why = fbRateCheck(kNick, now); !why.empty()) return errorResp(429, why);
        if (std::string why = fbRateCheck(kIp, now); !why.empty()) return errorResp(429, why);
        nz::json::Value o(nz::json::Object{});
        o.set("time", isoNow());
        o.set("nickname", p.nickname);
        o.set("ip", req.clientIp);
        o.set("type", type);
        o.set("text", text);
        if (p.s.started) {
            const nz::Game& g = p.s.g;
            nz::json::Value c(nz::json::Object{});
            c.set("character", g.player.name);
            c.set("realm", nz::realmName(g.player.stage));
            c.set("xp", (double)g.player.xp);
            c.set("age", (double)g.player.age);
            c.set("gameDate", nz::calendarLabel(g));
            c.set("location", g.player.location);
            c.set("ended", g.ended);
            o.set("context", c);
        }
        const auto ua = req.headers.find("user-agent");
        if (ua != req.headers.end()) o.set("userAgent", ua->second.substr(0, 200));
        std::error_code ec;
        fs::create_directories(u8(g_feedbackDir), ec);
        std::string line = o.dump();
        for (char& ch : line) if (ch == '\n' || ch == '\r') ch = ' ';
        {
            std::ofstream f(u8(g_feedbackDir + "/feedback.jsonl"), std::ios::binary | std::ios::app);
            if (!f) return errorResp(500, "反馈保存失败，请稍后再试。");
            f << line << '\n';
            if (!f.good()) return errorResp(500, "反馈保存失败，请稍后再试。");
        }
        fbRateNote(kNick, now);
        fbRateNote(kIp, now);
        nz::json::Value r(nz::json::Object{});
        r.set("ok", true);
        return jsonResp(200, r);
    }

    // 切到这个玩家的会话与存档目录，交给原有规则接口
    nz::api::bindSession(&p.s);
    nz::setSaveDirOverride(p.dir);
    Response r;
    try {
        r = nz::api::handle(req);
    } catch (...) {
        nz::api::bindSession(nullptr);
        nz::setSaveDirOverride(p.dir + "/__unbound__");
        throw;
    }
    persist(p);
    nz::api::bindSession(nullptr);
    // 未绑定玩家时指向一个不存在的子目录，避免误写到数据根目录
    nz::setSaveDirOverride(g_dataDir + "/__unbound__");
    return r;
}

void tick() {
    const long long now = nowSec();
    for (auto it = g_players.begin(); it != g_players.end();) {
        if (now - it->second->lastSeen > g_idleSeconds) {
            persist(*it->second);
            std::printf("[%s] 长时间未操作，已保存并释放内存\n", it->second->nickname.c_str());
            it = g_players.erase(it);
        } else ++it;
    }
    if (g_sessionsDirty && now - g_lastSessionsFlush >= 5) {
        g_lastSessionsFlush = now;
        saveSessions();
    }
    std::fflush(stdout);
}

void shutdownAll() {
    for (auto& [id, p] : g_players) persist(*p);
    saveSessions();
    std::printf("已保存 %zu 位在线玩家的进度。\n", g_players.size());
    std::fflush(stdout);
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
#endif
    int port = 8106;
    std::string host = "0.0.0.0";
    std::string webDir = defaultWebDir();
    bool openBrowser = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--host" && i + 1 < argc) host = argv[++i];
        else if (a == "--web" && i + 1 < argc) webDir = argv[++i];
        else if (a == "--data" && i + 1 < argc) g_dataDir = argv[++i];
        else if (a == "--feedback" && i + 1 < argc) g_feedbackDir = argv[++i];
        else if (a == "--idle-minutes" && i + 1 < argc) g_idleSeconds = std::max(1, std::atoi(argv[++i])) * 60LL;
        else if (a == "--no-open") openBrowser = false;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "宁州一梦 · C++ 版（Web 多人版）\n\n"
                "用法：ningzhou_web [选项]\n"
                "  --host ADDR        监听地址（默认 0.0.0.0；仅本机用 127.0.0.1）\n"
                "  --port N           监听端口（默认 8106）\n"
                "  --web DIR          前端目录（默认可执行文件同级的 web/）\n"
                "  --data DIR         玩家数据目录（默认 ./saves）\n"
                "  --feedback DIR     反馈箱目录（默认 ./feedback）\n"
                "  --idle-minutes N   玩家多久不操作后保存并释放内存（默认 30）\n"
                "  --no-open          启动后不自动打开浏览器\n");
            return 0;
        }
    }
    if (port <= 0 || port > 65535) { std::fprintf(stderr, "端口无效\n"); return 1; }

    std::error_code ec;
    fs::create_directories(u8(g_dataDir + "/players"), ec);
    nz::setSaveDirOverride(g_dataDir + "/__unbound__");
    loadSessions();
    std::printf("数据目录：%s（已有会话 %zu 个）\n", g_dataDir.c_str(), g_sessions.size());

    if (openBrowser) {
#ifdef _WIN32
        const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/";
        std::string cmd = "cmd /c start \"\" \"" + url + "\"";
        std::thread([cmd] { system(cmd.c_str()); }).detach();
#endif
    }
    return nz::http::serve(host, port, webDir, handle, tick, shutdownAll);
}
