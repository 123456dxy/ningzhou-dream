# 炼器与流派法器 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Steps use checkbox syntax for tracking.

**Goal:** 在当前主工程交付可玩的十份自由投料、流派法器和定向等量换牌。
**Architecture:** crafting 统一目录、纯报价、开工与冻结成品；combat 统一主动及入战冻结被动；interface 翻译请求并拒绝过期输入；persistence 同步保存与验证。桌面和手机复用十槽与多选模块。
**Tech Stack:** C++20、现有 JSON/HTTP、原生 ES modules、Visual Studio/MSBuild。
**Spec:** docs/superpowers/specs/2026-10-17-forging-redesign-design.md

## Global Constraints

- 当前工作区源码与 visual-studio/ningzhou.sln；保留本轮开始前全部未提交修改及玩家存档服务。基线在 tmp/forge-redesign-baseline-20261017。
- 不新增材料获取/天机大比；不改朋友联机传输。新目录材料待后续投放，既有材料可用。
- 五行剑武器为抽牌/换牌，不参与元素剑链。rank1..5抽牌/换牌上限1..5；实际费用分别1/2/2/3/3和1/1/2/2/3。
- 先弃实际N再抽N；可选少于上限；空手拒绝；保留牌可选。非法/重复/过期UID及无法等量补牌必须原子拒绝。turn1用，turn2冷却，turn3再用。
- 新Gear字段 ForgeVersion,EffectId,InscriptionId,EffectPower,ForgeEnergy,SourceMaterials；Job.ForgeProduct 冻结全部成器参数。旧版本默认0沿用旧规则。
- 报价无时间/RNG/存档变化；后端重核资格；新增源码同步VS；不提交未获请求的commit。

## Review Focus

- 含空槽、不足十份、重复ID和库存刚变化：预览可恢复且开工不部分扣资源。
- 旧Gear和旧Job：加载/完成不被新规则改写。
- 换牌牌堆损坏或重复UID：零消耗拒绝，而非只弃未补。
- 开轮资源/caps：玩家、NPC/留影及存读档一致，不叠加完整旧通用被动。
- 手机多选、连点、过期响应：不重复动作，错误能退出恢复。

### Task 1: 目录与炼器合同（forge_backend）
**Files:** content/forge_content.*, crafting/forge_rules.*, crafting/economy.cpp, crafting/material_supply.cpp, tests/test_forge_redesign.cpp。
**Interfaces:** ForgeRequest{MaterialIds,MainMaterialId,ShapeId,EffectId,InscriptionId}; ForgeQuote{Valid,Enabled,Reason,Materials,Dimensions,Grade,DimensionScore,Energy,CostSpiritStoneCount,DurationDays,Utilization,SuccessProbability,Product}; mfQuoteForge(Game,Request), mfBeginForge(Game,Request,ExpectedCost), mfCompleteForge(Game,Job)。具体命名见公共头。
- [x] 添加纯查询、十槽/6+独立ID、缺料/非法、真实开工完成/拆解及旧Job测试，先观察失败。
- [x] 新目录独立于旧获取池，参考五维向量与能量；同名汇总，主材来自投料；合法效果前选。
- [x] 新合同冻结Gear；完成和拆解使用冻结材料，不重算；定向检查通过。

### Task 2: 新法器战斗（forge_combat）
**Files:** combat/artifact_effects.*, combat/artifact.cpp, combat/battle.cpp, battle.h, tests/test_forge_artifact.cpp。
**Interfaces:** mfArtifactActionCost(Player,Gear,Battle), mfArtifactEffectDescription(Gear), mfArtifactPassiveDescription(Gear), mfQuoteArtifactAction(Player,Battle,Gear,Targets,RequireTargets); battleTurn(Game,id,target,Targets)与batch preview兼容重载。ArtifactCombatSnapshot见model.h。
- [x] 添加品阶数量、实际费用/冷却、少手/空手/非法/保留/金道/等量/异常原子测试，观察失败。
- [x] data-driven效果白名单实现攻击、防护及资源供给/上限，旧gear沿原分支。
- [x] 冻结双方被动，统一玩家/NPC；等量核心先执行，额外抽随后；通过定向检查。

### Task 3: HTTP与持久化（root）
**Files:** model.h, interface/forge_api.*, api.cpp, interface/equipment_views.cpp, persistence/forge_codec.*, arts_codec.cpp,npc_codec.cpp,game_codec.cpp,battle_codec.cpp,shape_validation.cpp,state_validation.cpp; tests/test_forge_api.cpp。
**Interfaces:** 请求{version:2,materials:[IDs],main,shape,effect,inscription,quoteToken?}; options含materials/shapes/effects/inscriptions；preview.info包含权威结果和quoteToken；batch battle请求{id,targets,expectedTurn,expectedHand}。
- [x] 添加raw错误类型/越界、旧档默认、新Gear/Job/snapshot往返、过期拒绝测试，先观察失败。
- [x] route只分派新版到新模块；复用规则生成options/preview，报价token绑定状态/选择。
- [x] 同步纯snapshotcodec及严格校验；装备视图正确显示新效果不声称旧通用收益；跑API与存档定向检查。

### Task 4: 共用界面（forge_ui）
**Files:** web/forge.mjs,forge.css,artifact-redraw.mjs,app.mjs,artifacts.mjs,v2/app.mjs,v2/views/inventory.mjs,battle.mjs,tools/forge-web-check.mjs。
- [x] 纯选择状态先验证空槽/重复/移除/批量/多选少手行为。
- [x] 十槽、过滤、主材/效果/灵纹、权威预览、来源待开放、过期响应与动作锁。
- [x] 桌面/手机batch多选、取消和恢复；前端契约及语法通过。

### Task 5: 整合与实玩
- [x] generate-vs-projects同步后MSBuild所有受影响目标，修复构建错误；测试注册入harness。
- [x] 隔离存档/端口启动新二进制，核验HTTP200后先给用户网址。
- [x] 真实浏览器炼器/装备/抽换/连点/错误恢复和窄屏检查并记录耗时；检查存读档。
- [x] 独立review审查本轮差量；定向回归后因combat/persistence改动跑全套一次，记录真实结果。
- [x] 交付实际内容、网址、检查与未验证范围；保持未提交。

## Review ledger

独立review三项：清浊毒缓存复活、临时成品无ID导致主动费用缺失、普通NPC主动计划/冷却存档拒绝，均修复并限定复核addressed。初次全回归另捕获新NPC精确毒跳伤初始化被旧哨兵覆盖，最小修复已完成；系统TEMP的天机测试写入失败，在项目独立TEMP隔离重试，未修改朋友维护的天机模块。实际结果待最终记录。

最终验证：当前visual-studio/ningzhou.sln全部5目标MSBuild Release成功；--forge 312断言、完整21342断言均0失败。桌面289契约；炼器29纯契约及33含DOM、v2 102含DOM通过。真实本地Chrome完成十份投料→开炉851ms→装备250ms→少手牌换1张351ms，取消、资源扣除和冷却一致；390px无横溢。报告tmp/forge-ui-live/playtest-report.json。新版preview 53368使用最终二进制与隔离角色，未提交。
