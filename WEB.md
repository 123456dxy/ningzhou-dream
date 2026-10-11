# Web 版说明（推荐玩法）

控制台版适合调代码，但**玩起来太苦**。Web 版才是给人玩的：
界面就是 JS 版那一套 HTML/CSS，规则全部由 C++ 计算。

---

## 怎么跑

```powershell
cd ningzhou-cpp
.\build.ps1                 # 构建（会同时生成 ningzhou_web.exe）
.\build\启动宁州一梦-网页版.bat   # 双击这个，浏览器自动打开
```

或者手动：

```powershell
.\build\ningzhou_web.exe                # 默认 8106 端口，自动开浏览器
.\build\ningzhou_web.exe --port 9000    # 换端口
.\build\ningzhou_web.exe --no-open      # 不自动开浏览器
.\build\ningzhou_web.exe --web D:\前端   # 指定前端目录
```

打开后地址是 `http://127.0.0.1:8106/`。**服务只监听本机**，不对外暴露。

---

## 架构：为什么 C++ 能直接用原版前端

关键发现：**JS 版根本没有后端**。

| 检查项 | 结果 |
| --- | --- |
| `fetch()` / `XMLHttpRequest` | 一处都没有 |
| 存档 | `localStorage`（浏览器本地） |
| 渲染 | `innerHTML` 拼模板字符串 |
| `styles.css` | 42 KB，独立文件 |

它是纯前端——规则跑在浏览器里。所以 C++ 要取代的不是"前端"，
而是**那个跑在浏览器里的规则引擎**。界面可以原封不动复用。

```
┌─────────────────────────────────────────┐
│  浏览器                                  │
│  index.html + styles.css + app.mjs      │  ← 复用原版界面
│  （纯展示：不算任何数值）                  │
└──────────────┬──────────────────────────┘
               │ HTTP  POST /api/<动作>
               │       GET  /styles.css ...
┌──────────────▼──────────────────────────┐
│  ningzhou_web.exe（C++）                 │
│  ├── http.cpp   极简 HTTP 服务（无依赖）   │
│  ├── api.cpp    接口层：只做翻译，不写规则  │
│  └── nzcore     全部游戏规则              │  ← 和自测同一套代码
└─────────────────────────────────────────┘
```

### 三个设计要点

**1. 前端不算任何数值。**

每次操作都是「POST 给 C++ → 拿回完整 state → 重渲染」。
连卡牌上的伤害数字都来自 C++（`battlePreview`，和真实出牌共用同一个函数），
所以**界面上显示什么，打出去就是什么**。

**2. `api.cpp` 只做翻译，不写规则。**

它把 `{"id":"bandit"}` 转成 `findWildTask("bandit")`，把结果转回 JSON。
所有数值都来自 `core`，保证和 507 项自测覆盖的是同一套代码。
改规则只需要改 core，界面自动跟着变。

**3. HTTP 服务是自己写的，没引第三方库。**

整个项目到现在零外部依赖。这里需要的只是一个本地单线程的静态文件服务 +
JSON 接口，用 Winsock 写 288 行就够了，而且你能直接读懂。

---

## 接口清单

所有接口都是 `POST`，body 是 JSON，返回 `{ok, state}` 或 `{ok:false, error}`。

| 接口 | 参数 | 作用 |
| --- | --- | --- |
| `/api/state` | — | 查当前完整状态 |
| `/api/new` | `name`,`gender`,`seed`,`talent` | 开新的一世 |
| `/api/advance` | `days` | 推进若干天 |
| `/api/plan` | `plan` | 切换月度计划 |
| `/api/breakthrough` | — | 尝试突破 |
| `/api/choose` | `id` | 处理待决抉择 |
| `/api/arts/node` | `node` | 点亮道法节点（如 `"8-1"`） |
| `/api/arts/focus` | `dao` | 切换主修大道 |
| `/api/arts/learn` | `id` | 修习功法 |
| `/api/stipend` | — | 领取俸禄 |
| `/api/story` | `id`,`choice` | 完成原创支线 |
| `/api/wild` | `id` | 接野外悬赏（会进斗法） |
| `/api/expedition` | — | 探访后山 |
| `/api/travel` | `to` | 前往他处 |
| `/api/task` | `id` | 接城中任务 |
| `/api/battle/turn` | `id` | 出牌（`uid`）／`"end"`／`"escape"` |
| `/api/save` | `slot` | 存档（1-3） |
| `/api/load` | `slot` | 读档 |
| `/api/slots` | — | 存档位列表 |

### state 里有什么

`player` / `clock` / `arts`（含 `mods` 加成汇总）/ `battle`（含每张手牌的预览）/
`logs` / `locations` / `stories` / `wildTasks` / `plans` / `breakthrough` / `bonds`。

前端拿到就直接渲染，不需要自己算。

---

## 存档

**存在 C++ 这边**，不是浏览器的 `localStorage`。
位置：`build/saves/slotN.json`，缩进格式化的可读 JSON，中文不转义。

所以：

- 可以直接用编辑器打开看、改、对比
- 和 JS 版的存档**不互通**（字段名接近，但顶层结构不同）
- 换浏览器不影响存档

---

## 调试界面

改了 `web/` 下的文件（HTML/CSS/JS）**不用重新构建**：

```powershell
Copy-Item web\* build\web\ -Recurse -Force   # 拷过去
# 然后浏览器 Ctrl+F5
```

因为 HTTP 层设了 `Cache-Control: no-store`，刷新就是最新的。

改了 C++ 规则才需要重新构建。注意**先关掉正在运行的服务**，
否则 `ningzhou_web.exe` 被占用，链接会失败（`Permission denied`）。

调试 C++ 规则：

```powershell
.\build\ningzhou.exe --debug     # 控制台版可以直接看内部状态
.\build.ps1 -Test                # 507 项自测
```

---

## 已知不足

| 不足 | 说明 |
| --- | --- |
| 单线程 | 一次处理一个请求。本地单人够用，不是给人多开用的 |
| 没有并发保护 | 同上；也不打算做，本地游戏不需要 |
| 界面只覆盖了主要流程 | 炼丹炼器、坊市买卖的完整交互还没接 |
| 部分页签是简版 | 舆图还没画成真正的地图，是卡片列表 |
| 无认证 | 只监听 127.0.0.1，不要改成 0.0.0.0 暴露出去 |

---

## 和 JS 版界面的差别

- 复用同一份 `styles.css`（42 KB，一字未改），所以配色、卡片、侧边栏一致
- `index.html` 的结构照搬，去掉了原版的几个页签（同道众生、宗门与天下），
  合并成了「宗门知交」
- 侧边栏版本号显示 `C++ · 07`
- 原版英雄区的山水 SVG、竖排诗句、印章都保留了
