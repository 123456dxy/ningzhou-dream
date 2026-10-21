# 宁州一梦体验与内容补齐编码方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. 本文是交接方案，不表示已实现；用户指定由另一个 agent 编码。

**Goal:** 补齐批量服丹、神通搭配体验、材料来源、天机阁执事、炼丹信息、事件成品法器、本命槽位与法器表现。

**Architecture:** C++20 后端集中提供资格、报价、结算及内容白名单，经典网页和 v2 共用响应。沿现有职责模块扩展；新规则不继续堆入 api.cpp 或 app.mjs。各任务独立完成定向验证，再合入当前主工程。

**Tech Stack:** C++20、CMake、当前 visual-studio/ningzhou.sln、原生 JavaScript/CSS、现有浏览器验收工具。

**Spec:** 用户本轮七条需求及下文“需求与决策”。接口中标为“新增建议”的名字尚不存在，不能误当现有 API。

2026-10-18执行状态已另行核对，见[本轮进度与未完成项更正](../../reviews/2026-10-18-gameplay-update.md)。本文原始清单不是完成证明；原始十项成长队列另有未完成内容。本命已由用户后续明确为单槽可更换，以下早期“待回复/若确认”以此后续决定为准，不需再次询问。

## 需求与决策

| 需求 | 编码口径 |
| --- | --- |
| 滚动条服丹、一键服丹 | 已确认：数量滑条选择同一种丹药的数量，一次请求服用该数量；另提供该种丹药的“最多可服”操作。不跨丹药种类自动混吃。 |
| 服丹、神通装备卸载不回顶部 | 同页动作后保留视觉位置、筛选、详情展开态；异步目录刷新也不能再次拉回顶部。 |
| 已装备神通随页面滚动 | 桌面专用吸附侧栏，内部独立滚动；窄屏提供有高度上限的已装备区，主列表仍可到达。 |
| 天机阁执事 | 特殊 NPC 提供三至五阶突破丹、突破秘法、炼丹材料、炼器材料货架。固定价格不随行情、声望、天赋、本地产地优惠改变。境界范围为筑基至元婴。 |
| 坊市没有一二阶炼器材料 | 按缺货问题处理，补开放的一、二阶器材渠道，不理解成禁止出售。仍保留三阶以上普通坊市禁售。 |
| 炼丹界面优化 | 优先解决选丹方、查看整批成本与缺料、补料、开炉、查看任务的流程；本轮不改丹药成功率和产量平衡。 |
| 事件装备 | 少量白名单 V2 制成品模板随机掉落；事件奖励品阶跟随事件生成时的境界冻结。 |
| 炼器引爆 | 待确认是火法器效果还是炼器操作。未确认前只可规划火池兑现信息，不改伤害倍率。 |
| 法器立绘 | 先接器型白名单外观及缺图回退，素材制作与代码接入分开验收。 |
| 本命神通 | 单个槽位移到神通界面。战斗外是否可更换，仍待用户回复；下文按“允许换神通、筑基路线特性不变”提出方案，不能视为已确认。 |

需要产品配置后才能开放的内容：突破秘法实际效果、执事商品价目/库存、执事人数与驻点、引爆新机制。不要出售没有实际效果的秘法，不要自行创造新伤害系数。

## 全局约束

- 先读 AGENTS.md、docs/BACKEND_ARCHITECTURE.md、docs/CODING_STANDARDS.md 及所改模块 README。
- 直接修改当前工作区，不在旧工作树写完再覆盖；保护已有未提交改动，不使用 git add .。
- 数值、数量上限、耗时、资格、来源、掉落及效果由后端计算；报价和执行共用规则，前端不乘公式。
- 查询不推进活动时钟、共享 RNG 或存档；真实随机只发生在事件或动作提交边界。
- 非法 ID、数量类型、负数、溢出、未知枚举明确拒绝；技术错误不部分扣费或扣物品。
- 新持久字段同步 codec、形状/状态校验及往返测试。用户此前明确不要求额外旧档迁移，不以旧档迁移扩大本轮范围。
- 不手改 data/reference_tables.h；目录/来源扩展放职责模块及既有数据生成链。
- 保留法器固定基础表：直伤20/40/120/450/1800，护体20/50/150/500/2000，绵护3/8/25/100/400。不得恢复战力/最大气血/攻击倍率基础伤害。
- 金丹、元婴本轮不新增突破小游戏。普通 NPC 交情兑换、既有材料情报/秘境不能被执事商店替换。
- 测试用临时目录、临时角色和临时端口；不要修改真实玩家、停服务或把账号存档打进发行包。
- 新增/删除源码后运行 tools/generate-vs-projects.py，CMake 继续为构建源。以当前 VS/MSBuild 编译结果验收。

## 审查重点

1. 异步二次重绘、卡片消失或账号改变后，旧滚动恢复不得跳错页面。
2. 同种丹药逐枚状态会改变耐药和丹毒，批量不能把第一枚效果直接乘数量。
3. 新材料“已经登记”和“已经开放某渠道”是两件事；不能把所有 AcquisitionPending 一次改成可购买。
4. 事件成品不能获得玩家亲炼专属售价加成，也不能通过拆解意外开放未配置来源。
5. 本命更换、事件模板和火法器必须在预览、真实战斗、秘境及留影使用同一规则。

## 任务一 修复位置保留并增加神通吸附栏

**现状：** web/app.mjs 的 call/render 整体替换 workspace；viewArts 还会先显示读取占位、再异步重画，导致页面高度塌缩。服丹 details 被关闭、最后一枚丹药卡片消失也会改变高度。不是简单删除 scrollTo 即可解决。

**文件：** 新增 web/ui-position.mjs；修改 web/app.mjs、web/arts.mjs、web/inventory-panel.mjs、web/enhance.css 的神通专用样式；v2 对应 web/v2/app.mjs、disclosures.mjs、views/arts.mjs、styles.css。

- [ ] 新增建议接口 captureUiPosition(root, scopeKey) / restoreUiPosition(root, snapshot, scopeKey)。快照包含页面滚动、稳定物品/神通ID锚点及偏移、details展开态、按稳定键保存的内部滚动、焦点。
- [ ] 同页动作先保存快照，重绘后先恢复展开态，再恢复内部滚动和视觉锚点。最后一枚卡片消失时锚定相邻卡片，不能强行恢复超出页面高度的数值。
- [ ] 神通目录失效时保留上一份可展示目录，后台刷新；不要把长列表先替换成极短的读取占位。
- [ ] 快照按页签/子页隔离；切页、切角色、进入事件/斗法时作废旧恢复任务。借用既有异步版本检查，不另起无保护的 fetch。
- [ ] 仅神通搭配侧栏增加专用类。外层 sticky，内部 overflow-y:auto、max-height 受视口约束；标题和槽位统计不随内部列表滚走。不要给全站通用 plan-panel 加 sticky。
- [ ] v2 复用 disclosure/focus 能力；新增内部滚动使用业务键，不依赖 DOM 索引。
- [ ] 浏览器验收：下半页服丹/最后一枚/失败恢复、装备卸载后的两次重画、内部列表滚动、慢响应后换页、桌面与390px窄屏。只改布局不补镜像式 C++ 单元测试。

## 任务二 同种丹药数量滑条与批量服丹

**复用：** src/core/progression/arts.cpp 的 pillPlan、artPillUseInfo、artApplyPill。现有 /api/use-pill 只接受 id，尚无批量服丹接口；pillCatalog.maxSale 是出售数量，不能复用成服丹上限。

**文件：** 新增 src/core/progression/pill_batch_rules.h/.cpp、src/core/interface/pill_batch_api.h/.cpp、tests/test_pill_batch.cpp；修改 arts/economy 视图、routes.h/.cpp、api.cpp 的薄分发、两套行囊界面。

**新增建议契约：**

- PillBatchQuote mfQuotePillBatch(const Game&, const string& RecipeId, int Count)。结果含 Enabled/Reason、OwnedCount、MaxUsableCount、RequestedCount、总实际收益、服后丹毒/耐药及最大数量的停止原因。
- string mfUsePillBatch(Game&, const string& RecipeId, int Count)。按精确数量提交，重新核验；若当前只能服更少则整笔拒绝，不暗中少服或多服。
- POST /api/pills/use-quote：{id,count}，纯查询；POST /api/pills/use：{id,count}，动作。原 /api/use-pill 缺省单枚语义保留，并委托同一规则。

- [ ] 在局部候选 Player 状态逐枚调用现有计划/应用，累计真实变化，避免复制整个世界或重复写日志；验证全部可服后一次提交，只记一条批量纪事。
- [ ] maxCount 由后端给出，同时受库存、现有服用条件和具名单次技术上限约束。技术上限采用1,000枚，界面明确“一次最多1,000枚”；不要为每个目录条目无条件预演1,000次，按当前选中丹药计算、命中停止条件即退出。
- [ ] 不擅自增加“收益为零就禁止服用”的新规则；沿用现有单枚资格。突破准备丹同类限制、耐药和丹毒按真实逐枚顺序判断。
- [ ] 滑条与数字框同步，库存不足禁用；拖动过程中节流查询，仅最新选择的响应可显示。提交时动作锁防重复点击；超时不自动重放服丹。
- [ ] “服用所选数量”和“最多可服”都只针对当前一种丹药；后者提交服务端报价的数量，不遍历全背包。
- [ ] 测试：逐枚结果与批量一致、耐药中途到限/库存变化整笔拒绝、非法数量与上溢、查询无副作用、同类突破丹、丹毒累计、批量请求只有一次。浏览器联合任务一验收位置与展开态。

## 任务三 补齐普通坊市一二阶炼器材料

**根因：** content/forge_content.cpp 的新 fm-* 目录已登记560种材料，全部 AcquisitionPending；simulation/play.cpp 的 marketList/marketBuy 只遍历旧 MaterialDef_TABLE。旧一二阶材料本来能买，新目录没有接入，不能只放宽 source_limits。

**文件：** src/core/content/forge_content.h/.cpp；新增 crafting/material_sources.h/.cpp 集中渠道白名单；修改 simulation/play.cpp、crafting/source_limits.cpp、interface/economy_views.cpp，复用 craft_prices.cpp 与 material_supply.cpp。

- [ ] 用显式渠道判定区分 OrdinaryMarket / TianjiSteward / BeastDrop / Pending；保留绿瓶专属来源。新增建议接口 bool mfCanBuyOrdinaryForgeMaterial(const Player&, const string& MaterialId)。列表和真实购买共用它。
- [ ] 普通坊市先接入一二阶矿物材，妖兽部位保持对应妖兽来源；不把224种一二阶新材全部无差别卖出。这是沿用用户此前“妖兽部位来自妖兽、矿物来自商店”的方案建议。
- [ ] 沿用当前资格：炼气买一阶、筑基起买二阶，普通市不卖三阶以上。初版沿用普通坊市现有无限供给，不另加库存刷新系统。
- [ ] 名称、阶级、属性、材类统一由 mfFindForgeMaterial 解析，修正 economy_views::marketGrade 仅查旧表导致新材显示0阶的隐患。
- [ ] materialCatalog 包含已开放未持有材料；不能继续只追加“待开放且已持有”项。SourceNote 随实际渠道更新。
- [ ] 定向测试 marketList→marketBuy→forge-preview 全链路；高阶、专属材料及未开放ID拒绝；补料采购与直接采购一致，不顺带改猎妖/情报随机池。

## 任务四 天机阁特殊执事固定价商店

**边界：** friendship.cpp 的 npcBag/takeFromNpc 是交情兑换，NPC会补货和服丹。商业货架必须与个人行囊分离，不能把待售突破丹塞进 npc.bag。

**文件：** 新增 content/tianji_shop_content.h/.cpp、crafting/tianji_shop.h/.cpp、interface/tianji_shop_api.h/.cpp、tests/test_tianji_shop.cpp；修改特殊NPC生成/识别位置、routes 与两套 NPC/天机阁界面。

**新增建议契约：** TianjiShopQuote mfQuoteTianjiPurchase(const Game&, const string& NpcId, const string& OfferId, int Count)；string mfBuyTianjiOffer(Game&, const string& NpcId, const string& OfferId, int Count, int ExpectedCost)。GET /api/tianji-shop?npc=... 只读目录；POST /api/tianji-shop/buy 提交 {npc,offer,count,expectedCost}。

TianjiShopQuote 至少含 Enabled/Reason、NpcId/OfferId、Count、FixedUnitPrice、TotalCost、OwnedCount、MaxPurchaseCount；金额乘法用int64核验后才转既有灵石存储类型。前端显示报价对象，不再次乘单价。

- [ ] 特殊NPC使用稳定模板ID/身份标记，驻天机阁，不通过中文称呼判断。其筑基至元婴境界不自动决定玩家购货门槛；门槛从 Offer 配置读取。
- [ ] Offer 白名单字段明确 OfferId、ItemKind、ItemId、Grade、FixedUnitPrice、RequiredStage。炼丹材料与矿物材选择实际开放项；妖兽部位不因材料目录全量遍历而自动上架。
- [ ] 直接读取固定单价，不调用 mfNormalBuyPrice、声望/行情/商贾/本地优惠函数。先固化现有本地基价形成可审阅价目表，不能使用外部参考 sourcePrice；新增秘法需单独指定价格与已实现的效果ID。
- [ ] 初版建议无限供给以复用现有市场模型；若用户要求限购，再增加独立商店库存、真实刷新边界及 codec，不复用个人行囊或在查询中刷新。该库存选择与具体价目需用户确认后开放。
- [ ] 购买按候选状态整体核算：当前位置、执事身份、商品ID、数量、余额、库存/容量及宽整数金额；提交重新报价，过期 expectedCost 明确拒绝。
- [ ] 突破秘法先定义是消耗品准备、永久领悟还是学习条目，再接真实突破入口；未定义效果的条目不售卖。金丹元婴继续现有突破，不借此新增小游戏。
- [ ] 原天机阁情报与执事交易分别显示。普通坊市高阶拒绝提示可增加“天机阁执事”实际已开放途径。
- [ ] 测试不同声望/行情/商贾天赋报价完全相同；重复购买、非法ID/数量、金额边界、异地交易拒绝及不部分扣费；购入卖出不形成确定套利；浏览器商品分类、错误恢复及窄屏。

## 任务五 重整炼丹界面与整批报价

**现状：** 经典 viewMarket() 内已有丹方、1/3/5/10炉、补料和任务；v2 brewView 更简略。后端已给 ingredients，但手机文案误称未提供；两端“逐炉扣料”与后端开工一次扣整批料、灵石和耐久不符。

**文件：** 新增 web/alchemy.mjs、web/alchemy.css、src/core/crafting/alchemy_rules.h/.cpp、tests/test_alchemy_quote.cpp；将经典炼丹渲染从 app.mjs 抽出，更新 web/index.html 的样式引用；修改 web/v2/views/inventory.mjs 及对应样式；复用 economy.mjs、craft-pricing.mjs、workshop.mjs。整批报价由 interface/economy_views.cpp 编码，ecoBegin 的炼丹分支改为消费该权威报价。

- [ ] 桌面采用“丹方列表 / 选中丹方详情 / 本批报价与开炉”布局；窄屏纵向排列，操作摘要保持短小。品阶、用途、搜索与选中丹方可保留。
- [ ] 同一屏展示：本批炉数、产量/成功概率、实际药效、丹毒耐药、每项所需/持有/缺少、灵石、总天数、丹炉耐久和阻止理由。
- [ ] 新增建议 AlchemyQuote mfQuoteAlchemy(const Game&, const string& RecipeId, int BatchCount)，由既有真实开炉校验和冻结合同逻辑共用。前端不再自己把单炉数值乘炉数。不要直接让 quote 调用会推进RNG/开工的 ecoBegin。
- [ ] AlchemyQuote 至少包含 Enabled/Reason、RecipeId/BatchCount、Ingredients各项Required/Owned/Missing、StoneCost、DurationDays、DurabilityCost、SuccessProbability、OutputPerSuccess和ExpectedOutput；后两者清楚区分。接口建议 POST /api/alchemy/quote {id,count}，登记为纯查询；真实开炉继续既有动作入口，不另建第二套任务。
- [ ] “一键补料”费用与“开炉费用”分列。补料失败保持丹方/炉数/滚动；成功后重查整批报价，不自动开炉。
- [ ] 文案改成“开炉时一次投入整批材料与费用”；区分期望收入、实际成丹、失败损耗、出售收入，不承诺即时收益。
- [ ] 测试整批报价与真实扣料/时间/耐久一致、缺料后恢复、报价过期、切丹方时旧响应失效；真实浏览器覆盖补料→开炉→查看冻结任务。保留现成功率与产量规则。

## 任务六 事件随机掉落固定模板制成法器

**现状：** exploration/adventure.cpp 的后山装备仍手工构造旧 ForgeVersion=0，并按 advSeq 奇偶交替剑/衣。adventureGear 还用于出生天赋，不可整体替换。V2成品构造嵌在 mfQuoteForge，不能靠伪造玩家材料和器道绕过资格来生成奖励。

**文件：** 新增 content/gear_templates.h/.cpp；从 crafting/forge_rules.cpp 抽出纯配方求值到 forging_product.h/.cpp；修改 adventure.cpp、Gear/AdvContext/Battle所需字段、对应codec和校验、craft_prices.cpp 的亲炼加成判定。

- [ ] 新增建议 ForgeRecipeResult mfEvaluateForgeRecipe(const ForgeRequest&)，只从合法配方计算五维、品阶、器能、EffectPower与成品基础，不看玩家库存/工作/器道、不消耗RNG；mfQuoteForge在其上叠加玩家报价与资格。保证原有配方测试数值不变。
- [ ] ForgeRecipeResult 明确包含 Valid/Reason、Dimensions、DimensionScore、Utilization、Grade、Energy、RequiredEnergy和Product；不含玩家库存、费用、耗时或成功率。奖励工厂只接收完整可成器配方，玩家投料中间预览继续允许不足十份的中间状态。
- [ ] 模板目录初版建议6种：破阵剑 damage、引念剑 draw、换念剑 redraw、护体钟 shield、清浊钟 cleanse、绵护珠 turn_shield。模板写死合法器型/属性/效果/十份配方，无随机词条；按1—5阶配置可通过规则校验的配方。
- [ ] 新增建议 mfCreateGearFromTemplate(TemplateId, Grade) 消费同一纯工厂。V2必须具备合法 SourceMaterials、ForgeEnergy、EffectPower、维度、slot/effect，不能只补版本号。
- [ ] 真实事件生成时按奖励池随机选择一次并冻结 TemplateId/Grade/RewardVersion；开战、预览、读取和结算不重抽。同步 AdvContext→Battle→codec及校验，生成唯一装备ID只在提交奖励时进行。
- [ ] 品阶沿用现有事件生成时境界映射，最高5阶；本轮先处理装备奖励，其他事件的灵石/材料增长用独立奖励配置入口逐类迁移，不对所有旧奖励直接乘境界倍率。
- [ ] Gear 增加明确来源，例如 Origin=PlayerForged/EventReward，保留 V2合法制成品条件，但亲炼专属售值天赋只对PlayerForged生效。事件掉落可装备、出售和按真实配方拆解；配方仅用已开放材料，避免拆出尚未配置来源的新材料。
- [ ] 测试固定种子覆盖模板、事件冻结跨存读档不重抽、出生天赋不变、V2往返、报价/实战能力、出售/拆解及奖励拒绝不部分提交。

## 任务七 神通界面的单一本命槽

**现状：** Arts.LifeboundSpell 已是单个ID，LifeboundKind 已记录 innate/retain。旧 /api/zhuji bind 永久锁定，web/zhuji.mjs 写“不能更改”。战斗仅给匹配神通的第一张副本加特性，不需要持久化每战 cardUid。

**文件：** 新增 progression/lifebound_rules.h/.cpp；修改 interface/arts_views.cpp、zhuji_api.cpp、routes/API薄分发、web/arts.mjs、app.mjs、web/zhuji.mjs、v2/views/arts.mjs，扩展 tests/test_lifebound_cards.cpp。

- [ ] 新增建议 LifeboundQuote mfQuoteLifeboundSpell(const Game&, const string& SpellId)；string mfSetLifeboundSpell(Game&, const string& SpellId)。新增 POST /api/arts/lifebound {spellId}，明确设置目标，不做 toggle；重复设相同目标无资源副作用。
- [ ] LifeboundQuote 含 Enabled/Reason、SpellId、Kind、IsCurrent、IsCarried；神通目录和真实设置入口共用它。拒绝时不清空原槽位。
- [ ] 若用户确认可换，战斗外可 A→B→A；类型继续由筑基路线决定，不能自由切换固有/保留。若用户确认不可换，保留原锁定，只迁移选择入口；两种规则不能同时存在。
- [ ] 允许当前已掌握、境界可用的普通神通；本轮不扩展成法器主动、秘境临时牌或尚未领悟神通。固定攻防牌是否纳入“任意一张”需明确，不靠前端隐藏保证资格。
- [ ] 槽位建议作为唯一“特性绑定槽”，不额外增加普通携带上限。卸下目标神通后保留绑定，但显示“未携带，本命当前不生效”；不得自动挤掉其他神通或暗中增牌。此槽位语义应在实施前向用户展示。
- [ ] 神通目录返回本命状态、是否可设置和reason；神通页显示一个槽位及每张可选项的“设为本命”。突破结算引导去同一神通页，不保留第二套永久绑定UI。
- [ ] 旧筑基bind入口退为同一领域规则的适配，避免两个接口互相矛盾。保留 LifeboundSpell/Kind 字段，不新建本命数组。
- [ ] 战斗仍只强化第一张副本：固有占正常起手名额，保留占正常手牌容量；不因为换本命刷新已经冻结的战斗/秘境/留影牌组。
- [ ] 测试同目标重复、替换规则、未掌握/非法ID、战斗中拒绝、未携带提示、两副本仅一张特性、秘境/留影、刷新和存读档；两端UI联合滚动验收。

## 任务八 引爆优化与法器立绘

**引爆当前规则：** artifact_effects.cpp 的 fire_burst = 品阶固定直伤×灵纹 + 当前灼烧池×剩余回合，清空火池；目前明确不吃人物火道引爆成长，test_artifact_fixed_values.cpp 有对应断言。

- [ ] 先确认“优化”指效果还是炼器操作。若指火法器，先补后端结果拆解：基础伤害、火池值、剩余回合、兑现伤害、被盾吸收、实际损血、清空结果。工坊写规则，斗法写当前可兑现的真实值，前端不重算。
- [ ] 若要改倍率/消耗规则，需先给出新规则表，再同时修改真实入口、预览、说明、NPC/留影及测试。没有确认就保留当前固定基数与倍率，不擅自接入 fire_growth 的人物成长。
- [ ] 若实际指炼器操作，转入任务五同类交互：投料/主材/效果/灵纹的报价反馈和成器结果；与战斗引爆数值分开做。

**外观文件：** 新增 content/artifact_appearances.h/.cpp 与 web/assets/artifacts/；修改 forge_api.cpp、equipment_views.cpp、artifactView输出，以及 web/forge.mjs、inventory与两套斗法展示。

- [ ] 当前仅有11器型内联SVG。先按 ShapeId 提供11个白名单外观ID及后端路径映射，参考 beast_portraits 的白名单模式。
- [ ] 新增建议 mfFindArtifactAppearance(ShapeId)，API返回 appearanceId/illustration。初版从器型推导即可，不为每件装备保存任意资源路径；模板特色图后续按稳定模板ID扩展。
- [ ] 工坊预览、成品详情、装备与斗法详情共用映射；没有新素材时保留SVG回退，不用全部相同的临时图片冒充立绘完成。
- [ ] 验收素材11类齐全、透明边缘/缩放/文字对比、缺图回退、非法ID不产生任意路径、桌面/窄屏均不遮挡操作。素材生成需另外按图片技能流程执行，代码agent不必自行重画角色立绘。

## 执行顺序与交付

建议顺序：任务一→二→三→四→五→六→七→八。UI滚动先做，避免后续新增动作继续跳动；事件模板在纯法器工厂抽出后再接入。

若有多名agent：前端体验、商店来源、法器/事件可分别分析或做独立模块，但 app.mjs、api.cpp、model.h、routes、codec 的公共接线由一个集成者串行完成，避免同文件相互覆盖。

- [ ] 每项先补有价值的失败用例，再改对应领域规则；纯样式只做浏览器验证。不要为每个小改动重复跑整套全量。
- [ ] 扩展 tests/test_main.cpp/harness.h 时增加明确的定向入口；不要假设仓库已有 --pill-batch 或 --tianji-shop 参数。现有 --forge 可覆盖法器相关回归。
- [ ] 新增C++后运行 Python tools/generate-vs-projects.py；MSBuild构建 visual-studio/ningzhou.sln 的Release/x64，可指定 build-vs-current/Release 输出，保护被运行服务锁定的其他目录。
- [ ] 临时完整服真实浏览器验证经典桌面、390px窄屏及v2受影响流程；记录请求数量、错误后恢复及交互耗时。新增跨模块状态/冻结合同后，再按风险执行一次全量C++回归。
- [ ] 交付列出实际修改、实际命令/结果和未做范围。源码编译、生成发行包、把当前运行服务切到新版是三个步骤，不能互相冒充完成。用户未要求打包时不自动生成新发行版。

本轮方案阶段仅做代码只读分析与文档整理，未实施上述任务、未执行这些新功能测试。

构建命令示例（执行者应在自己的实现结束后运行，本方案阶段未执行）：

```powershell
& 'C:/Users/24974/AppData/Local/Programs/Python/Python311/python.exe' tools/generate-vs-projects.py
& 'D:/VisualStudio/2022/Community/MSBuild/Current/Bin/MSBuild.exe' visual-studio/ningzhou.sln /m /p:Configuration=Release /p:Platform=x64 "/p:OutDir=$((Get-Location).Path)\build-vs-current\Release\" /nologo /v:minimal
```

把新行为测试注册到相应定向入口后，从独立临时工作目录运行绝对路径的测试EXE。退出码0且对应断言通过才可声称通过；使用独立TEMP/TMP，不借历史日志交付。
