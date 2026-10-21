# 筑基、丹药续命与绝对气血 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将已试玩确认的两种筑基玩法、丹药续命改版和玩家绝对气血共同接入当前正式工程。

**Architecture:** 数值规则由 C++ 后端共享计算，网页只提交选择、显示报价与结果。玩家气血采用绝对值；NPC 和副本保留内部百分比，在边界转换。筑基有独立持久状态和局部随机流，仅适用于玩家 stage 2→3。

**Tech Stack:** C++20、CMake、Visual Studio/MSBuild、现有原生 JS 前端、JSON 存档。

**Spec:** 用户本对话已确认原型；`prototypes/zhuji-lab/engine.mjs` 的 elements 模式；`prototypes/meridian-network-lab`；`D:/Users/24974/Downloads/丹药与续命改版需求.md`；`D:/Users/24974/Downloads/气血改绝对值实施方案.md`。

## 当前实施状态（2026-10-17）

三项规则已直接接入当前主项目的核心、经典/v2网页与存档，新增源码已同步Visual Studio工程。Release x64主解决方案编译、定向规则/HTTP检查及真实浏览器验收已完成。完整回归首次完成后发现63条陈旧测试断言，已核对根因并更新夹具；最终完整重跑21030条通过、0失败，退出0。下方勾选表示对应工作已经执行，实际结果见末尾记录。

玩家使用顺序为：人物面板查看绝对当前/最大气血；行囊服丹前查看后端实际收益、耐药与丹毒；炼气后期圆满后从突破入口自选筑基路线、准备丹药/灵核、完成90天安排并操作至定型，再从神通界面的单槽选择或更换本命。金丹和元婴未加入新玩法，原突破按钮、概率及结算保留。详见[筑基使用说明](../../ZHUJI_INTEGRATION.md)、[丹药规则](../../PILL_EFFECTS.md)与[当前源码说明](../../../README.md)。

## Global Constraints

- 当前 `visual-studio/ningzhou.sln` 对应源码是实施位置；不换旧工作树、不覆盖现存修改。
- 金丹和元婴暂不做新玩法；旧在途突破任务兼容原结算。
- 新字段 PascalCase；命名空间级新函数使用 mf 前缀。现有 health、hpBonus、lifeBonus 名称保留兼容。
- 查询不推进活动 RNG、日历、NPC 服丹或存档；拒绝的输入不扣费。
- 新状态同步 codec、原始结构校验、领域校验、旧档默认值、往返测试。
- 不手改 reference_tables.h，先保留现有生成差异，再从正式输入生成。
- 玩家存档、现有服务和无关修改保留；测试使用独立目录、角色、端口。
- 本轮不自动提交；新增源码同步 CMake 和 VS 项目。

## Review Focus

1. health≤100 不代表旧档：用显式 healthRulesVersion 迁移，旧百分比 50 和新绝对 50 必须可区分。
2. NPC/副本 health 与玩家绝对值互写，不能把百分比直接覆盖玩家，或把新丹药基础值遗漏。
3. 筑基连点、过期响应、刷新和重复归元不能多扣道具、多发根基或重掷随机结果。
4. 已删除丹药/药材的旧库存与服用历史必须有明确迁移，不能导致全档拒载。
5. 基础气血、天赋寿元与服丹寿元必须分开，装备降上限需在动作边界夹紧当前气血。

## 影响记录与本轮裁决

改动前已保存 `tmp/three-change-baseline-d54b0458/tracked-before.patch` 和 `status-before.txt`。权威源是上述两份需求和用户确认原型；附件中的旧路径、无生成输入和无法本机构建等描述不作为当前事实。

- 附件详细实施方案及 v2 附录 B 优先：玩家绝对值，NPC/副本内部百分比。统一30点门槛；过月3%；疗养整月含过月恢复合计10%，按 round(10%)-round(3%) 计算疗养额外部分。
- 外部游历/事件的原 n 点百分比伤害转为上限的 n%，保留相对代价。致死失败为0，胜利/撤退至少1。
- 共享 BATTLE_POWER 使用曲线A，不拆攻击曲线。丹药和根基的基础气血经过2.4及既有生命倍率，不进入伤害。
- 保留当前未提交的0/1/2/4丹毒规则，越阶不另加毒。年化毒统一5。
- Person::PillLifeBonus 独立记录丹药寿元0..500，lifeBonus仍为总寿元增量；旧档从天赋贡献中分离。
- Arts::BaseHealthBonus、NpcPillState::BaseHealthBonus 记录体魄丹基础气血；Arts::FoundationHealthBonus 单独记录筑基根基，旧 hpBonus 仍为旧倍率。
- 新筑基准备按既有突破门槛、费用和90天任务结算；选择路线及0..6份实际凝元丹（筑基界面按筑基丹表述）、可选灵核。90天后进入交互，无额外旧成功率抽签。失败/中止沿用旧突破修为损失且不退已提交准备。归元一次性完成突破及不可补修根基。
- 本命按本轮默认方案实现：五行对应固有、周天对应保留，成功后从神通界面单槽选择或更换当前境界可用的已领悟普通神通（含固定攻防）。只作用于携带该神通时的第一张副本：固有占正常首手名额，保留占正常手牌容量；尚无可选神通时留待以后绑定。准备期与活跃期隔离其他写动作，准备途中既有事件/斗法可解决。

## Task 1: 共享模型、存档与迁移（root）

**Files:** model.h；persistence/person_codec.cpp、arts_codec.cpp、game_codec.cpp、shape_validation.cpp、validation.cpp（以实际结构为准）；tests 的存档定向覆盖。

**Interfaces:** 新增上述3个基础气血字段和 Person::PillLifeBonus；Player::Zhuji 使用 progression/zhuji_state.h 的 ZhujiRun。HP 模块提供 mfPlayerHealthMaximum(const Player&) / mfNpcHealthMaximum(const Npc&)；筑基模块提供局部 codec。

- [x] 加入字段及严格类型/范围校验；禁止用查询补字段。
- [x] 新存档写 healthRulesVersion=2；旧格式先校验原始数据、再迁移比例气血、最后校验领域状态；递归检查 checkpoint snapshot。
- [x] 迁移退役5519→5420库存，保留历史使用记录兼容；清退5318/5319/6323/6324和对应无效驱妖状态。旧5519在途炼丹映射5420，旧5318/5319合同取消并按冻结合同补偿，非法合同先拒绝。
- [x] 加入旧百分比/新绝对50、满血、0、冻结战斗、旧续命天赋拆分、非法字段和往返用例。

## Task 2: 绝对气血、公式与适配（HP agent）

**Files:** combat/battle_scaling.h/.cpp、battle.cpp、battle_lab.cpp、simulation/sim.cpp/calendar.cpp、cultivate.cpp、exploration 互写、各门槛展示、tests。

**Interfaces:** mfPlayerHealthMaximum(const Player&) / mfNpcHealthMaximum(const Npc&) 和共享转换/恢复函数。

- [x] 增加定向用例：新曲线基础max序列、体魄丹350基础、根基20基础、NPC1.45、疗养148上限共恢复15。
- [x] 替换共享曲线、生命构建、伤害基础和气血30门槛；清理旧百分比写回及玩家战力health/400。
- [x] 对战结算、日历战中治疗、副本与NPC转换、新角色满血、装备变化夹紧复用权威上限。
- [x] 执行定向回归并报告未验证范围。

## Task 3: 丹药、寿元与数据（pill agent）

**Files:** progression/arts.cpp/.h、social/npc_consumables.cpp、真实NPC年行为、tools/extract-data.py、tracked overrides输入、生成表、相关文档/测试。

- [x] 保留现有生成文案差异，改输入走原生成链，提供逐字节生成检查入口。
- [x] 用测试覆盖五档回复、满回复sentinel、永久基础气血、寿元500独立于天赋、取消资质/悟性总封顶及stage12修为丹。
- [x] 共享服丹计划实现预览/执行/净丹毒/耐药/拒绝原因一致；护甲转每回合盾。
- [x] NPC适配百分比气血、寿元分带、真实获取化毒丹、40丹毒边界与不循环化毒。
- [x] 清除退役内容引用，旧档兼容由Task1接线。

## Task 4: 筑基规则、API与玩法页（foundation agent）

**Files:** 新 progression/zhuji_state.h、zhuji_rules.h/.cpp、zhuji_board.h/.cpp，interface/zhuji_api.h/.cpp，persistence/zhuji_codec.h/.cpp，web/zhuji.mjs/.css，api.cpp/sim.cpp 定点接线、tests。

- [x] 覆盖与已确认原型的关键规则：均匀五行随机、聚气跨轮、合一12、携带上限、独立随机流；36穴65边、冲穴耗气/次数/灵核/加成、归元门槛。
- [x] 实现局部状态及严格动作输入，真实准备、revision/requestId、拒绝原子性、保存重载与一次结算；失败可重试，人生结束终止未完局且不再扣失败修为。
- [x] 仅stage2→3走新支路；老job及金丹元婴走原流程；天赋来源真实，不开放任意seed或模拟天赋参数。
- [x] 网页显示两种玩法选择与紧凑桌面操作；手机地图横滚、详情纵滚，通用规则/手记默认折叠，不加常驻返程提示。补充跨账号/人生/同世回档重置及旧异步响应隔离。

## Task 5: 主界面、构建与可玩核验（root）

**Files:** web/app.mjs、通用气血与丹药显示模块、CMakeLists.txt、tests/test_main.cpp、tools/generate-vs-projects.py 生成结果、模块README/HTTP文档。

- [x] 气血条按current/max，丹药说明展示实际基础/实际上限增量；经典/v2接入筑基页及忙碌状态，成功结果与后续突破入口并存。
- [x] 核对天机大比等朋友模块的兼容边界：天机冻结快照仅补基础气血、根基与本命兼容字段；不改变联机协议或HTTP实现。
- [x] 同步CMake与VS，用MSBuild编译、跑本轮针对性测试，再按风险跑现有回归。
- [x] 独立目录/角色/端口启动，确认可用后先给网址；真实浏览器验证准备→筑基→结算、治疗/续命/气血、刷新/连点/失败恢复及窄屏。
- [x] 最后审阅diff、确认测试使用独立存档，交付实际检查及限制。

## 本轮实际验收（2026-10-17）

- `MSBuild visual-studio/ningzhou.sln /m /p:Configuration=Release /p:Platform=x64` 退出0，生成核心库、正式网页端、控制台、战斗实验室及测试程序。日志 `tmp/three-change-msbuild-final.log`。核心项目103个编译输入与当前 `src/core` 源文件逐项一致；CMake递归源文件配置覆盖新增模块。
- 完整回归首次结束为20927通过、63失败，记录在 `tmp/three-change-full-final.log`。其中56条源于HEAD已存在的1080日市场周期与取消月售量限制，7条源于NPC化毒40点门槛及绝对气血夹具；未改生产经济或NPC服丹规则。修正后隔离市场五组646条及丹药2630条均通过。最终完整重跑日志为 `tmp/three-change-full-final2.log`，21030条通过、0失败，进程及 `tmp/three-change-full-final2.exit.txt` 均确认退出0。
- `tools/check-pill-revision-http.mjs` 实际HTTP验证五档回复、全回复、预览/执行一致、基础气血及寿元独立额度、查询只读；迁移专用检查验证旧5519映射及退役合同清退/补偿，均退出0。CLI独立角色验证炼气圆满不会绕过新玩法、活跃筑基不能被普通动作推进或修改。
- `python -B tools/test_reference_data.py` 五项通过；`tools/extract-data.py --check` 逐字节一致；八个受影响正式前端模块 `node --check` 通过；`node tools/check-zhuji.mjs` 通过；`git diff --check` 无空白错误。
- 真实浏览器使用独立角色、目录、端口：五行经过90天准备、随机五行操作、永久定型120分，绑定金芒刺固有；周天实际经过失败/灵核恢复次数、三关及上方至阳返程，101分定型并绑定水灵盾保留，刷新后保持。实际服疗伤丹130→170、续命200→210，预览、库存扣除与执行一致。
- 1280×800桌面玩法及操作按钮可见；390×844手机地图内部横滚、详情页面纵滚，无页面横向溢出，实测归元及绑定。两标签页冲突时旧确认被拒绝且可刷新恢复，双击只扣一次；一次选穴报价约337ms、已通穴提交约989ms（包含浏览器自动化及渲染，仅为单次样本）。
- 最终试玩直接运行当前 `build-vs-share/Release/ningzhou_web.exe`、读取当前 `web`，端口53364返回HTTP200；仅存档/反馈目录独立。未将原型目录当作正式执行入口，未提交Git或发布打包。

未验证Linux/CMake实际编译、朋友联机服务端到端、长期NPC经济/难度统计及内存分配异常注入。金丹、元婴未新增玩法；控制台仅对筑基提供网页操作提示。测试使用独立路径、角色和端口，未在玩家存档上执行测试动作。
