#include "core/interface/battle_lab_api.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

// 服务启动配置仅拥有值；固定 loopback、无账号存储；验证完成才调用阻塞 HTTP 服务。
struct LaunchOptions {
    int Port = 8111;
    std::string WebRoot;
};

// 用途：在 C++20 中将 UTF-8 文本变为原生路径；流程：启动资源检查；用法：传 UTF-8 路径；返回：拥有 filesystem 路径。
std::filesystem::path mfUtf8Path(const std::string& Text) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(Text.data()), Text.size()));
}

// 用途：编码原生路径供既有 HTTP 静态服务使用；流程：资源目录定位；用法：传原生路径；返回：拥有 UTF-8 字符串。
std::string mfPathText(const std::filesystem::path& Path) {
    const auto Text = Path.u8string();
    return std::string(reinterpret_cast<const char*>(Text.data()), Text.size());
}

// 用途：定位实际程序目录；流程：双击启动资源查找；用法：传完整命令行；返回：原生绝对目录，不依赖 Windows 当前工作目录。
std::filesystem::path mfExecutableDirectory(const std::vector<std::string>& Arguments) {
#ifdef _WIN32
    static_cast<void>(Arguments);
    std::wstring ExecutablePath(1024, L'\0');
    while (ExecutablePath.size() <= 32768) {
        const DWORD WrittenCharacters = GetModuleFileNameW(nullptr, ExecutablePath.data(),
            static_cast<DWORD>(ExecutablePath.size()));
        if (WrittenCharacters == 0) throw std::runtime_error("无法定位试炼程序目录。 ");
        if (WrittenCharacters < ExecutablePath.size()) {
            ExecutablePath.resize(WrittenCharacters);
            return std::filesystem::path(ExecutablePath).parent_path();
        }
        if (ExecutablePath.size() == 32768) break;
        ExecutablePath.resize(std::min<std::size_t>(32768, ExecutablePath.size() * 2));
    }
    throw std::runtime_error("试炼程序路径过长。 ");
#else
    if (Arguments.empty()) throw std::invalid_argument("缺少程序启动路径。 ");
    return std::filesystem::absolute(mfUtf8Path(Arguments.front())).parent_path();
#endif
}

// 用途：优先选取独立试炼资产；流程：无 --web 的启动；用法：传命令行；返回：已存在试炼首页的资源目录，末级回退源码 web。
std::filesystem::path mfDefaultWebRoot(const std::vector<std::string>& Arguments) {
    const std::vector<std::filesystem::path> Candidates{
        mfExecutableDirectory(Arguments) / "battle-lab-web",
        std::filesystem::current_path() / "battle-lab-web",
        std::filesystem::current_path() / "web"
    };
    for (const auto& Candidate : Candidates)
        if (std::filesystem::is_regular_file(Candidate / "battle-lab.html")) return Candidate;
    return Candidates.back();
}

// 用途：验证端口和静态目录；流程：独立试炼启动；用法：传完整命令行列表；返回：验证配置，非法输入抛 invalid_argument。
LaunchOptions mfParseLaunchOptions(const std::vector<std::string>& Arguments) {
    LaunchOptions Options;
    Options.WebRoot = mfPathText(mfDefaultWebRoot(Arguments));
    for (std::size_t ArgumentIndex = 1; ArgumentIndex < Arguments.size(); ++ArgumentIndex) {
        const auto& Argument = Arguments[ArgumentIndex];
        if (Argument == "--no-open") continue;
        if ((Argument == "--port" || Argument == "--web") && ArgumentIndex + 1 >= Arguments.size())
            throw std::invalid_argument("选项缺少参数。 ");
        if (Argument == "--port") {
            const auto& Value = Arguments[++ArgumentIndex];
            int Port = 0;
            const auto Parsed = std::from_chars(Value.data(), Value.data() + Value.size(), Port);
            if (Parsed.ec != std::errc{} || Parsed.ptr != Value.data() + Value.size() || Port < 1024 || Port > 65535)
                throw std::invalid_argument("端口须为 1024 至 65535 的整数。 ");
            Options.Port = Port;
        } else if (Argument == "--web") {
            Options.WebRoot = mfPathText(std::filesystem::absolute(mfUtf8Path(Arguments[++ArgumentIndex])));
        } else {
            throw std::invalid_argument("未知启动参数。仅支持 --port、--web 和 --no-open。 ");
        }
    }
    if (!std::filesystem::is_regular_file(mfUtf8Path(Options.WebRoot) / "battle-lab.html"))
        throw std::invalid_argument("静态目录中找不到 battle-lab.html，请通过 --web 指定本工程 web 目录。 ");
    return Options;
}

// 用途：组装内存试炼会话并监听 loopback；流程：程序主入口；用法：传已转为 UTF-8 的命令行；返回：HTTP 退出码或启动失败 1。
int mfRunBattleLab(const std::vector<std::string>& Arguments) {
    try {
        const LaunchOptions Options = mfParseLaunchOptions(Arguments);
        nz::BattleLab::Session LabSession;
        std::printf("独立斗法试炼场：http://127.0.0.1:%d/battle-lab.html\n", Options.Port);
        std::printf("只使用内存测试角色；关闭进程后不保存任何人生档。\n");
        return nz::http::serve("127.0.0.1", Options.Port, Options.WebRoot,
            [&](const nz::http::Request& Request) {
                return nz::BattleLab::mfHandle(LabSession, Request);
            });
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "试炼启动失败：%s\n", Error.what());
        return 1;
    }
}

#ifdef _WIN32
// 用途：把 Windows Unicode 命令行转换为 UTF-8；流程：wmain 启动参数；用法：传有效宽文本；返回：拥有字符串，转换失败抛错。
std::string mfUtf8Argument(const wchar_t* Text) {
    const int RequiredBytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text, -1, nullptr, 0, nullptr, nullptr);
    if (RequiredBytes <= 0) throw std::invalid_argument("命令行包含无效 Unicode。 ");
    std::string Result(static_cast<std::size_t>(RequiredBytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, Text, -1, Result.data(), RequiredBytes, nullptr, nullptr) <= 0)
        throw std::invalid_argument("无法转换 Unicode 命令行。 ");
    Result.pop_back();
    return Result;
}
#endif

} // namespace

#ifdef _WIN32
// 用途：保留 Windows 规定入口并支持中文路径；流程：独立 exe 启动；用法：系统调用；返回：服务退出码。
int wmain(int ArgumentCount, wchar_t* Arguments[]) {
    SetConsoleOutputCP(CP_UTF8);
    try {
        std::vector<std::string> Converted;
        for (int ArgumentIndex = 0; ArgumentIndex < ArgumentCount; ++ArgumentIndex)
            Converted.push_back(mfUtf8Argument(Arguments[ArgumentIndex]));
        return mfRunBattleLab(Converted);
    } catch (const std::exception& Error) {
        std::fprintf(stderr, "%s\n", Error.what());
        return 1;
    }
}
#else
// 用途：保留 POSIX 规定入口；流程：独立 exe 启动；用法：系统调用；返回：服务退出码。
int main(int ArgumentCount, char* Arguments[]) {
    return mfRunBattleLab(std::vector<std::string>(Arguments, Arguments + ArgumentCount));
}
#endif
