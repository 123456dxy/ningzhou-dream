// 宁州一梦 · C++ 版 — 控制台界面
//
// 设计目标：能看懂、能调试。
//   - 所有菜单都在一个 switch 里，从菜单项能直接翻到规则函数
//   - 每次操作后可选打印内部状态（--debug），便于核对数值
//   - 存档是一份可读 JSON，出问题时可以直接打开看
#include <cstdio>
#include <iostream>
#include <string>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/model.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/sword.h"

namespace nz {
namespace ui {

// ============================================================
// 输入输出小工具
// ============================================================
// Windows 控制台默认不是 UTF-8，这里强制切一次，否则中文会乱码。
void setupConsole();

void clearScreen();
void line();
void title(const std::string& text);

// 读一行；EOF 时返回 false，避免在管道输入下无限循环
bool readLine(std::string& out);
int readInt(int lo, int hi, int fallback = -1);

// 宽字符对齐（中文按 2 列算），让表格看起来整齐
size_t displayWidth(const std::string& s);
std::string pad(const std::string& s, size_t width);

// ============================================================
// 由 society_ui.cpp 提供的宗门与知交界面
// ============================================================
void showSocietyMenu(Game& g);
// 结算属于 society 的在途工作；返回 true 表示已处理
bool societyJobComplete(Game& g, const Job& job);
// 野外悬赏斗法结束后的奖励
void wildBattleEnd(Game& g, const Battle& b);

} // namespace ui
} // namespace nz
