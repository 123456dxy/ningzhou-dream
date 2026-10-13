// 宁州一梦单文件发行启动器。游戏及网页资源由打包脚本写入 RCDATA 101。
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define _UNICODE
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <compressapi.h>
#include <winhttp.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "core/json.h"

#ifndef NZ_PACKAGE_ID
#define NZ_PACKAGE_ID "development"
#endif

namespace fs = std::filesystem;
using nz::json::Value;

namespace {
constexpr UINT MSG_STATUS = WM_APP + 1;
constexpr UINT MSG_READY = WM_APP + 2;
constexpr UINT MSG_DONE = WM_APP + 3;
constexpr int BUTTON_OPEN = 1001, BUTTON_SAVES = 1002, BUTTON_EXIT = 1003;
constexpr size_t MAX_RESPONSE = 64 * 1024 * 1024;
constexpr uint64_t MAX_UNPACKED = 2ULL * 1024 * 1024 * 1024;

struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Internet {
    HINTERNET value = nullptr;
    explicit Internet(HINTERNET h) : value(h) {}
    ~Internet() { if (value) WinHttpCloseHandle(value); }
};
#ifndef NZ_LOCAL_MODE
// 打包本地测试版时，build-release.ps1 -Local 会以 -DNZ_LOCAL_MODE=1 编译本文件，
// 让单文件启动器默认以 --local（关闭创建角色/登录/注册与联网模块）运行内置游戏。
#define NZ_LOCAL_MODE 0
#endif
struct Options {
    fs::path dataDir;
    bool noOpen = false, extractOnly = false, headless = false, local = NZ_LOCAL_MODE != 0;
    int port = 0;
};
struct App {
    Options options;
    HWND window = nullptr, status = nullptr, openButton = nullptr;
    HFONT font = nullptr;
    std::atomic<bool> closing{false}, ready{false};
    std::atomic<int> exitCode{0};
    std::thread worker;
    std::wstring url;
    fs::path runtime;
};
App app;

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    if (value.size() > INT_MAX) throw std::runtime_error("文本过长");
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!n) throw std::runtime_error("安装包包含无效的 UTF-8 文本");
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), n);
    return result;
}
std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), n, nullptr, nullptr);
    return result;
}
std::wstring systemError(DWORD error = GetLastError()) {
    wchar_t* buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                            nullptr, error, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = n ? std::wstring(buffer, n) : L"Windows 错误 " + std::to_wstring(error);
    if (buffer) LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result;
}
[[noreturn]] void fail(const std::wstring& text) { throw std::runtime_error(utf8(text)); }
[[noreturn]] void winFail(const std::wstring& text) { fail(text + L"：" + systemError()); }
void status(const std::wstring& text) {
    auto* message = new std::wstring(text);
    if (!PostMessageW(app.window, MSG_STATUS, 0, reinterpret_cast<LPARAM>(message))) delete message;
}

std::string readFile(const fs::path& path, size_t limit = MAX_RESPONSE) {
    const auto size = fs::file_size(path);
    if (size > limit) fail(L"文件过大：" + path.wstring());
    std::ifstream input(path, std::ios::binary);
    if (!input) fail(L"无法读取：" + path.wstring());
    std::string data(static_cast<size_t>(size), '\0');
    if (size && !input.read(data.data(), static_cast<std::streamsize>(size))) fail(L"文件读取不完整：" + path.wstring());
    return data;
}
bool sameContents(const fs::path& path, const char* expected, size_t size) {
    if (!fs::is_regular_file(path) || fs::file_size(path) != size) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    char chunk[64*1024];
    size_t offset = 0;
    while (offset < size) {
        size_t count = std::min(size-offset, sizeof(chunk));
        if (!input.read(chunk, static_cast<std::streamsize>(count)) || std::memcmp(chunk, expected+offset, count)) return false;
        offset += count;
    }
    return true;
}
void checkNormalPath(const fs::path& root, const fs::path& target) {
    DWORD rootFlags = GetFileAttributesW(root.c_str());
    if (rootFlags != INVALID_FILE_ATTRIBUTES && (rootFlags & FILE_ATTRIBUTE_REPARSE_POINT))
        fail(L"安装目录不能是目录链接");
    auto relative = target.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) fail(L"安装包路径越界");
    fs::path current = root;
    for (const auto& part : relative) {
        if (part == L"..") fail(L"安装包路径越界");
        current /= part;
        DWORD flags = GetFileAttributesW(current.c_str());
        if (flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            fail(L"安装目录包含链接，无法安全写入：" + current.wstring());
    }
}
void atomicWrite(const fs::path& path, const char* data, size_t size) {
    checkNormalPath(app.options.dataDir, path);
    fs::create_directories(path.parent_path());
    checkNormalPath(app.options.dataDir, path);
    auto temporary = path;
    temporary += L".nz-writing-" + std::to_wstring(GetCurrentProcessId());
    checkNormalPath(app.options.dataDir, temporary);
    // CREATE_ALWAYS 只用于本进程专有的临时文件；最终文件由同目录原子替换。
    Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) winFail(L"无法写入临时文件");
    size_t done = 0;
    while (done < size) {
        DWORD length = static_cast<DWORD>(std::min<size_t>(size - done, 1024 * 1024));
        DWORD written = 0;
        if (!WriteFile(output.value, data + done, length, &written, nullptr) || written != length) {
            DWORD error = GetLastError();
            CloseHandle(output.value); output.value = nullptr;
            DeleteFileW(temporary.c_str());
            fail(L"文件写入失败：" + systemError(error));
        }
        done += written;
    }
    if (!FlushFileBuffers(output.value)) winFail(L"文件刷新失败");
    CloseHandle(output.value); output.value = nullptr;
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError(); DeleteFileW(temporary.c_str());
        fail(L"无法替换文件：" + systemError(error));
    }
}
void atomicWrite(const fs::path& path, const std::string& data) { atomicWrite(path, data.data(), data.size()); }

uint64_t integer(const unsigned char*& cursor, const unsigned char* end, unsigned count) {
    if (static_cast<size_t>(end - cursor) < count) fail(L"安装包被截断");
    uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i) result |= static_cast<uint64_t>(*cursor++) << (i * 8);
    return result;
}
fs::path checkedRelative(const std::string& name) {
    auto text = wide(name);
    if (text.empty() || text.front() == L'/' || text.front() == L'\\') fail(L"安装包包含绝对路径");
    for (wchar_t c : text)
        if (c < 32 || c == L':' || c == L'*' || c == L'?' || c == L'"' || c == L'<' || c == L'>' || c == L'|')
            fail(L"安装包包含非法路径字符");
    std::replace(text.begin(), text.end(), L'\\', L'/');
    std::wstringstream input(text);
    std::wstring part;
    while (std::getline(input, part, L'/')) {
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ')
            fail(L"安装包包含非法路径分段");
        std::wstring upper = part;
        for (auto& c : upper) c = towupper(c);
        auto base = upper.substr(0, upper.find(L'.'));
        if (base == L"CON" || base == L"PRN" || base == L"AUX" || base == L"NUL" ||
            (base.size() == 4 && (base.substr(0,3) == L"COM" || base.substr(0,3) == L"LPT") && base[3] >= L'1' && base[3] <= L'9'))
            fail(L"安装包包含 Windows 保留文件名");
        if (upper == L"SAVES" || upper == L"LATEST-SESSION.JSON" || upper == L"LAUNCHER-STATE.JSON" || upper == L".PACKAGE-ID")
            fail(L"安装包不允许包含用户存档或启动器状态");
    }
    if (text.back() == L'/') fail(L"安装包包含空文件名");
    fs::path relative(text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) fail(L"安装包路径非法");
    return relative;
}

void extract() {
    status(L"正在准备游戏资源，首次启动可能需要片刻……");
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!resource) winFail(L"未找到游戏资源");
    DWORD compressedSize = SizeofResource(nullptr, resource);
    HGLOBAL loaded = LoadResource(nullptr, resource);
    const auto* bytes = static_cast<const unsigned char*>(LockResource(loaded));
    if (!bytes || compressedSize < 20 || std::memcmp(bytes, "NZEXEP01", 8)) fail(L"游戏资源格式无效");
    const unsigned char* cursor = bytes + 8;
    const auto* compressedEnd = bytes + compressedSize;
    uint64_t rawSize = integer(cursor, compressedEnd, 8);
    uint32_t algorithm = static_cast<uint32_t>(integer(cursor, compressedEnd, 4));
    if (rawSize < 12 || rawSize > MAX_UNPACKED || rawSize > SIZE_MAX || algorithm != COMPRESS_ALGORITHM_XPRESS_HUFF)
        fail(L"游戏资源尺寸或压缩算法无效");
    std::vector<unsigned char> raw(static_cast<size_t>(rawSize));
    DECOMPRESSOR_HANDLE decompressor = nullptr;
    if (!CreateDecompressor(algorithm, nullptr, &decompressor)) winFail(L"无法创建资源解压器");
    SIZE_T actual = 0;
    BOOL ok = Decompress(decompressor, const_cast<unsigned char*>(cursor), static_cast<SIZE_T>(compressedEnd-cursor), raw.data(), raw.size(), &actual);
    DWORD error = GetLastError();
    CloseDecompressor(decompressor);
    if (!ok || actual != raw.size()) fail(L"游戏资源解压失败：" + systemError(error));
    cursor = raw.data();
    const unsigned char* end = cursor + raw.size();
    if (std::memcmp(cursor, "NZFILE01", 8)) fail(L"资源文件列表格式无效");
    cursor += 8;
    uint64_t count = integer(cursor, end, 4);
    if (count == 0 || count > 100000) fail(L"资源文件数量无效");
    struct File { fs::path path; const char* data; size_t size; };
    std::vector<File> files;
    files.reserve(static_cast<size_t>(count));
    bool serverFound = false;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t pathSize = integer(cursor, end, 4);
        uint64_t fileSize = integer(cursor, end, 8);
        if (!pathSize || pathSize > 32767 || pathSize > static_cast<uint64_t>(end-cursor)) fail(L"资源路径长度无效");
        std::string name(reinterpret_cast<const char*>(cursor), static_cast<size_t>(pathSize)); cursor += pathSize;
        auto relative = checkedRelative(name);
        if (fileSize > static_cast<uint64_t>(end-cursor)) fail(L"资源文件内容不完整");
        if (relative == fs::path(L"runtime/ningzhou_web.exe")) serverFound = true;
        files.push_back({app.options.dataDir/relative, reinterpret_cast<const char*>(cursor), static_cast<size_t>(fileSize)});
        cursor += fileSize;
    }
    if (cursor != end || !serverFound) fail(L"游戏资源列表无效或缺少游戏程序");
    const auto marker = app.options.dataDir/L".package-id";
    checkNormalPath(app.options.dataDir, marker);
    bool current = fs::exists(marker) && readFile(marker, 1024) == NZ_PACKAGE_ID;
    for (size_t i = 0; i < files.size(); ++i) {
        if (app.closing) return;
        const auto& file = files[i];
        checkNormalPath(app.options.dataDir, file.path);
        if (!current || !sameContents(file.path, file.data, file.size))
            atomicWrite(file.path, file.data, file.size);
        if (i % 40 == 0) status(L"正在准备游戏资源（"+std::to_wstring(i+1)+L" / "+std::to_wstring(files.size())+L"）……");
    }
    if (!app.closing) atomicWrite(marker, NZ_PACKAGE_ID);
    checkNormalPath(app.options.dataDir, app.runtime/L"saves");
    fs::create_directories(app.runtime/L"saves");
    checkNormalPath(app.options.dataDir, app.runtime/L"saves");
}

struct Response { DWORD code = 0; std::string body; };
Response request(int port, const wchar_t* method, const wchar_t* path, const std::string& body = {}) {
    Internet session(WinHttpOpen(L"NingzhouDreamLauncher/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session.value) winFail(L"无法初始化本机连接");
    WinHttpSetTimeouts(session.value, 1000, 1000, 1500, 2500);
    Internet connect(WinHttpConnect(session.value, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0));
    if (!connect.value) winFail(L"无法连接游戏");
    Internet call(WinHttpOpenRequest(connect.value, method, path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0));
    if (!call.value) winFail(L"无法创建游戏请求");
    DWORD noRedirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(call.value, WINHTTP_OPTION_REDIRECT_POLICY, &noRedirect, sizeof(noRedirect));
    const wchar_t* headers = L"Content-Type: application/json; charset=utf-8\r\n";
    if (body.size() > MAX_RESPONSE || !WinHttpSendRequest(call.value, headers, static_cast<DWORD>(-1L),
            body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
        !WinHttpReceiveResponse(call.value, nullptr)) winFail(L"游戏连接失败");
    Response result;
    DWORD length = sizeof(result.code);
    if (!WinHttpQueryHeaders(call.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &result.code, &length, WINHTTP_NO_HEADER_INDEX)) winFail(L"无法读取游戏响应");
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(call.value, &available)) winFail(L"游戏响应读取失败");
        if (!available) break;
        if (result.body.size()+available > MAX_RESPONSE) fail(L"游戏响应过大");
        size_t offset = result.body.size(); result.body.resize(offset+available);
        DWORD received = 0;
        if (!WinHttpReadData(call.value, result.body.data()+offset, available, &received)) winFail(L"游戏响应读取失败");
        result.body.resize(offset+received);
        if (!received) break;
    }
    return result;
}
Value parsed(const Response& response) {
    std::string error;
    Value value = Value::parse(response.body, &error);
    if (!error.empty() || !value.isObject()) fail(L"游戏返回了无效数据");
    return value;
}
std::wstring apiError(const Value& value) {
    if (value["error"].isString()) return wide(value["error"].asString());
    if (value["message"].isString()) return wide(value["message"].asString());
    return L"游戏未接受此请求";
}
bool validExport(const Value& data) {
    if (!data["ok"].asBool() || !data["game"].isString() || data["game"].asString().empty()) return false;
    std::string error;
    auto game = Value::parse(data["game"].asString(), &error);
    return error.empty() && game.isObject() && game["player"].isObject() &&
           game["player"]["name"].isString() && !game["player"]["name"].asString().empty();
}

struct ReservedPort {
    SOCKET value = INVALID_SOCKET;
    int port = 0;
    ~ReservedPort() { if (value != INVALID_SOCKET) closesocket(value); }
};
void reservePort(ReservedPort& reserved, int requested) {
    reserved.value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (reserved.value == INVALID_SOCKET) fail(L"无法寻找本机可用端口");
    BOOL exclusive = TRUE;
    setsockopt(reserved.value, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    sockaddr_in address{}; address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); address.sin_port = htons(static_cast<unsigned short>(requested));
    if (bind(reserved.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR)
        fail(L"指定端口正在使用，请关闭该实例或省略 --port 参数");
    int size = sizeof(address);
    if (getsockname(reserved.value, reinterpret_cast<sockaddr*>(&address), &size) == SOCKET_ERROR) fail(L"无法读取本机端口");
    reserved.port = ntohs(address.sin_port);
}
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\""; unsigned slashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') result.append(slashes*2+1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0; result.push_back(c);
    }
    result.append(slashes*2, L'\\'); result += L'"'; return result;
}
void writeState(int port, DWORD pid, bool running, bool restored = false) {
    Value state(nz::json::Object{});
    state.set("packageId", NZ_PACKAGE_ID);
    state.set("url", "http://127.0.0.1:"+std::to_string(port)+"/");
    state.set("port", port); state.set("serverPid", static_cast<double>(pid));
    state.set("launcherPid", static_cast<double>(GetCurrentProcessId()));
    state.set("dataDir", utf8(app.options.dataDir.wstring()));
    state.set("ready", running); state.set("running", running); state.set("restored", restored);
    atomicWrite(app.runtime/L"launcher-state.json", state.dump(2));
}
void openGame() {
    if (!app.ready) return;
    auto result = ShellExecuteW(app.window, L"open", app.url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) status(L"无法自动打开浏览器，请复制此地址："+app.url);
}

void worker() {
    try {
        fs::create_directories(app.options.dataDir);
        DWORD rootAttributes = GetFileAttributesW(app.options.dataDir.c_str());
        if (rootAttributes != INVALID_FILE_ATTRIBUTES && (rootAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) fail(L"安装目录不能是目录链接");
        extract();
        if (app.options.extractOnly || app.closing) { PostMessageW(app.window, MSG_DONE, 0, 0); return; }
        fs::remove(app.runtime/L"launcher-stop");
        status(L"正在启动游戏……");
        WSADATA winsock{};
        if (WSAStartup(MAKEWORD(2,2), &winsock)) fail(L"无法初始化本机网络");
        struct Cleanup { ~Cleanup() { WSACleanup(); } } cleanup;
        ReservedPort reserved; reservePort(reserved, app.options.port);
        int port = reserved.port;
        Handle job(CreateJobObjectW(nullptr, nullptr));
        if (!job.value) winFail(L"无法创建游戏进程管理器");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) winFail(L"无法设置游戏退出保护");
        fs::path executable = app.runtime/L"ningzhou_web.exe";
        std::wstring command = quote(executable.wstring())+L" --port "+std::to_wstring(port)+L" --no-open";
        if (app.options.local) command += L" --local";
        STARTUPINFOW startup{}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, app.runtime.c_str(), &startup, &process)) winFail(L"无法启动游戏程序");
        Handle child(process.hProcess), thread(process.hThread);
        if (!AssignProcessToJobObject(job.value, child.value)) {
            DWORD error = GetLastError(); TerminateProcess(child.value, 1);
            fail(L"无法管理游戏进程："+systemError(error));
        }
        closesocket(reserved.value); reserved.value = INVALID_SOCKET;
        if (ResumeThread(thread.value) == static_cast<DWORD>(-1)) winFail(L"无法运行游戏进程");
        bool ready = false;
        for (int attempt = 0; attempt < 80 && !app.closing; ++attempt) {
            if (WaitForSingleObject(child.value, 0) == WAIT_OBJECT_0) fail(L"游戏程序在启动时退出，请检查安装目录和杀毒软件提示");
            try {
                auto response = request(port, L"GET", L"/api/state");
                ready = response.code == 200 && parsed(response)["ok"].asBool();
            } catch (...) {}
            if (ready) break;
            Sleep(250);
        }
        if (!ready && !app.closing) fail(L"游戏启动超时，请检查本机连接和杀毒软件提示");
        bool restored = false;
        const fs::path sessionPath = app.runtime/L"latest-session.json";
        checkNormalPath(app.options.dataDir, sessionPath);
        if (ready && !app.closing && !app.options.local && fs::exists(sessionPath)) {
            status(L"正在恢复上次游玩进度……");
            auto saved = readFile(sessionPath);
            std::string error; auto data = Value::parse(saved, &error);
            if (!error.empty() || !validExport(data)) fail(L"自动续玩文件损坏，已保留原文件。可从存档目录备份后重命名 latest-session.json 再启动");
            auto response = request(port, L"POST", L"/api/import", saved);
            auto imported = parsed(response);
            if (response.code != 200 || !imported["ok"].asBool()) fail(L"恢复上次进度失败，已保留原文件："+apiError(imported));
            restored = true;
        }
        if (ready && !app.closing) {
            app.url = L"http://127.0.0.1:"+std::to_wstring(port)+L"/";
            app.ready = true;
            writeState(port, process.dwProcessId, true, restored);
            status(std::wstring(restored ? L"已恢复上次进度。" : L"游戏已启动。")+L"\r\n"+app.url+L"\r\n关闭此窗口会保存续玩进度并退出游戏。手动存档仍在游戏内保存。");
            PostMessageW(app.window, MSG_READY, 0, 0);
        }
        while (!app.closing) {
            if (fs::exists(app.runtime/L"launcher-stop")) { fs::remove(app.runtime/L"launcher-stop"); app.closing = true; break; }
            if (WaitForSingleObject(child.value, 250) == WAIT_OBJECT_0) fail(L"游戏服务意外退出。上次自动续玩文件和手动存档仍保留在存档目录");
        }
        app.ready = false;
        if (ready && WaitForSingleObject(child.value, 0) != WAIT_OBJECT_0) {
            status(L"正在保存续玩进度并退出……");
            auto response = request(port, L"GET", L"/api/state");
            auto state = parsed(response);
            if (response.code != 200 || !state["ok"].asBool()) fail(L"退出前无法读取游戏状态");
            // 本地测试版关闭了 /api/export，进度由内置游戏自己的存档目录保存，不走续玩导出。
            if (state["hasGame"].asBool() && !app.options.local) {
                auto exported = request(port, L"GET", L"/api/export");
                auto data = parsed(exported);
                if (exported.code != 200 || !validExport(data)) fail(L"自动续玩保存失败："+apiError(data));
                atomicWrite(sessionPath, exported.body);
            }
        }
        writeState(port, process.dwProcessId, false, restored);
        // job 在作用域结束时只终止本启动器创建的服务，不接触其他实例。
    } catch (const std::exception& error) {
        app.exitCode = 1;
        app.ready = false;
        std::wstring message;
        try { message = wide(error.what()); } catch (...) { message = L"游戏启动或保存失败"; }
        status(message+L"\r\n数据目录："+app.options.dataDir.wstring());
        try { atomicWrite(app.runtime/L"launcher-error.txt", utf8(message)); } catch (...) {}
        if (!app.options.headless && !app.options.extractOnly && app.closing)
            MessageBoxW(app.window, message.c_str(), L"宁州一梦：续玩保存未完成", MB_OK | MB_ICONWARNING);
        if (!app.options.headless && !app.options.extractOnly && !app.closing) return;
    }
    PostMessageW(app.window, MSG_DONE, 0, 0);
}

void layout(HWND window) {
    RECT client{}; GetClientRect(window, &client);
    int width = client.right, height = client.bottom;
    if (app.status) MoveWindow(app.status, 28, 82, width-56, std::max(72, height-174), TRUE);
    if (app.openButton) MoveWindow(app.openButton, 28, height-62, 140, 36, TRUE);
    MoveWindow(GetDlgItem(window, BUTTON_SAVES), 182, height-62, 170, 36, TRUE);
    MoveWindow(GetDlgItem(window, BUTTON_EXIT), width-130, height-62, 100, 36, TRUE);
}
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_SIZE: layout(window); return 0;
    case MSG_STATUS: {
        std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lparam));
        if (app.status) SetWindowTextW(app.status, text->c_str());
        return 0;
    }
    case MSG_READY:
        if (app.openButton) EnableWindow(app.openButton, TRUE);
        if (!app.options.noOpen && !app.options.headless && !app.closing) openGame();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wparam) == BUTTON_OPEN) openGame();
        if (LOWORD(wparam) == BUTTON_SAVES) {
            // 自动续玩和手动存档都能从这里找到，亦可按错误提示备份自动续玩文件。
            fs::path folder = app.runtime;
            try { checkNormalPath(app.options.dataDir, folder); fs::create_directories(folder); ShellExecuteW(window, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
            catch (...) { MessageBoxW(window, L"无法打开存档目录。", L"宁州一梦", MB_OK | MB_ICONWARNING); }
        }
        if (LOWORD(wparam) == BUTTON_EXIT) SendMessageW(window, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        if (app.exitCode || !app.worker.joinable()) { DestroyWindow(window); return 0; }
        app.closing = true;
        if (app.openButton) EnableWindow(app.openButton, FALSE);
        SetWindowTextW(app.status, L"正在保存续玩进度并退出，请稍候……");
        return 0;
    case MSG_DONE: DestroyWindow(window); return 0;
    case WM_DESTROY: PostQuitMessage(app.exitCode); return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

Options arguments() {
    Options options;
    // FOLDERID_LocalAppData；内联 GUID 避免额外依赖 uuid.dll/import library。
    constexpr GUID localAppData = {0xf1b32785,0x6fba,0x4fcf,{0x9d,0x55,0x7b,0x8e,0x7f,0x15,0x70,0x91}};
    PWSTR directory = nullptr;
    if (FAILED(SHGetKnownFolderPath(localAppData, KF_FLAG_CREATE, nullptr, &directory))) fail(L"无法读取本机用户数据目录");
    options.dataDir = fs::path(directory)/L"NingzhouDream"/L"game"; CoTaskMemFree(directory);
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) winFail(L"无法读取启动参数");
    std::unique_ptr<wchar_t*, decltype(&LocalFree)> owner(argv, &LocalFree);
    for (int i = 1; i < argc; ++i) {
        const std::wstring argument = argv[i];
        if (argument == L"--no-open") options.noOpen = true;
        else if (argument == L"--local") options.local = true;
        else if (argument == L"--online") options.local = false;
        else if (argument == L"--headless") { options.headless = true; options.noOpen = true; }
        else if (argument == L"--extract-only") { options.extractOnly = true; options.noOpen = true; }
        else if (argument == L"--data-dir" && i+1 < argc) options.dataDir = argv[++i];
        else if (argument == L"--port" && i+1 < argc) {
            wchar_t* tail = nullptr;
            long port = wcstol(argv[++i], &tail, 10);
            if (!tail || *tail || port < 1 || port > 65535) fail(L"--port 必须为 1 到 65535");
            options.port = static_cast<int>(port);
        } else fail(L"未知或不完整的启动参数："+argument);
    }
    options.dataDir = fs::absolute(options.dataDir).lexically_normal();
    if (options.dataDir == options.dataDir.root_path()) fail(L"游戏数据目录不能是磁盘根目录");
    return options;
}
std::wstring mutexName(const fs::path& path) {
    uint64_t hash = 1469598103934665603ULL;
    for (wchar_t c : path.wstring()) { hash ^= static_cast<uint64_t>(towlower(c)); hash *= 1099511628211ULL; }
    std::wostringstream name; name << L"Local\\NingzhouDreamLauncher-" << std::hex << hash;
    return name.str();
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    try {
        // 参数错误也应在验收模式直接退出，避免无人值守时停在消息框。
        int earlyArgc = 0;
        LPWSTR* earlyArgs = CommandLineToArgvW(GetCommandLineW(), &earlyArgc);
        if (earlyArgs) {
            for (int i=1; i<earlyArgc; ++i) {
                if (std::wstring(earlyArgs[i]) == L"--headless") app.options.headless = true;
                if (std::wstring(earlyArgs[i]) == L"--extract-only") app.options.extractOnly = true;
            }
            LocalFree(earlyArgs);
        }
        app.options = arguments();
        app.runtime = app.options.dataDir/L"runtime";
        Handle singleton(CreateMutexW(nullptr, TRUE, mutexName(app.options.dataDir).c_str()));
        if (!singleton.value) winFail(L"无法检查游戏运行状态");
        if (GetLastError() == ERROR_ALREADY_EXISTS) fail(L"此数据目录中的游戏已经运行。请使用原来的游戏窗口，或关闭后再启动");
        WNDCLASSW cls{}; cls.hInstance = instance; cls.lpfnWndProc = windowProc;
        cls.lpszClassName = L"NingzhouDreamReleaseLauncher";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW); cls.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        if (!RegisterClassW(&cls)) winFail(L"无法创建游戏启动窗口");
        app.window = CreateWindowExW(0, cls.lpszClassName, L"宁州一梦 · 游戏启动器", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                     CW_USEDEFAULT, CW_USEDEFAULT, 610, 340, nullptr, nullptr, instance, nullptr);
        if (!app.window) winFail(L"无法打开游戏启动窗口");
        if (!app.options.headless && !app.options.extractOnly) {
            app.font = CreateFontW(-18, 0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
            auto label = CreateWindowW(L"STATIC", L"宁州一梦", WS_CHILD|WS_VISIBLE, 28,24,540,36,app.window,nullptr,instance,nullptr);
            app.status = CreateWindowW(L"STATIC", L"正在准备游戏……", WS_CHILD|WS_VISIBLE|SS_LEFT,28,82,540,128,app.window,nullptr,instance,nullptr);
            app.openButton = CreateWindowW(L"BUTTON", L"打开游戏", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,28,242,140,36,app.window,reinterpret_cast<HMENU>(BUTTON_OPEN),instance,nullptr);
            auto saves = CreateWindowW(L"BUTTON", L"打开存档文件夹", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,182,242,170,36,app.window,reinterpret_cast<HMENU>(BUTTON_SAVES),instance,nullptr);
            auto exit = CreateWindowW(L"BUTTON", L"退出", WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_PUSHBUTTON,450,242,100,36,app.window,reinterpret_cast<HMENU>(BUTTON_EXIT),instance,nullptr);
            for (auto control : {label, app.status, app.openButton, saves, exit}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(app.font), TRUE);
            EnableWindow(app.openButton, FALSE); layout(app.window);
            ShowWindow(app.window, show); UpdateWindow(app.window);
        }
        app.worker = std::thread(worker);
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
        app.closing = true;
        if (app.worker.joinable()) app.worker.join();
        if (app.font) DeleteObject(app.font);
        return app.exitCode;
    } catch (const std::exception& error) {
        std::wstring text;
        try { text = wide(error.what()); } catch (...) { text = L"启动失败"; }
        if (!app.options.headless && !app.options.extractOnly) MessageBoxW(nullptr,text.c_str(),L"宁州一梦",MB_OK|MB_ICONERROR);
        return 1;
    }
}
