// 宁州一梦 · C++ 版 — Web 服务入口
//
// 启动一个只监听本机的 HTTP 服务：
//   /            前端界面（复用 JS 版那套 HTML/CSS）
//   /api/<动作>  游戏规则（由 C++ core 提供）
//
// 用法：
//   ningzhou-web              默认 8106 端口
//   ningzhou-web --port 9000
//   ningzhou-web --web <目录>  指定前端目录（默认 ./web）
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/api.h"
#include "core/http.h"

namespace {

// 与可执行文件同级的 web/ 目录
std::string defaultWebDir() {
#ifdef _WIN32
    wchar_t buf[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buf, 32768);
    if (n > 0 && n < 32768) {
        const auto path = (std::filesystem::path(std::wstring(buf, n)).parent_path() / L"web").u8string();
        return std::string(reinterpret_cast<const char*>(path.data()), path.size());
    }
#endif
    return "web";
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);   // 中文输出
#endif

    int port = 8106;
    std::string webDir = defaultWebDir();
    bool openBrowser = true;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) port = std::atoi(argv[++i]);
        else if (a == "--web" && i + 1 < argc) webDir = argv[++i];
        else if (a == "--no-open") openBrowser = false;
        else if (a == "--help" || a == "-h") {
            std::printf(
                "宁州一梦 · C++ 版（Web 界面）\n\n"
                "用法：ningzhou-web [选项]\n"
                "  --port N      监听端口（默认 8106）\n"
                "  --web DIR     前端目录（默认可执行文件同级的 web/）\n"
                "  --no-open     启动后不自动打开浏览器\n"
                "  --help        显示帮助\n");
            return 0;
        }
    }

    // 自动开浏览器（方便双击启动）
    if (openBrowser) {
#ifdef _WIN32
        const std::string url =
            "http://127.0.0.1:" + std::to_string(port) + "/";
        std::string cmd = "cmd /c start \"\" \"" + url + "\"";
        std::thread([cmd] { system(cmd.c_str()); }).detach();
#endif
    }

    return nz::http::serve(port, webDir, nz::api::handle);
}
