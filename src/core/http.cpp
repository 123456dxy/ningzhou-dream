// 宁州一梦 · C++ 版 — 极简 HTTP 服务实现
#include "core/http.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <chrono>
#include <sstream>
#include <algorithm>
#include <atomic>
#include <csignal>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "core/json.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#define CLOSE_SOCKET close
#endif

namespace nz::http {

namespace {

constexpr size_t kMaxBody = 6u * 1024 * 1024;     // 绝对上限（仅存档导入可用到）
constexpr size_t kMaxImportBody = 6u * 1024 * 1024; // 存档导入：一份新档约 1.3MB，导出包转义后更大
constexpr size_t kMaxApiBody = 512u * 1024;         // 其余接口
constexpr size_t kMaxHeader = 16u * 1024;

// 常见扩展名 -> MIME
std::string mimeOf(const std::string& path) {
    auto ends = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css"))  return "text/css; charset=utf-8";
    if (ends(".js"))   return "text/javascript; charset=utf-8";
    if (ends(".mjs"))  return "text/javascript; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".svg"))  return "image/svg+xml";
    if (ends(".png"))  return "image/png";
    if (ends(".webp")) return "image/webp";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".ico"))  return "image/x-icon";
    if (ends(".woff2")) return "font/woff2";
    return "application/octet-stream";
}

// URL 解码：%XX 与 '+'（表单）
std::string urlDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(s[i + 1]), lo = hex(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>(hi * 16 + lo);
                i += 2;
                continue;
            }
        }
        if (s[i] == '+') out += ' ';
        else out += s[i];
    }
    return out;
}

// 防止 ../ 穿越到静态目录之外
bool isSafePath(const std::string& p) {
    if (p.find("..") != std::string::npos) return false;
    if (p.find('\0') != std::string::npos) return false;
    return true;
}

// 直接接收 path：Windows 下 path::string() 会转成 ANSI 代码页，含中文的安装路径会读不到文件
bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

std::string statusText(int code) {
    switch (code) {
        case 200: return "OK";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 405: return "Method Not Allowed";
        case 413: return "Payload Too Large";
        case 429: return "Too Many Requests";
        case 503: return "Service Unavailable";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

std::string buildResponse(const Response& r) {
    std::ostringstream out;
    out << "HTTP/1.1 " << r.status << " " << statusText(r.status) << "\r\n";
    out << "Content-Type: " << r.contentType << "\r\n";
    out << "Content-Length: " << r.body.size() << "\r\n";
    bool cacheSet = false;
    for (const auto& [k, v] : r.extraHeaders) {
        out << k << ": " << v << "\r\n";
        if (k == "Cache-Control") cacheSet = true;
    }
    if (!cacheSet) out << "Cache-Control: no-store\r\n";
    out << "X-Content-Type-Options: nosniff\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << r.body;
    return out.str();
}

bool contentLengthOf(const std::string& head, size_t& length) {
    length = 0; bool seen = false;
    std::istringstream lines(head); std::string line;
    while (std::getline(lines, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& c : key) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        if (key == "transfer-encoding") return false;
        if (key != "content-length") continue;
        if (seen) return false;
        seen = true;
        const auto start = line.find_first_not_of(" \t", colon + 1);
        const auto end = line.find_last_not_of(" \t\r");
        if (start == std::string::npos || end < start) return false;
        for (size_t i = start; i <= end; ++i) {
            if (line[i] < '0' || line[i] > '9') return false;
            length = length * 10 + (line[i] - '0');
            if (length > kMaxBody) return false;
        }
    }
    return true;
}

// 解析请求头 + 正文
bool parseRequest(const std::string& raw, Request& req) {
    const size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::istringstream hs(raw.substr(0, headerEnd));
    std::string line;
    if (!std::getline(hs, line)) return false;
    {   // 请求行：GET /path HTTP/1.1
        std::istringstream ls(line);
        std::string version;
        ls >> req.method >> req.path >> version;
    }
    if (req.method.empty() || req.path.empty()) return false;

    // 拆出查询串
    const size_t q = req.path.find('?');
    if (q != std::string::npos) {
        req.query = req.path.substr(q + 1);
        req.path = req.path.substr(0, q);
    }
    req.path = urlDecode(req.path);

    size_t contentLength = 0;
    if (!contentLengthOf(raw.substr(0, headerEnd), contentLength) ||
        raw.size() < headerEnd + 4 + contentLength) return false;

    req.body = raw.substr(headerEnd + 4, contentLength);
    while (std::getline(hs, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0, colon);
        for (auto& ch : key) ch = static_cast<char>(::tolower(static_cast<unsigned char>(ch)));
        const auto vs = line.find_first_not_of(" \t", colon + 1);
        req.headers[key] = vs == std::string::npos ? "" : line.substr(vs);
    }
    return true;
}

} // namespace

namespace {

std::atomic<bool> g_stop{false};
std::atomic<int> g_active{0};
std::mutex g_apiMutex;   // 串行执行全部 api / tick / shutdown

void onSignal(int) { g_stop = true; }

// 每个 IP 的简单令牌桶限流（仅 /api/）
struct Bucket { double tokens = 0; std::chrono::steady_clock::time_point last; };
std::mutex g_rateMutex;
std::unordered_map<std::string, Bucket> g_buckets;

bool allowRequest(const std::string& ip) {
    constexpr double kRate = 15.0;    // 每秒补充
    constexpr double kBurst = 60.0;   // 突发上限
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(g_rateMutex);
    if (g_buckets.size() > 10000) g_buckets.clear();
    auto it = g_buckets.find(ip);
    if (it == g_buckets.end()) {
        g_buckets[ip] = Bucket{kBurst - 1, now};
        return true;
    }
    Bucket& b = it->second;
    const double dt = std::chrono::duration<double>(now - b.last).count();
    b.last = now;
    b.tokens = std::min(kBurst, b.tokens + dt * kRate);
    if (b.tokens < 1.0) return false;
    b.tokens -= 1.0;
    return true;
}

// 用途：取得静态资源实际落到的路径，供根目录边界检查使用。
// Windows 原生句柄会解析 junction；部分 MinGW filesystem::canonical 不识别此类链接。
// 使用方式：传入根目录或目标资源路径；失败写入 ec，成功返回实际路径。
std::filesystem::path resolvedStaticPath(const std::filesystem::path& path, std::error_code& ec) {
#ifdef _WIN32
    // GetFinalPathName 返回 \\?\ 长路径前缀，此时 Win32 不再自动把 URL 的 / 转成 \。
    // 目标含 assets/... 等多级路径时必须先统一分隔符，再交给 CreateFileW。
    auto nativePath = path;
    nativePath.make_preferred();
    const HANDLE handle = CreateFileW(nativePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { ec = std::error_code(GetLastError(), std::system_category()); return {}; }
    const DWORD size = GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!size) { const DWORD error = GetLastError(); CloseHandle(handle); ec = std::error_code(error, std::system_category()); return {}; }
    std::wstring finalPath(size, L'\0');
    const DWORD written = GetFinalPathNameByHandleW(handle, finalPath.data(), size, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    const DWORD error = written ? ERROR_INSUFFICIENT_BUFFER : GetLastError();
    CloseHandle(handle);
    if (!written || written >= size) { ec = std::error_code(error, std::system_category()); return {}; }
    finalPath.resize(written);
    ec.clear();
    return std::filesystem::path(finalPath);
#else
    return std::filesystem::weakly_canonical(path, ec);
#endif
}

// 静态路径：URL 解码后再校验，并确认规范化后仍在静态根目录内
bool resolveStatic(const std::string& root, const std::string& rel, std::filesystem::path& out) {
    if (rel.empty() || rel[0] != '/') return false;
    if (rel.find('\0') != std::string::npos || rel.find('\\') != std::string::npos) return false;
    for (size_t i = 0; i < rel.size();) {   // 拒绝任何 . / .. 路径段
        size_t j = rel.find('/', i + 1);
        if (j == std::string::npos) j = rel.size();
        const std::string seg = rel.substr(i + 1, j - i - 1);
        if (seg == "." || seg == "..") return false;
        i = j;
    }
    std::error_code ec;
    const auto base = resolvedStaticPath(std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(root.data()), root.size())), ec);
    if (ec) return false;
    const std::string relNoSlash = rel.substr(1);
    const auto target = resolvedStaticPath(
        base / std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(relNoSlash.data()), relNoSlash.size())), ec);
    if (ec) return false;
    // 按路径组件确认子目录，避免 Windows 窄字符串编码与分隔符影响包含判断。
    // 每个根目录组件必须匹配，且目标必须还有下一组件；同名前缀的相邻目录不算子目录。
    auto basePart = base.begin(), targetPart = target.begin();
    for (; basePart != base.end(); ++basePart, ++targetPart)
        if (targetPart == target.end() || *basePart != *targetPart) return false;
    if (targetPart == target.end()) return false;
    out = target;
    return true;
}

void sendAll(int fd, const std::string& out) {
    size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (sent < out.size() && std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        const int n = ::send(fd, out.data() + sent, static_cast<int>(out.size() - sent), 0);
#else
        const int n = static_cast<int>(::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL));
#endif
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}

void handleConnection(int fd, std::string ip, std::string staticDir, const Handler* api) {
#ifdef _WIN32
    const DWORD receiveTimeout = 10000, sendTimeout = 15000;
#else
    const timeval receiveTimeout{10, 0}, sendTimeout{15, 0};
#endif
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout));

    std::string raw;
    char buf[8192];
    size_t contentLength = 0;
    bool headersDone = false, requestComplete = false, tooLarge = false;
    // 每个连接独占一个线程，慢连接不会卡住别人；但仍限定总时长：
    // 请求头 10 秒内收齐，正文（存档导入可达数 MB）90 秒内收齐，单次空闲 10 秒即断开。
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed > std::chrono::seconds(headersDone ? 90 : 10)) break;
        const int n = static_cast<int>(::recv(fd, buf, sizeof(buf), 0));
        if (n <= 0) break;
        raw.append(buf, static_cast<size_t>(n));
        if (!headersDone) {
            const size_t he = raw.find("\r\n\r\n");
            if (he == std::string::npos) {
                if (raw.size() > kMaxHeader) break;
                continue;
            }
            headersDone = true;
            if (!contentLengthOf(raw.substr(0, he), contentLength)) { tooLarge = true; break; }
            const bool isImport = raw.rfind("POST /api/import ", 0) == 0 || raw.rfind("POST /api/import?", 0) == 0;
            if (contentLength > (isImport ? kMaxImportBody : kMaxApiBody)) { tooLarge = true; break; }
        }
        if (raw.size() > kMaxHeader + kMaxBody) { tooLarge = true; break; }
        if (raw.size() >= raw.find("\r\n\r\n") + 4 + contentLength) { requestComplete = true; break; }
    }

    Response resp;
    Request req;
    if (tooLarge) {
        resp.status = 413;
        resp.body = "{\"ok\":false,\"error\":\"请求过大（存档导入上限 6MB，其余 512KB）\"}";
    } else if (!requestComplete || !parseRequest(raw, req)) {
        resp.status = 400;
        resp.body = "{\"ok\":false,\"error\":\"请求格式错误\"}";
    } else if (req.path.rfind("/api/", 0) == 0) {
        req.clientIp = ip;
        if (!allowRequest(ip)) {
            resp.status = 429;
            resp.body = "{\"ok\":false,\"error\":\"操作太频繁，请稍后再试\"}";
        } else {
            std::lock_guard<std::mutex> lock(g_apiMutex);
            try {
                resp = (*api)(req);
            } catch (const std::exception& e) {
                resp = Response{};
                resp.status = 500;
                json::Value error(json::Object{});
                error.set("ok", false);
                error.set("error", e.what());
                resp.body = error.dump();
            }
        }
    } else if (req.method != "GET" && req.method != "HEAD") {
        resp.status = 405;
        resp.body = "{\"ok\":false,\"error\":\"不支持的请求方法\"}";
    } else {
        std::string rel = req.path == "/" ? "/index.html" : req.path;
        std::filesystem::path file;
        std::string content;
        bool found = false;
        // 地图/立绘已压缩为 WebP：请求 .png 时优先返回同名 .webp
        if (rel.size() > 4 && rel.compare(rel.size() - 4, 4, ".png") == 0) {
            const std::string webp = rel.substr(0, rel.size() - 4) + ".webp";
            if (resolveStatic(staticDir, webp, file) && readFile(file, content)) {
                found = true;
                resp.contentType = "image/webp";
            }
        }
        if (!found) {
            if (!resolveStatic(staticDir, rel, file)) {
                resp.status = 404;
                resp.body = "找不到文件";
                resp.contentType = "text/plain; charset=utf-8";
            } else if (readFile(file, content)) {
                found = true;
                resp.contentType = mimeOf(rel);
            } else {
                resp.status = 404;
                resp.body = "找不到文件";
                resp.contentType = "text/plain; charset=utf-8";
            }
        }
        if (found) {
            resp.body = std::move(content);
            if (rel.rfind("/assets/", 0) == 0)
                resp.extraHeaders.push_back({"Cache-Control", "public, max-age=604800"});
        }
        if (req.method == "HEAD") resp.body.clear();
    }

    sendAll(fd, buildResponse(resp));
    CLOSE_SOCKET(fd);
    --g_active;
}

} // namespace

int serve(const std::string& host, int port, const std::string& staticDir, Handler api,
          std::function<void()> tick, std::function<void()> shutdown) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup 失败\n");
        return 1;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    const int server = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server < 0) {
        std::fprintf(stderr, "创建 socket 失败\n");
        return 1;
    }
    int yes = 1;
    ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        std::fprintf(stderr, "监听地址无效：%s\n", host.c_str());
        CLOSE_SOCKET(server);
        return 1;
    }
    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "端口 %d 绑定失败（可能已被占用）\n", port);
        CLOSE_SOCKET(server);
        return 1;
    }
    if (::listen(server, 64) < 0) {
        std::fprintf(stderr, "listen 失败\n");
        CLOSE_SOCKET(server);
        return 1;
    }

    std::printf("宁州一梦 C++ 服务已启动：http://%s:%d/\n", host.c_str(), port);
    std::printf("静态目录：%s\n", staticDir.c_str());
    std::fflush(stdout);

    constexpr int kMaxConnections = 64;
    auto lastTick = std::chrono::steady_clock::now();
    while (!g_stop) {
#ifdef _WIN32
        WSAPOLLFD pfd{static_cast<SOCKET>(server), POLLRDNORM, 0};
        const int ready = WSAPoll(&pfd, 1, 1000);
#else
        pollfd pfd{server, POLLIN, 0};
        const int ready = ::poll(&pfd, 1, 1000);
#endif
        const auto now = std::chrono::steady_clock::now();
        if (tick && now - lastTick >= std::chrono::seconds(1)) {
            lastTick = now;
            std::lock_guard<std::mutex> lock(g_apiMutex);
            try { tick(); } catch (...) {}
        }
        if (ready <= 0) continue;

        sockaddr_in client{};
        socklen_t clientLen = sizeof(client);
        const int fd = static_cast<int>(::accept(server, reinterpret_cast<sockaddr*>(&client), &clientLen));
        if (fd < 0) continue;
        if (g_active.load() >= kMaxConnections) {
            Response busy;
            busy.status = 503;
            busy.body = "{\"ok\":false,\"error\":\"服务器繁忙\"}";
            sendAll(fd, buildResponse(busy));
            CLOSE_SOCKET(fd);
            continue;
        }
        char ipbuf[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &client.sin_addr, ipbuf, sizeof(ipbuf));
        ++g_active;
        std::thread(handleConnection, fd, std::string(ipbuf), staticDir, &api).detach();
    }

    std::printf("正在停止服务，保存玩家数据……\n");
    std::fflush(stdout);
    {
        std::lock_guard<std::mutex> lock(g_apiMutex);
        if (shutdown) {
            try { shutdown(); } catch (...) {}
        }
    }
    CLOSE_SOCKET(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

} // namespace nz::http
