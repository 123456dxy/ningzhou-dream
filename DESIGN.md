# 宁州一梦 · C++ 版 —— 设计理念与架构

> 这份文档讲**为什么这么写**，不是讲怎么用。用法看 [README.md](README.md)，
> 接续进度看 [CONTEXT.md](CONTEXT.md)。

---

## 一、这个移植到底在解决什么问题

原版是浏览器里的 JS 游戏（`michangsheng-demo/`，Demo 07），约 27 万字符、12 个模块。
它能玩，但对想改规则的人来说有三个障碍：

1. **代码是压缩过的**——`sim.mjs` 456 行塞了 4.5 万字符，很多逻辑一行到底，读不动
2. **没有类型**——`s.player.arts.progress.brew` 这种嵌套，打错字运行时才发现
3. **改不了数值试效果**——得改文件、刷浏览器，没有断点

所以这个 C++ 版的目标**不是重做一个游戏**，而是：

> 把同一套规则翻译成能读、能断点、能打印中间值的代码，并且**保证数值和原版一模一样**。

最后这条是关键约束。如果只是"重新实现一个类似的修仙游戏"，那没有意义——
你没法确认改完之后行为对不对。所以整个项目围绕**逐值对照 JS 版**来组织。

---

## 二、四条设计原则

### 原则 1：数值必须与 JS 版逐位一致

这是最硬的一条，决定了后面很多设计。

JS 的 `Math.round`、`Math.floor`、`>>> 0`（转无符号 32 位）在 C++ 里都有细微差别：

| JS | C++ | 陷阱 |
| --- | --- | --- |
| `Math.round(2.5)` → 3 | `std::round(2.5)` → 3 | 但 `round(-2.5)` JS 给 -2，C 给 -3 |
| `Math.round(-2.5)` → **-2** | `jsRound()` | 用 `floor(n+0.5)` 才对 |
| `x >>> 0` | `uint32_t` | 有符号溢出的行为不同 |
| `Math.floor(-0.5)` → -1 | `int(-0.5)` → 0 | 截断 ≠ 向下取整 |

所以 `base.h` 里专门做了这几个函数：

```cpp
inline int jsRound(double n) { return static_cast<int>(std::floor(n + 0.5)); }
inline int jsFloor(double n) { return static_cast<int>(std::floor(n)); }
inline double jsSqrt(double n) { return n <= 0.0 ? 0.0 : std::sqrt(n); }
```

**随机数是最极端的例子**。JS 版用的是一个 LCG：

```js
rng = (Math.imul(rng, 1664525) + 1013904223) >>> 0
```

`Math.imul` 是 32 位整数乘法（结果截断到 32 位）。C++ 里直接写：

```cpp
g.rng = static_cast<uint32_t>(g.rng * 1664525u + 1013904223u);
```

这样同一个种子产生**完全相同的序列**。自测里锁死了这一点：

```
[通过] 种子 20261004 的前 5 个随机数与 JS 版一致
```

为什么值得这么做？因为只要随机数序列一致，**同一份存档在两版里推进会走出同一条时间线**。
否则任何数值对不上，你都分不清是"公式写错了"还是"随机数不同导致的"。

### 原则 2：数据表不手抄，从 JS 那边导出

157 种药材、126 张丹方、40 部功法、39 项天赋、8 段支线……这些是原作攻略整理出来的资料。
手工重打一遍必然出错，而且 JS 版一更新就全废。

所以走管道：

```
michangsheng-demo/*.mjs
      │  node dump-data.mjs      （直接 import JS 模块，不解析文本）
      │  node dump-society.mjs
      ▼
   data-dump.json / society-dump.json
      │  python extract-data.py
      ▼
ningzhou-cpp/data/reference_tables.h   ← 生成物，勿手改
```

**为什么用 Node 而不是正则解析**：早期我试过用 Python 正则抓 JS 数组字面量，
结果被 `{id:'x',price:.02}` 这种非标准 JSON 绊住。直接 `import` 让 JS 引擎自己求值，
拿到的一定是运行时真值。

**代价**：`reference_tables.h` 是生成物，改它没意义（下次生成会覆盖）。
要改数据得改 JS 版的源表。这一点在文件头和 README 里都标了。

### 原则 3：字段名与 JS 对齐，方便两边对着读

`model.h` 里的结构体字段**故意**保持和 JS 一样：

```cpp
// C++
g.player.arts.progress.brew
g.bonds.payroll.balance
g.adventure.since
```
```js
// JS
s.player.arts.progress.brew
s.bonds.payroll.balance
s.adventure.since
```

这不是偷懒，是为了调试时能直接对着两份代码看。如果 C++ 这边改成
`player.cultivation.brewCount`，你就得在脑子里做一层映射，很累。

同理，函数名也尽量对应：`artMods` → `artModsOf`，`battleTurn` → `battleTurn`。

### 原则 4：规则层不依赖界面

```
         ┌─────────────┐
         │   ui.cpp    │  控制台菜单、输入输出
         └──────┬──────┘
                │ 只调用公开函数
         ┌──────▼──────┐
         │   nzcore    │  纯规则静态库
         │  (src/core) │  不知道有界面这回事
         └─────────────┘
```

`src/core/` 里没有任何 `#include <iostream>` 或 `printf`。
好处是：

- **自测可以脱离界面跑**——507 项检查全是纯函数调用，秒级完成
- **想换图形界面、Web 界面、命令行参数版，core 一行不用改**
- **每个模块能单独测**，不依赖"先建号再点五次菜单"

这也是为什么 `tests/` 里能直接构造 `Game g; g.player.stage = 3;` 然后调规则函数。

---

## 三、模块划分

按 JS 版原本的切分走，一个 `.mjs` 对应一对 `.h/.cpp`：

```
src/core/
├── base.h            基础类型 + JS 数值语义 + 随机数
├── model.h           全部数据结构（唯一的"真相"定义）
├── rules.cpp         规则表查找、人物生成
├── calendar.cpp      时间：360 天月历、跨月工作
├── sim.cpp           中枢：世界推进、NPC、行动、结算
├── arts.cpp          洞府：十二大道、功法神通、炼丹炼器
├── battle.cpp        卡牌斗法
├── sword.cpp         剑道七重境界
├── economy.cpp       营生：任务、委托、坊市
├── society.cpp       宗门知交：俸禄、师徒、支线、悬赏
├── adventure.cpp     游历：六类遇事、后山
├── json.cpp          极简 JSON（自写，无第三方依赖）
└── save.cpp          存档序列化
```

### 为什么 sim 是中枢

`sim.cpp` 最大（989 行），因为 JS 版的 `sim.mjs` 就是中枢。它负责：

- 纪事记录（`logEvent`）
- 数值公式（`cultivationRate` / `breakthroughChance`）
- 世界年度推进（`worldYear`）、NPC 年度行动（`npcTurn`）
- 新游戏（`createGame`）、结算（`finish`）

其它模块通过 `sim.h` 拿这些能力。比如 `calendar.cpp` 在跨年时要调 `worldYear`。

### 为什么 JSON 是自己写的

存档只需要对象/数组/字符串/数字/布尔/null 这几种结构。
引第三方库（nlohmann/json 之类）要多一个依赖，而且你读代码时还得先学它的 API。

自己写 261 行，好处是：

- **没有任何外部依赖**，`git clone` 下来只要有编译器就能构建
- **出错时能直接读源码**，不用猜库的行为
- 存档格式自己控制——写成缩进格式化的，**中文不转义**，可以直接用编辑器打开看

代价：不支持完整 JSON 标准（比如不做深度嵌套的性能优化）。
但存档场景够用，而且自测里覆盖了中文、浮点、负数、转义这些边界。

---

## 四、几个关键设计决策

### 决策 1：用 `optional` 表达"可空"，而不是空指针

JS 里到处都是 `s.pending`、`a.battle` 这种"可能是 null"的字段。

C++ 里如果照搬指针，就要处理生命周期和悬垂问题。用 `std::optional`：

```cpp
struct Game {
    optional<Job> job;         // 当前在途工作，可能没有
    optional<Pending> pending; // 待处理抉择，可能没有
    optional<Ending> ending;   // 结算信息，人生结束后才有
};
```

用法接近 JS：

```cpp
if (g.pending.has_value()) { ... }
g.job.reset();                    // 相当于 JS 的 s.clock.job = null
```

### 决策 2：NPC 用值存储，靠 id 查找

JS 里 NPC 是对象数组，到处传引用，互相之间用 `n.relations[otherId]` 记关系。

C++ 里如果也用指针，`npcs` 数组一旦扩容（`push_back`）所有指针就悬垂了。
所以：

- `npcs` 是 `vector<Npc>`，**按值存**
- 关系用 **id 字符串**记录（`relations[id] = 60`）
- 需要操作某个 NPC 时，用 `findNpc(g, id)` 现查

```cpp
for (auto& n : g.npcs)
    if (n.id == id && n.alive) { n.xp += 40; break; }
```

代价：每次查找是 O(n)，720 个 NPC 时略慢。
但换来的是**不会有悬垂指针**，调试时不用担心"这个引用还指向有效对象吗"。

### 决策 3：战斗状态挂在 `Arts` 上，而不是独立管理

JS 版把战斗状态放在 `p.arts.battle`。看着别扭，但有个好处：
**存档时战斗状态自动跟着走**，刷新页面能续战。

C++ 里照搬：

```cpp
struct Arts {
    // ...
    optional<Battle> battle;   // 在途斗法
};

struct Battle {
    // 牌组
    vector<Card> deck;
    vector<string> hand, draw, discard;
    // 状态
    map<string,int> cooldowns;
    int poison, poisonTurns, burnTurns;
    // 游历上下文（对应 JS 把 adventure 挂在 battle 上）
    string advKind; int advFee; string advBook;
};
```

这样 `battleStart` 只在需要时构造，`battleFinish` 里 `reset()`，
和 JS 的 `a.battle = {...}` / `a.battle = null` 语义一致。

### 决策 4：预览与真实结算共用同一个函数

Demo 07 的卡牌斗法有个要求：**卡牌上显示的伤害必须和真打出去的一致**。

如果预览用一个简化公式、结算用另一个，两边早晚会对不上。所以：

```cpp
// 真实出牌
static string battleEffect(const Player& p, Battle& b, const SpellDef& spell);

// 预览：在副本上跑同一个函数
Preview battlePreview(const Player& p, const Battle& b, const string& id) {
    Battle copy = b;                          // 复制一份
    const string text = battleEffect(p, copy, *spell);   // 在副本上真算一遍
    out.damage = b.enemyHp - copy.enemyHp;    // 用差值当预览值
    // ...
}
```

**预览就是"试着打一下然后撤销"**，不是另算一遍。这样数学上不可能不一致。

自测里专门验了这一点：

```
-- 出牌预览（与真实结算共用同一函数）--
  [通过] 预览 water (费1 伤0 盾26 疗0 灵-1 气0)
  [通过] 预览 sword (费2 伤23 盾0 疗0 灵-2 气1)
```

### 决策 5：日结与月结分开

时间系统有两层：

- **每天**：`calendarRun` 里逐日推进，处理丹毒化解、`refreshDaily`
- **每 360 天**：`worldYear` 处理资源再生、坊市行情、NPC 年度行动

为什么分开？因为 NPC 演算很贵（720 个 NPC 每人一次决策）。
玩家按天行动，如果每天都跑一遍 NPC 逻辑，性能受不了。

JS 版原话是"NPC 宏观修行每 360 天结算，以控制模拟成本"。C++ 照搬。

### 决策 6：静态链接，让 exe 能单独跑

**这是踩过坑才加的。**

GCC 默认动态链接 `libstdc++-6.dll` 和 `libgcc_s_seh-1.dll`。
这两个 DLL 在 `C:\msys64\ucrt64\bin` 下，不在系统 PATH 里。

后果：**直接双击 exe，什么提示都没有，进程就消失了**，
退出码 `0xC0000135`（STATUS_DLL_NOT_FOUND）。

更糟的是它**不报错**——没有弹窗、没有日志，看起来就像程序闪退。

解决：

```cmake
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_link_options(-static-libgcc -static-libstdc++ -static)
endif()
```

代价：exe 从 1.3MB 涨到 3.9MB。
收益：**可以拷到任何地方双击运行**，不依赖工具链。

验证方式（自测里也这么验）：

```powershell
# 把 PATH 里的 msys64 去掉，再运行
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -notmatch 'msys64' }) -join ';'
.\build\ningzhou.exe --help     # 必须正常输出
```

---

## 五、怎么保证"没抄错"

这是整个项目最重要的部分。**507 项自测，几乎每一项都是"和 JS 版对答案"**。

### 方法论

```
1. 写一个 .mjs 脚本，import JS 模块，打印参考值
2. 把参考值写进 C++ 自测的期望值里
3. 跑自测，不一致就查
4. 不一致时：先跑 JS 确认真实行为，再决定改代码还是改测试
```

第 4 步是关键。开发过程中有 **3 次是"我记错了规则"而不是代码有 bug**：

| 案例 | 我的错误期望 | JS 的真实行为 |
| --- | --- | --- |
| 灵击张数 | 2 张 | **4 张**（携带 2 + 固定 2） |
| 守势护盾 | 26 | **17**（0.12×144，26 是水灵盾的值） |
| 恶名阈值 | 41 才停 | **40 就停**（`notoriety < 40` 才发） |

如果当时直接改代码去迎合错误期望，就会把对的逻辑改坏。
所以规矩是：**测试挂了先怀疑自己记错**。

### 对照脚本清单

`tools/*.mjs` 就是干这个的：

| 脚本 | 拿什么当标准答案 |
| --- | --- |
| `cards-check.mjs` | 卡牌斗法逐次出牌（8 步） |
| `artmods-check.mjs` | 洞府加成 |
| `craft-check.mjs` | 炼丹成丹率、炼器利用率 |
| `build-check.mjs` | 人物生成 |
| `society-check.mjs` | 俸禄账簿 |
| `birth-check.mjs` | 五项开局天赋发的物品 |
| `rng-check.mjs` | 随机数序列 |
| `name-check.mjs` | NPC 姓名用字 |

### 最有价值的一项：卡牌逐次出牌

```
-- 逐次出牌（对照 JS 版实际结果）--
  [通过] 第1步 strike (气血144 敌196 灵2 盾0 气0 回0 出1 手4)
  [通过] 第2步 sword  (气血144 敌173 灵0 盾0 气1 回0 出2 手3)
  ...
  [通过] 第8步 end    (气血100 敌149 灵3 盾0 气1 回3 出0 手5)
```

一场固定出牌序列，8 步里比了 8 个维度：气血 / 敌血 / 灵力 / 护盾 / 剑气 /
回合数 / 已出牌数 / 手牌数。

**并且连起手 5 张牌的 uid 顺序都一致**——这点很重要，
因为洗牌序列一旦偏了，之后每一抽都会不同，逐值对照就无从谈起。

### 逐值比对抓到过的真 bug

| bug | 表现 | 根因 |
| --- | --- | --- |
| 辅修功法错误加修炼效率 | 修炼 1.15 而非 1.10 | JS 用 `if(id===a.main \|\| !['cult','brew','forge'].includes(k))` 跳过，我漏了 |
| `swordGain` 重复定义 | 链接期报错 | `sim.cpp` 和新的 `sword.cpp` 都有 |
| 存档目录不存在 | 保存静默失败 | `saveToFile` 没建目录 |
| 测试结构体字段错位 | 8 项全挂 | `Step` 少写一个字段，初始化列表移位 |

---

## 六、这个设计的取舍

### 做得好的地方

- **可调试性**：`--debug` 能一次打印种子、加成、战斗公式的当前计算值
- **可验证性**：507 项对照，改坏了立刻知道
- **无依赖**：不需要第三方库，JSON 和测试框架都是自带的
- **分层干净**：core 不依赖界面，能单独测

### 明确的不足

| 不足 | 说明 |
| --- | --- |
| **界面是控制台** | 原版的卡牌拖拽、地图、人物分页没复刻 |
| **存档不互通** | 字段名接近，但顶层结构不同，两边各自存档 |
| **NPC 演算简化** | 保留了核心逻辑，未逐条复刻每个支线分支 |
| **自测框架很简陋** | 只有 `check` / `checkEq` / `checkNear`，没有 fixture 和 mock |
| **查找是 O(n)** | NPC 用 id 线性查，720 人时略慢（但换来了无悬垂指针） |
| **exe 体积偏大** | 静态链接后 3.9MB |

### 为什么不做图形界面

`core/` 已经是纯规则层，接任何界面都行。但控制台版有个当下更重要的优势：
**出问题时信息全在屏幕上**。图形界面崩了往往只有一个白屏，
控制台版能看到完整的中间状态。

对一个以"能调试"为首要目标的移植来说，这是更合适的选择。

---

## 七、如果继续往下做

按价值排序：

1. **补齐界面入口**——炼丹/炼器的完整交互流程（规则和校验函数都在，缺 UI）
2. **战斗增强**——24 回合上限提示、起手准备（`combatPrep`）的入口
3. **存档与 JS 版互通**——做一层字段映射，方便两边对照同一个存档
4. **图形界面**——`core/` 可直接接，比如 ImGui 或 Web 前端
5. **性能**——NPC 查找换成哈希表（当 NPC 数量大幅增加时才需要）

---

## 八、一句话总结

> 这不是重写一个游戏，是把一套已有的规则**翻译成能读懂、能断点、能验证的形式**，
> 并以「与原版逐值一致」作为不可让步的约束。
>
> 所有设计决策——复刻 LCG、导出数据表、对齐字段名、core 不依赖 UI——
> 都是为了这一件事服务。
