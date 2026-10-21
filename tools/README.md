# tools 说明

这个目录包含数据导出、原版数值对照和本地回归检查。新增的本地斗法规则以 C++ 实现及回归测试为准；原版 JS 对照脚本用于核对未改动规则。

历史数据导出和 JS 数值对照脚本依赖仓库外的 `michangsheng-demo/`。它们按 `../../michangsheng-demo/*.mjs` 从 `tools/` 导入，需将对应 JS 项目放在本仓库的同级目录；公开仓库不包含该项目。已生成的 `data/reference_tables.h` 随仓库提供，正常构建、运行 C++ 游戏及其规则自测无需 JS 项目。`node tools/check-web.mjs` 等本地前端回归检查也不依赖该目录。

## 1. 生成数据表（改了 JS 版之后要跑）

原始数据表从 JS 导出到生成头文件；本地功法、神通和悟道的新增设计在 `src/core/arts.cpp`、`battle.cpp` 中覆盖，不手改生成文件：

```
michangsheng-demo/*.mjs
      │
      │  node dump-data.mjs      -> data-dump.json      （药材/丹方/功法/神通/天赋/剑道…）
      │  node dump-society.mjs   -> society-dump.json   （8 段支线/3 类悬赏）
      ▼
   python extract-data.py
      ▼
ningzhou-cpp/data/reference_tables.h     ← 自动生成，不要手改
```

`extract-data.py`在原始两份dump之上应用受版本控制的`data/reference_overrides.json`，再生成头文件。本地覆盖同时保存已经存在的抄本、丹术与悬赏风险文案，以及正式丹药改版；不修改仓库外的JS项目。覆盖按稳定表名和ID定位，未知表、字段、ID、重复目标和类型不一致会明确失败。两份dump仍为忽略的原始中间文件。

完整命令：

```powershell
cd ningzhou-cpp
node tools\dump-data.mjs    > tools\data-dump.json
node tools\dump-society.mjs > tools\society-dump.json
python tools\extract-data.py
python tools\extract-data.py --check
python tools\test_reference_data.py
```

`--check`逐字节验证当前生成表，`--output`可指定独立输出路径，`--overrides`可指定覆盖输入。生成结果固定为UTF-8无BOM、CRLF，跨平台重生不会重排整表。本轮先只编码原有10行本地差异，确认与原78908字节逐字节一致，再加入丹药调整。

**什么时候要重跑**：JS原始数据或本地覆盖输入改变时重跑。
改 C++ 代码不用重跑。

## 2. 对照脚本（怀疑数值算错时用）

这些脚本直接 `import` JS 版模块并打印结果，用来拿"标准答案"：

| 脚本 | 对照什么 | 对应 C++ |
| --- | --- | --- |
| `cards-check.mjs` | 卡牌斗法逐次出牌 | battle.cpp |
| `deck-check.mjs` | 牌组构成与起手抽牌 | battle.cpp |
| `artmods-check.mjs` | 洞府加成（artMods） | arts.cpp |
| `artgain-check.mjs` | 大道经验与悟道点 | arts.cpp |
| `craft-check.mjs` | 炼丹成丹率、炼器利用率 | arts.cpp |
| `build-check.mjs` | 人物生成（buildCharacter） | rules.cpp |
| `birth-check.mjs` | 五项开局天赋发的物品 | adventure.cpp |
| `society-check.mjs` | 俸禄账簿 | society.cpp |
| `notoriety-check.mjs` | 恶名阈值（39/40/41） | society.cpp |
| `rng-check.mjs` | 随机数序列 | base.h |
| `name-check.mjs` / `name-arrays.mjs` | NPC 姓名用字表 | society.cpp |
| `battle-check.mjs` | 旧版回合制斗法（Demo07 已废，留作参考） | — |

**用法**：`node tools\cards-check.mjs`，把输出和 C++ 自测里的期望值对一下。

**重要**：发现 C++ 和 JS 不一致时，先跑这些脚本确认 JS 的真实行为，
再决定是改 C++ 代码还是改测试里的期望值。开发过程中有 3 次是
**测试写错了**而不是代码错了（灵击张数、守势护盾、恶名阈值）。

## 3. 辅助

真实浏览器斗法回归使用隔离服务，会重置该服务中的测试角色；`battle-fixture.mjs` 明确禁止正式端口 8106/8107。

```powershell
# 先另开隔离服务：build\ningzhou_web.exe --port 18106 --no-open
node tools\battle-fixture.mjs --port 18106
node tools\uitest.mjs --battle-ui --port 18106
```

存档由真实建号、悬赏斗法和导出接口生成，再通过正式读档校验；测试点击真实手牌，检查增益、移除目标、取消与确认、换牌、境界和固定牌显示。截图在 `build/shots/`。

地图专项：`node tools/uitest.mjs --map-ui --port 18106`。该模式在隔离服务中创建测试角色，通过真实接口和鼠标点击核对搜索、路线、交通方式、海路限制、逐日行进和抵达；逐一点选全部13处地点，核对独立插画与地点对应、浏览器成功加载、大图打开/关闭、查看时快捷键不推进以及窄屏布局。`node tools/check-web.mjs` 另核对图片覆盖真实地点表、PNG尺寸和全部图片内容各异。

秘境与养成专项：`node tools/uitest.mjs --dungeon-ui --port 18106`。会在隔离服务重置测试角色，实际购买指定材料情报、定位并赶往入口，走完引擎已生成的相连路线；检查选卡、遗物、行商、营地删牌、带伤续战、首领十份材料及离开结算。另验证洞府声望与灵气档位、非致命入门考核失败重试与胜利入宗、藏经阁请领神通、死亡选择回档与接受结算、自然寿尽。窄屏检查使用 390px，截图在 `build/shots/dungeon-ui/`；脚本拒绝正式端口 8106/8107。`check-web.mjs` 的纯展示与动作检查可独立运行。

| 文件 | 用途 |
| --- | --- |
| `show-society.py` | 把 society-dump.json 打印成可读的支线清单（含各支线的触发条件） |
| `playtest.ps1` | 用脚本化输入驱动控制台界面，做自动化试玩 |
| `data-dump.json` / `society-dump.json` | 上面两个 dump 脚本的产物，中间文件 |

## 注意事项

- `data-dump.json` 有 180KB，是中间产物，已在 `.gitignore` 里排除。
- 这些脚本**只读** JS 版，不会修改 `michangsheng-demo/` 里的任何东西。
- 依赖 Node（跑 .mjs）和 Python 3（跑 .py），构建本身不需要它们。
