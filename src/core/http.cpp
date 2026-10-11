// 宁州一梦 · C++ 版 — 极简 HTTP 服务实现
#include "core/http.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <map>
#include <chrono>
#include <sstream>

#include "core/json.h"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#define CLOSE_SOCKET closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define CLOSE_SOCKET close
#endif

namespace nz::http {

namespace {

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

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size())), std::ios::binary);
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
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

std::string buildResponse(const Response& r) {
    std::ostringstream out;
    out << "HTTP/1.1 " << r.status << " " << statusText(r.status) << "\r\n";
    out << "Content-Type: " << r.contentType << "\r\n";
    out << "Content-Length: " << r.body.size() << "\r\n";
    // 本地调试用，禁止缓存，改完前端刷新就能看到
    out << "Cache-Control: no-store\r\n";
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
            if (length > 8u * 1024 * 1024) return false;
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
    return true;
}

} // namespace

int serve(int port, const std::string& staticDir, Handler api) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup 失败\n");
        return 1;
    }
#endif

    const int server = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (server < 0) {
        std::fprintf(stderr, "创建 socket 失败\n");
        return 1;
    }

    // 允许快速重启（避免 TIME_WAIT 占端口）
    int yes = 1;
    ::setsockopt(server, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&yes), sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);   // 只监听本机

    if (::bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::fprintf(stderr, "端口 %d 绑定失败（可能已被占用）\n", port);
        CLOSE_SOCKET(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }
    if (::listen(server, 16) < 0) {
        std::fprintf(stderr, "listen 失败\n");
        CLOSE_SOCKET(server);
        return 1;
    }

    std::printf("宁州一梦 C++ 服务已启动：http://127.0.0.1:%d/\n", port);
    std::printf("静态目录：%s\n", staticDir.c_str());
    std::printf("按 Ctrl+C 停止。\n");
    std::fflush(stdout);

    // 单线程循环。本地单人游戏，不需要并发。
    while (true) {
        sockaddr_in client{};
        socklen_t clientLen = sizeof(client);
        const int fd = static_cast<int>(
            ::accept(server, reinterpret_cast<sockaddr*>(&client), &clientLen));
        if (fd < 0) continue;
#ifdef _WIN32
        const DWORD receiveTimeout = 1500, sendTimeout = 4000;
#else
        const timeval receiveTimeout{1, 500000}, sendTimeout{4, 0};
#endif
        if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&receiveTimeout), sizeof(receiveTimeout)) != 0 ||
            ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&sendTimeout), sizeof(sendTimeout)) != 0) {
            CLOSE_SOCKET(fd);
            continue;
        }

        // 读到请求头结束
        std::string raw;
        char buf[8192];
        size_t contentLength = 0;
        bool headersDone = false;
        bool requestComplete = false;
        const auto requestDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (true) {
            if (std::chrono::steady_clock::now() >= requestDeadline) break;
            const int n = static_cast<int>(::recv(fd, buf, sizeof(buf), 0));
            if (n <= 0) break;
            raw.append(buf, static_cast<size_t>(n));
            if (!headersDone) {
                const size_t he = raw.find("\r\n\r\n");
                if (he != std::string::npos) {
                    headersDone = true;
                    if (!contentLengthOf(raw.substr(0, he), contentLength)) break;
                }
            }
            if (headersDone && contentLength <= 8u * 1024 * 1024 && raw.size() >= raw.find("\r\n\r\n") + 4 + contentLength) {
                requestComplete = true;
                break;
            }
            if (raw.size() > 8u * 1024 * 1024) break;   // 防御：超大请求
        }

        Response resp;
        Request req;
        if (!requestComplete || !parseRequest(raw, req)) {
            resp.status = 400;
            resp.body = "{\"error\":\"请求格式错误\"}";
        } else if (req.path.rfind("/api/", 0) == 0) {
            // 交给游戏接口
            try {
                resp = api(req);
            } catch (const std::exception& e) {
                resp.status = 500;
                json::Value error(json::Object{});
                error.set("ok", false);
                error.set("error", e.what());
                resp.body = error.dump();
            }
        } else {
            // 静态文件
            std::string rel = req.path == "/" ? "/index.html" : req.path;
            if (!isSafePath(rel)) {
                resp.status = 400;
                resp.body = "路径不合法";
                resp.contentType = "text/plain; charset=utf-8";
            } else {
                std::string content;
                if (readFile(staticDir + rel, content)) {
                    resp.body = std::move(content);
                    resp.contentType = mimeOf(rel);
                } else {
                    resp.status = 404;
                    resp.body = "找不到 " + rel;
                    resp.contentType = "text/plain; charset=utf-8";
                }
            }
        }

        const std::string out = buildResponse(resp);
        size_t sent = 0;
        while (sent < out.size()) {
            const int n = static_cast<int>(
                ::send(fd, out.data() + sent,
                       static_cast<int>(out.size() - sent), 0));
            if (n <= 0) break;
            sent += static_cast<size_t>(n);
        }
        CLOSE_SOCKET(fd);
    }

    CLOSE_SOCKET(server);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}

} // namespace nz::http
