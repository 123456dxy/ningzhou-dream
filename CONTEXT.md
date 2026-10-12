# 会话上下文（压缩版）

> 这份文件用于在新会话里快速接上进度。记录的是**当前事实**，不是历史叙述。

## 任务

把《觅长生 · 宁州一梦》JS 版（Demo 07）的规则移植成 C++，目标是**能读懂、能调试**。
用户原话：「帮我把宁州一梦做成c++的版本，我才能看懂代码并进行调试」。

## 两个目录

| 路径 | 说明 |
| --- | --- |
| `michangsheng-demo/` | **原版 JS 游戏**（不要改它）。当前是 Demo 07 |
| `ningzhou-cpp/` | 本次交付的 C++ 版 |

## 当前状态：已完成

- **507 项自测全部通过，0 失败**
- 控制台界面可玩（建号 → 行动 → 斗法 → 游历 → 关系 → 存读档）
- 存档是缩进格式化的可读 JSON
- VS Code 配置齐备（`.vscode/` 有 settings / launch / c_cpp_properties）

### JS 版覆盖情况

JS 版从 Demo 05 直接跳到 Demo 07，两个大版本都已跟上：

| 模块 | JS 源 | C++ 实现 | 状态 |
| --- | --- | --- | --- |
| 人生与天下 | sim.mjs | sim.cpp | 已移植 |
| 月历 | calendar.mjs | calendar.cpp | 已移植，逐值验证 |
| 洞府道法 | arts.mjs | arts.cpp | 已移植，逐值验证 |
| 斗法 | battle.mjs | battle.cpp | **Demo07 卡牌制重写**，逐次出牌验证 |
| 剑道 | sword.mjs | sword.cpp | Demo06 新增，已移植 |
| 营生 | economy.mjs | economy.cpp | 已移植 |
| 宗门知交 | society.mjs | society.cpp | 已移植（含 8 段支线、3 类悬赏） |
| 游历后山 | adventure.mjs | adventure.cpp | Demo06 新增，已移植 |
| JSON / 存档 | — | json.cpp / save.cpp | C++ 独有 |

## 工具链

- MSYS2 UCRT64：g++ 16.2.0 / gdb 18.1 / cmake 4.4.3 / ninja 1.13.2
- 装在 `C:\msys64`

### 两个必须记住的坑

1. **`C:\msys64\ucrt64\bin` 必须进 PATH**。
   否则 `cc1plus.exe` / 生成的 exe 会以 `0xC0000135`（找不到 DLL）**静默失败**——
   不打印任何错误。`build.ps1` 已处理。
2. **PowerShell 把编译器 stderr 当终止错误**。
   `build.ps1` 用 `Tee-Object` 落盘后按退出码判断，失败时打印最后 40 行日志。

### 常用命令

```powershell
cd ningzhou-cpp
.\build.ps1 -Run            # 构建并启动
.\build.ps1 -Test           # 构建并跑 507 项自测
.\build.ps1 -Debug -Run     # 带调试符号
.\build\ningzhou.exe --debug        # 打印内部状态后退出
.\build\ningzhou.exe --new          # 直接建号
.\build\ningzhou.exe --load 1       # 读存档位 1
```

## 架构要点

### 数据表是自动生成的，不要手改

```
michangsheng-demo/*.mjs
   ↓ node tools/dump-data.mjs > tools/data-dump.json
   ↓ node tools/dump-society.mjs > tools/society-dump.json
   ↓ python tools/extract-data.py
ningzhou-cpp/data/reference_tables.h   ← 自动生成，勿手改
```

已导出：157 药材 / 126 丹方 / 7 丹炉 / 40 功法 / 40 神通 / 8 攻略 /
12 材料 / 5 器型 / 12 机缘 / 14 成就 / 39 天赋 / 5 开局天赋 /
6 游历类型 / 7 剑道境界 / 8 支线 / 3 悬赏。

### 字段名与 JS 对齐

`model.h` 里的结构体字段尽量与 JS 保持一致，方便两边对着读：
`g.player.arts.poison`、`g.bonds.payroll.balance`、`g.adventure.since` 等。

### 分层

- `src/core/` —— 纯规则，不依赖界面
- `src/ui.cpp` —— 控制台界面，只调用 core 的公开函数
- `tests/` —— 分模块自测，与 JS 逐值对照

## 调试方法论（重要）

**发现数值不一致时，先跑 JS 确认真实行为，再决定改代码还是改测试。**

`tools/*.mjs` 是直接 import JS 版模块、打印参考值的脚本：

| 脚本 | 产出 |
| --- | --- |
| `cards-check.mjs` | 卡牌斗法逐次出牌结果 |
| `artmods-check.mjs` | 洞府加成 |
| `build-check.mjs` | 人物生成 |
| `society-check.mjs` | 俸禄账簿 |
| `craft-check.mjs` | 炼丹炼器 |
| `battle-check.mjs` | 旧版斗法（Demo07 已过时，保留参考） |
| `rng-check.mjs` | 随机数序列 |
| `name-check.mjs` | NPC 姓名 |
| `birth-check.mjs` | 五项开局天赋物品 |

开发中这个方法抓到过 **3 次「我自己的期望值写错了」**：
灵击张数、守势护盾预览、恶名阈值（是 40 不是 41）。
每次都确认了 JS 的真实行为，没有为了测试变绿去改代码逻辑。

## 修过的真实 bug

1. **辅修功法错误提供修炼加成** —— JS 用 `if(id===a.main || !['cult','brew','forge'].includes(k))` 跳过，最初漏了，修炼 1.15 而非 1.10
2. **`requires` 是 C++20 关键字** —— 数据表字段改名 `requiresQuest`
3. **`Job` 缺 `chance`/`fee`** —— 编译期暴露
4. **`swordGain` 重复定义** —— 链接期 multiple definition
5. **存档目录不存在导致保存失败** —— `saveToFile` 加目录创建
6. **`createGame` 遇非法配点直接崩** —— 加回退重试
7. **`cls` 在管道输入下搅乱输出** —— 改成仅 `isatty` 时清屏
8. **`Step` 结构体字段数不匹配** —— 初始化错位，逐次出牌测试全挂
9. **初始进入游戏没有自动装备神通** —— `ensureArts` 只在整块洞府为空时才写
   `spells`，旧档清空神通后不会补；而且「携带/卸下神通」的入口
   （`artInstant` 的 `spell` 分支）**从来没有实现过**，`arts.h` 里声明了却没有定义，
   玩家只能一直用开局那四个，换了功法也带不上新神通。
   修法：`ensureArts` 补一个「已初始化但 spells 为空 -> 补回初始四神通」的分支；
   实现 `artInstant`（spell / node / main / aux / equip / salvage / inscribe）；
   新增 `/api/arts/spell`、`/api/arts/gear`、`/api/arts/spells` 与
   控制台「5.携带/卸下神通」，web 端补「神通总览」面板；
   建号时在纪事里明写「已自动装备神通：……」，让玩家看得见。
10. **主城接取战斗任务无效果** —— `monthComplete` 只处理
    `plan/expedition/bond/wild/breakthrough/travel/join/social`，
    营生类（`task/commission/hunt/mine/arena/quest/brew/forge/learn/meditate`）
    没有转交 `ecoComplete`，而 `ecoComplete` 在整个工程里**没有任何调用者**。
    于是猎妖委托到期不开打、山门差事不发工钱、委托不交工、炼丹炼器不出货，
    灵石只进不出。修法：`monthComplete` 开头加 `isEcoJob()` 判定并转交
    `ecoComplete`，且必须放在通用收尾之前（hunt 分支会 `battleStart`）。

## 已知边界

- 界面是**控制台菜单**，不是图形界面。原版的卡牌拖拽、地图、人物分页没复刻
- **存档与 JS 版不互通**（字段名接近但顶层结构不同）
- 部分 NPC 宏观演算保留核心逻辑，未逐条复刻每个分支
- 数值平衡以 JS 版为准，没调过任何系数
- 「拜师礼」需要同宗且对方境界更高、交情 ≥15；散修需先入宗
- 携带的灵击（`strike`）同时是内置基础卡，所以牌组里它会有 4 张
  （携带 2 + 内置 2）。JS 版同样如此，不是 bug

## 待办 / 可继续的方向

- 斗法的 24 回合上限、起手准备（combatPrep）的界面入口未做
- 炼丹/炼器的完整界面流程（目前只有规则与校验函数）
- 商会委托（commission）、藏经阁兑换、书坊买功法 的界面入口
- 想加图形界面的话，`core/` 已经是纯规则层，可以直接接

## 最近一次验证

```
清理重建 → 构建成功（ningzhou.exe 4,221,918 字节）
自测：通过 751 / 失败 0（新增「开机神通与营生任务结算」28 项回归）
实测（走真实 HTTP 接口）：
  建号 → arts.spells = 灵击/御剑诀/水灵盾/回春术（4 个，自动装备）
  /api/arts/spells → 40 张神通，carriedCount=4
  卸下回春术 → 携带金芒刺：ok；再装第 5 个 → 正确拒绝
  武陵城接猎妖委托 → 真的进入斗法（任务妖兽，kind=hunt）
  打完获胜 → 灵石 95→127，声望 +1
  山门差事到期 → 灵石 127→139（此前分文没有）
```

## 本机环境坑（2026-10-05 排查记录）

### VS Code 起不来（已修复）

症状：双击/命令行启动 `Code.exe` 后约 4 秒退出，退出码 `-2147483645`（`0x80000003`）。

原因有两层：

1. **安装被中断的自动更新搞坏了** —— 缺少 `resources\app\` 整个目录，
   根目录残留 `new_Code.exe`（1.136.1）和 `updating_version` 标记。
   官方源当时连不上，所以用本地暂存目录 `a44adf7f53`（完整的 768MB payload）
   补齐文件，再把 `new_Code.exe` 改名成 `Code.exe`，手工完成了这次更新。
2. **Chromium 沙箱在本机起不来** —— 补齐文件后仍 abort，
   逐个试参数发现**只加 `--no-sandbox` 就能启动**（`--disable-gpu` 单独无效）。

修复方式：在 `%USERPROFILE%\.vscode\argv.json` 里加

```json
"disable-chromium-sandbox": true,
"disable-hardware-acceleration": true
```

加完普通启动（不带参数）即可，窗口标题会显示 `ningzhou-cpp - Visual Studio Code`。

### 其它环境事实

- **这个 shell 里 `$env:APPDATA` 是空的**。启动 GUI 程序前需要补上：
  `$env:APPDATA = [Environment]::GetFolderPath('ApplicationData')`
- **`NODE_OPTIONS` 被设成了 WorkBuddy 的 shim**，Electron 会因此报错，
  启动 VS Code 前最好 `Remove-Item Env:NODE_OPTIONS`
- `Start-Process` 偶发 `NO_PROXY / no_proxy` 键重复报错，改用直接调用 exe 即可
- 已有的 C/C++ 扩展：`ms-vscode.cpptools`、`cmake-tools`、`cpptools-extension-pack`

### exe 缺 DLL 闪退（已修复）

症状：直接双击 `build\ningzhou.exe`，**什么都不显示就退出**，退出码 `-1073741515`（`0xC0000135`）。

原因：GCC 默认动态链接 `libstdc++-6.dll` / `libgcc_s_seh-1.dll`，
这两个在 `C:\msys64\ucrt64\bin`，不在系统 PATH 里。
**这个失败是静默的——没有弹窗、没有日志**，很容易误判成程序崩溃。

修复：`CMakeLists.txt` 加了静态链接

```cmake
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()
```

exe 体积 1.3MB → 3.9MB，换来可以拷到任何地方双击运行。

验证方法（改完 CMakeLists 后务必再验一次）：

```powershell
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'msys64' }) -join ';'
.\build\ningzhou.exe --help        # 必须正常输出
& 'C:\msys64\ucrt64\bin\objdump.exe' -p build\ningzhou.exe | Select-String 'DLL Name'
#   除 api-ms-win-* / KERNEL32 外不应有其它 DLL
```

注意：**编译期**仍然需要 `ucrt64\bin` 在 PATH 里（`cc1plus.exe` 要它），
静态链接只解决**运行期**。