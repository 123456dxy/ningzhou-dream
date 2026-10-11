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

Response handle(const Request& req) {
    const std::string& path = req.path;

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
        if (g_sessions.size() >= kMaxSessions) {
            // 清掉最久未用的一半
            std::vector<std::pair<long long, std::string>> v;
            for (const auto& [sid, info] : g_sessions) v.push_back({info.lastUsed, sid});
            std::sort(v.begin(), v.end());
            for (size_t i = 0; i < v.size() / 2; ++i) g_sessions.erase(v[i].second);
        }
        const std::string id = hexId(nick);
        const std::string sid = randomSid();
        g_sessions[sid] = SessionInfo{id, nick, nowSec()};
        g_sessionsDirty = true;
        Player& p = playerFor(id, nick);
        nz::json::Value o(nz::json::Object{});
        o.set("ok", true);
        o.set("nickname", nick);
        o.set("hasGame", p.s.started);
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
