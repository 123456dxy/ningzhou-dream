// 宁州一梦 · C++ 版 — 存档读写（对应 JS 版的 localStorage 存档）
//
// 存档为一份 JSON 文本，字段名与 JS 版尽量一致，便于对照排查。
// 写入时先写临时文件再改名，避免中途失败把好档写坏。
#pragma once

#include <string>

#include "core/model.h"

namespace nz {

// 存档格式标识，写进 JSON 便于识别
inline constexpr const char* SAVE_FORMAT = "ningzhou-dream-cpp";

// 把存档序列化成 JSON 文本
std::string saveToJson(const Game& g);

// 从 JSON 文本读回存档。失败时返回 false 并把原因写进 err
bool loadFromJson(const std::string& text, Game& out, std::string* err);

// 写文件 / 读文件
bool saveToFile(const Game& g, const std::string& path, std::string* err);
bool loadFromFile(const std::string& path, Game& out, std::string* err);

// 存档目录：可执行文件同级的 saves/
std::string defaultSaveDir();
// 覆盖存档目录（多人服务端按玩家切换；空串恢复默认）
void setSaveDirOverride(const std::string& dir);
// 某个存档位的完整路径（slot 从 1 开始）
std::string savePathFor(int slot);

// 列出已有存档位
struct SaveSlotInfo {
    int slot = 0;
    std::string path;
    bool exists = false;
    bool automatic = false;
    std::string name;      // 角色名
    std::string realm;     // 境界
    int year = 0;
    int age = 0;
    long long bytes = 0;
};
std::vector<SaveSlotInfo> listSaveSlots();

} // namespace nz
