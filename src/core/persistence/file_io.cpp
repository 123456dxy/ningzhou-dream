// Account save paths, automatic slots and atomic file replacement.
#include "core/save.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif

namespace nz {

bool saveToFile(const Game& game, const std::string& path, std::string* err) {
    if (err) err->clear();
    const auto target = std::filesystem::u8path(path);
    std::error_code errorCode;
    if (!target.parent_path().empty()) {
        std::filesystem::create_directories(target.parent_path(), errorCode);
        if (errorCode) { if (err) *err = "无法创建存档目录：" + errorCode.message(); return false; }
    }

    const std::string text = saveToJson(game);
    // 先写临时文件再改名，避免中途失败损坏原档
    const std::string temporaryPath = path + ".tmp";
    {
        std::ofstream file(std::filesystem::u8path(temporaryPath), std::ios::binary);
        if (!file) {
            if (err) *err = "无法写入 " + temporaryPath;
            return false;
        }
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (file.fail()) {
            if (err) *err = "写入过程中出错";
            return false;
        }
    }
#ifdef _WIN32
    const bool replaced = MoveFileExW(std::filesystem::u8path(temporaryPath).c_str(), target.c_str(),
                                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(std::filesystem::u8path(temporaryPath), target, errorCode);
    const bool replaced = !errorCode;
#endif
    // 替换失败保留原档；先删除旧档会使「安全临时写入」失去意义。
    if (!replaced) {
        if (err) *err = "无法把临时存档改名为 " + path;
        return false;
    }
    return true;
}

bool loadFromFile(const std::string& path, Game& result, std::string* err) {
    if (err) err->clear();
    std::ifstream file(std::filesystem::u8path(path), std::ios::binary);
    if (!file) {
        if (err) *err = "找不到存档文件 " + path;
        return false;
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    if (file.bad()) { if (err) *err = "读取存档过程中出错"; return false; }
    return loadFromJson(contents.str(), result, err);
}

// ------------------------------------------------------------
// 存档位
// ------------------------------------------------------------
namespace {
std::string& saveDirOverrideRef() { static std::string directory; return directory; }
}
void setSaveDirOverride(const std::string& dir) { saveDirOverrideRef() = dir; }

std::string defaultSaveDir() {
    // 多人服务端：当前玩家的存档目录
    if (!saveDirOverrideRef().empty()) return saveDirOverrideRef();
    // 与可执行文件同级的 saves/；拿不到路径时退回当前目录
#ifdef _WIN32
    wchar_t modulePath[32768];
    DWORD pathLength = GetModuleFileNameW(nullptr, modulePath, 32768);
    if (pathLength > 0 && pathLength < 32768) {
        const auto utf8 = (std::filesystem::path(std::wstring(modulePath, pathLength)).parent_path() / L"saves").u8string();
        return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    }
    return "saves";
#else
    return "saves";
#endif
}

std::string savePathFor(int slot) {
    return defaultSaveDir() + "/slot" + std::to_string(slot) + ".json";
}

std::vector<SaveSlotInfo> listSaveSlots() {
    std::vector<SaveSlotInfo> result;
    for (int slot = 1; slot <= 4; ++slot) {
        SaveSlotInfo info;
        info.slot = slot;
        info.automatic = slot == 4;
        info.path = savePathFor(slot);
        std::ifstream file(std::filesystem::u8path(info.path), std::ios::binary | std::ios::ate);
        if (!file) {
            result.push_back(info);
            continue;
        }
        info.bytes = static_cast<long long>(file.tellg());
        file.close();
        info.exists = true;

        // 读出头部的角色摘要（读整份档简单可靠）
        Game game;
        std::string err;
        if (loadFromFile(info.path, game, &err)) {
            info.name = game.player.name;
            info.realm = realmName(game.player.stage);
            info.year = game.year;
            info.age = game.player.age;
        }
        result.push_back(info);
    }
    return result;
}

} // namespace nz
