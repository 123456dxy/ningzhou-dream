# C++ API 声明索引与统计

本清单依据 2026-10-12 工作区头文件，用于在 Visual Studio 中按模块查找现有 C++ 调用入口。11 个模块是实现职责与导航分类，当前共同编译为 `nzcore`，不是 11 个已隔离的库。各模块设计见 [后端架构](BACKEND_ARCHITECTURE.md) 及模块 README。

2026-10-13 新增入口补充（不计入下方历史统计）：`character_build.h` 统一建角方案与立绘目录；`progression/manual_study.h` 提供功法研习报价；`social/npc_consumables.h` 管理 NPC 服丹动作与药效状态；`economy.h` 的 `ecoMarketTier` / `ecoMarketBudget` 提供分层月预算。规则与维护约定见 [代码规范](CODING_STANDARDS.md)、[初始天赋](INITIAL_TALENTS.md)、[研习与 NPC 丹药](MANUAL_STUDY_AND_NPC_PILLS.md) 和 [炼丹经济](ALCHEMY_ECONOMY.md)。

## 统计口径

本轮新增入口另列，不改写2026-10-12的历史口径：`crafting/craft_prices.h`提供完整材料定价报价，`crafting/craft_timing.h`提供品阶时长，`crafting/cave_garden.h`提供灵田纯报价和原子命令；`resource_points.h`提供默认关闭的强ID开发目录与功能闸门。`combat/battle_scaling.h`集中攻击、毒伤、灼烧的战力伤害基数。`shop.h`新增纯迁府报价。洞府和定价维护方式分别见 [洞府灵田](CAVE_GARDEN.md)、[制作时长](CRAFT_TIMING.md)、[资源点设计](RESOURCE_POINTS.md)。`interface/cave_views.h`和`interface/craft_price_views.h`为只读JSON适配，仍归内部接口而非新的HTTP协议入口。

- 公共统计范围：core 根目录保留的兼容公共头，加 `content/game_ids.h`、`content/text_table.h` 、`interface/routes.h` 和 `crafting/material_supply.h`。这里的“公共”指调用方可使用的 C++ 入口；项目目前没有安装/导出头的独立边界。
- 只计 namespace 顶层、以分号结束的显式自由函数声明。每一处声明计 1 条，函数重载分别计数；重复声明仍是两条声明，另给去重数。
- 不计 struct/class 成员、构造函数、静态成员函数、函数对象/回调类型、控制语句、数据字段、常量、类型声明，以及 cpp 文件中的实现或局部函数。
- `inline` / `template` 若有显式分号声明，按上述规则纳入；现有公共头中这两类只有头内定义，故不计入主表，另列补充清单。
- `interface/views.h` 与 `interface/view_policy.h` 是内部跨文件协作接口，单独列明；`interface/view_helpers.h`、`persistence/internal.h`、`persistence/json_helpers.h` 的私有 helper/codec 不计入公共 API。
- 统计按公共头的维护归属分配；`model.h` 是共享契约，在表中归 foundation，其中非内联规则实现仍在 `simulation/rules.cpp`。`api.cpp`、`save.cpp` 仍在 core 根。
- `tianji.h`、`http.h`、`pwhash.h` 为朋友维护的多人/服务端对接契约。对应实现 `tianji.cpp`、`http.cpp`、`pwhash.cpp` 仍在 core 根，本轮不修改它们及 `web_main.cpp`；纳入索引仅方便调用查询，不表示纳入本地实现维护。
- 清单先按 namespace/类型/函数体作用域筛选，再核对多行声明和默认 `{}` 参数；未将类成员或函数体语句当作自由函数。统计是头文件声明口径，不是 HTTP 路由数，也不是编译后导出符号数。

## 汇总

| 模块 | 公共头数 | 显式自由函数声明数 | 头内自由函数定义（主统计外） | 主要 namespace |
| --- | ---: | ---: | ---: | --- |
| [combat](../src/core/combat/README.md) | 4 | 50 | 0 | `nz`, `nz::conflict` |
| [content](../src/core/content/README.md) | 2 | 24 | 0 | `nz::content` |
| [crafting](../src/core/crafting/README.md) | 4 | 52 | 0 | `nz`, `nz::shop` |
| [exploration](../src/core/exploration/README.md) | 4 | 36 | 0 | `nz` |
| [foundation](../src/core/foundation/README.md) | 3 | 10 | 21 | `nz`, `nz::json` |
| [interface](../src/core/interface/README.md) | 3 | 12 | 0 | `nz::api` |
| [persistence](../src/core/persistence/README.md) | 1 | 9 | 0 | `nz` |
| [progression](../src/core/progression/README.md) | 4 | 74 | 0 | `nz`, `nz::cultivate`, `nz::legacy` |
| [simulation](../src/core/simulation/README.md) | 7 | 61 | 0 | `nz`, `nz::play`, `nz::tianji`, `nz::tide` |
| [social](../src/core/social/README.md) | 6 | 83 | 0 | `nz`, `nz::friendship`, `nz::play`, `nz::renown` |
| [transport](../src/core/transport/README.md) | 2 | 6 | 0 | `nz::http`, `nz::pw` |
| **合计** | **40** | **417** | **21** | — |

公共声明共 **417 条**；本清单中相同 namespace 与签名去重后为 **415 个自由函数签名**。差额来自 `nz::power(const Person&)` 在 `sim.h` 中出现两次，以及 `nz::swordGain(Game&, int)` 分别在 `sim.h` 与 `sword.h` 声明。它们不是新的重载。头内定义的 21 条另外列出，不与显式声明混加。

重载示例：`calendarDate(int)` 与 `calendarDate(const Game&)` 分别计数；`stableId` 的四个枚举参数版本、`displayName` 的两个版本、`renown::sources` 的两个版本也分别计数。对象成员入口单独列在后文，未计入上述数字。

责任归属单列如下，避免把索引归类误读成实现所有权：

| 范围 | 归属与实际实现路径 | 公共头数 | 显式自由函数声明数 |
| --- | --- | ---: | ---: |
| 天机大比 | 朋友维护，仅接口对接；`src/core/tianji.cpp` | 1（`tianji.h`） | 3（另有 Store 对象成员，不计入） |
| HTTP / 密码 | 朋友维护，仅接口对接；`src/core/http.cpp`、`src/core/pwhash.cpp` | 2 | 6 |
| 其余索引范围 | 本地规则与调用入口；具体职责见各模块 README | 37 | 408 |
| 合计 | 所有已列明公共头 | 40 | 417 |

## 按模块列出公共头与函数声明

### combat · 斗法与冲突

[模块设计说明](../src/core/combat/README.md)

#### `battle.h` · 31 条声明

头文件：[battle.h](../src/core/battle.h)。

对象/枚举类型：`nz::SpellDef`、`nz::Intent`、`nz::EnemySpec`、`nz::TurnResult`、`nz::Preview`、`nz::EnemyPreview`。

namespace：`nz`

```cpp
const vector<SpellDef>& allSpells();
const SpellDef* battleSpell(const string& id);
string battleSpellDescription(const SpellDef& spell);
bool battleNeedsTarget(const SpellDef& spell);
double battleSwordBonus(const Player& p);
int battleBasicCopies();
int battleSpeed(const Player& p);
double battleEscapeChance(const Player& p, const Battle& b);
int battleSpellSlots(const Player& p);
int battleEnergy(const Player& p);
int battleManaCap(const Player& p);
int battleNextMana(const Player& p, const Battle& b);
double battleReduction(const Player& p, const Battle& b);
int battleCost(const Player& p, const SpellDef& x);
Intent battleIntent(const Battle& b);
Intent spellToIntent(const SpellDef& sp);
void battleStart(Game& g, const EnemySpec& enemy);
void battleStartShadow(Game& encounter, const Player& challengerSnapshot, const Player& opponentSnapshot, uint32_t seed);
const string& battleShadowOutcome(const Game& encounter);
TurnResult battleTurn(Game& g, const string& id, const string& target = "");
void battleDraw(Game& g, int n);
Preview battlePreview(const Player& p, const Battle& b, const string& id);
Battle battleReferenceState(const Player& p);
Battle battleReferenceWithPreparation(const Player& p, const string& key, double value);
Preview battleReferencePreview(const Player& p, const SpellDef& spell);
string battlePreviewConditions(const Player& p, const SpellDef& spell);
EnemyPreview battleEnemyPreview(const Game& g);
bool battleDefeatEndsLife(const Battle& b);
void battleFinish(Game& g, bool win, bool escape = false);
bool battleActive(const Game& g);
vector<string> handSpells(const Battle& b);
```

#### `artifact.h` · 4 条声明

头文件：[artifact.h](../src/core/artifact.h)。

对象/枚举类型：`nz::ArtifactAbility`。

namespace：`nz`

```cpp
string artifactShape(const Gear& gear);
ArtifactAbility artifactAbility(const Gear& gear);
vector<ArtifactAbility> equippedArtifactAbilities(const Player& player);
optional<ArtifactAbility> equippedArtifactAbility(const Player& player, const string& id);
```

#### `sword.h` · 7 条声明

头文件：[sword.h](../src/core/sword.h)。

对象/枚举类型：`nz::SwordDuel`。

namespace：`nz`

```cpp
const vector<data::SwordRealmDef>& swordRealms();
int swordRank(int swordValue);
string swordRealmName(int swordValue);
bool swordCultivator(const Player& p);
bool swordCultivatorOf(const Game& g);
SwordDuel swordDuelOf(const Player& p, const Battle& b);
void swordGain(Game& g, int amount);
```

#### `conflict.h` · 8 条声明

头文件：[conflict.h](../src/core/conflict.h)。

对象/枚举类型：`nz::conflict::ConflictResult`。

namespace：`nz::conflict`

```cpp
std::string response(const Game& g, const Npc& n, const std::string& intent);
ConflictResult doConflict(Game& g, const std::string& npcId, const std::string& intent);
std::string conflictError(const Game& g, const std::string& npcId, const std::string& intent);
void loot(Game& g, Npc& n, double ratio);
void crime(Game& g, Npc& n, const std::string& intent);
void battleEnd(Game& g, const Battle& b);
std::string notorietyNote(const Game& g);
void refresh(Game& g);
```

### content · 标识与文案

[模块设计说明](../src/core/content/README.md)

#### `content/game_ids.h` · 19 条声明

头文件：[content/game_ids.h](../src/core/content/game_ids.h)。

对象/枚举类型：`nz::content::DaoId`、`nz::content::SectId`、`nz::content::SpellKind`、`nz::content::BattleKind`。

namespace：`nz::content`

```cpp
std::span<const DaoId> allDaoIds();
std::span<const SectId> allSectIds();
std::span<const SpellKind> allSpellKinds();
std::span<const BattleKind> allBattleKinds();
std::optional<DaoId> parseDaoId(std::string_view value);
std::optional<SectId> parseSectId(std::string_view value);
std::optional<SpellKind> parseSpellKind(std::string_view value);
std::optional<BattleKind> parseBattleKind(std::string_view value);
std::optional<DaoId> parseArtifactElement(std::string_view value);
std::string_view stableId(DaoId id);
std::string_view stableId(SectId id);
std::string_view stableId(SpellKind id);
std::string_view stableId(BattleKind id);
std::string_view legacyDaoKey(DaoId id);
std::string_view legacyValue(SpellKind id);
std::string_view legacyValue(BattleKind id);
int daoOrdinal(DaoId id);
DaoId sectDao(SectId id);
bool generates(DaoId previous, DaoId next);
```

#### `content/text_table.h` · 5 条声明

头文件：[content/text_table.h](../src/core/content/text_table.h)。

对象/枚举类型：`nz::content::TextId`、`nz::content::TextArg`。

namespace：`nz::content`

```cpp
std::string_view displayName(DaoId id);
std::string_view displayName(SectId id);
std::string_view text(TextId id);
std::string formatText(TextId id, std::initializer_list<TextArg> arguments = {});
std::string formatTextTemplate(std::string_view pattern, std::initializer_list<TextArg> arguments = {});
```

### crafting · 制作与经济

[模块设计说明](../src/core/crafting/README.md)

#### `economy.h` · 13 条声明

头文件：[economy.h](../src/core/economy.h)。

对象/枚举类型：`nz::EcoActionInfo`。

namespace：`nz`

```cpp
void ecoRefresh(Game& g);
void ecoSyncBooks(Game& g);
bool ecoAcquireBook(Game& g, const string& id, const string& source);
int ecoSalePrice(const Game& g, const string& type, const string& id);
void ensureEconomy(Game& g);
int ecoCraftDays(const Player& p, const string& type, int grade);
bool ecoIsCity(const string& location);
EcoActionInfo ecoActionInfo(const Game& g, const string& type, const string& id, int batch = 1);
string ecoActionError(const Game& g, const string& type, const string& id, int batch = 1);
void ecoBegin(Game& g, const string& type, const string& id, int batch = 1);
void ecoComplete(Game& g, const Job& job);
void ecoHuntEnd(Game& g, bool win);
const vector<struct TaskDef>* ecoTaskList();
```

2026-10-14 一键交易新增入口（历史声明数量保持原口径）：`shop.h` 的 `PotionSaleQuote` 为只读值对象，`mfQuoteMaxPotionSale(const Game&, const std::string&)` 及接收 `MarketSaleContext` 的重载返回单种丹药最大可售数、单价、总收益和资格；`mfSellPotions(Game&, const std::string&, int Count, int ExpectedUnitPrice=-1)` 重新核验并一次结算。`tide.h` 的 `MarketItem` 新增最大可买数量/成本/原因，仅扩大报价，不增加路由或持久字段。`foundation/state_limits.h` 集中一般持久数值上限。HTTP字段和测试边界见 [一键交易记录](reviews/2026-10-14-quick-trade.md)。

#### `shop.h` · 28 条声明

头文件：[shop.h](../src/core/shop.h)。

对象/枚举类型：`nz::shop::ActionQuote`、`nz::shop::SellResult`、`nz::shop::CaveTierDef`、`nz::shop::ManualSource`、`nz::shop::LibrarySpellSource`。

namespace：`nz::shop`

```cpp
int salePrice(const Game& g, const std::string& kind, const std::string& id);
ActionQuote saleQuote(const Game& g, const std::string& kind, const std::string& id);
ActionQuote gearActionQuote(const Game& g, const std::string& action, const std::string& id);
ActionQuote furnaceRepairQuote(const Game& g);
SellResult sell(Game& g, const std::string& kind, const std::string& id);
int corePrice(const Game& g, bool buying);
int pillPrice(const Game& g, bool buying);
SellResult tradeStock(Game& g, const std::string& kind, bool buying, int count);
const std::vector<CaveTierDef>& caveTierDefs();
int caveMaxSupply(const Player& player);
std::string caveUpgradeError(const Game& game);
SellResult caveUpgrade(Game& game);
SellResult caveSupplySet(Game& game, const std::string& kind, int level);
SellResult caveMove(Game& game);
ManualSource manualSource(const Game& g, const std::string& id);
LibrarySpellSource librarySpellSource(const Game& g, const std::string& id);
std::vector<LibrarySpellSource> availableLibrarySpells(const Game& g);
SellResult librarySpellExchange(Game& g, const std::string& id);
SellResult bookBuy(Game& g, const std::string& id);
SellResult libraryExchange(Game& g, const std::string& id);
std::vector<ManualSource> availableManuals(const Game& g);
SellResult unequip(Game& g, const std::string& id);
SellResult salvage(Game& g, const std::string& id);
SellResult inscribe(Game& g, const std::string& id);
SellResult repairFurnace(Game& g);
std::vector<const data::FurnaceDef*> buyableFurnaces(const Game& g);
SellResult buyFurnace(Game& g, const std::string& id);
SellResult cancelJob(Game& g);
```

#### `source_limits.h` · 4 条声明

头文件：[source_limits.h](../src/core/source_limits.h)。

namespace：`nz`

```cpp
int ordinaryHerbGradeLimit(const Player& player);
int ordinaryMaterialGradeLimit(const Player& player);
string ordinaryHerbError(const Player& player, int grade);
string ordinaryMaterialError(const Player& player, int grade);
```

### exploration · 交通与探索

[模块设计说明](../src/core/exploration/README.md)

#### `travel.h` · 6 条声明

头文件：[travel.h](../src/core/travel.h)。

对象/枚举类型：`nz::TravelOption`、`nz::TravelPlan`。

namespace：`nz`

```cpp
bool travelModeKnown(const string& mode);
string travelModeName(const string& mode);
TravelPlan travelPlan(const Game& g, const string& to);
const TravelOption* travelOption(const TravelPlan& plan, const string& mode);
string travelBegin(Game& g, const string& to, const string& mode = "boat");
bool travelAmbushTick(Game& g);
```

#### `location_services.h` · 3 条声明

头文件：[location_services.h](../src/core/location_services.h)。

对象/枚举类型：`nz::LocationService`。

namespace：`nz`

```cpp
vector<LocationService> locationServices(const Game& g, const string& location);
string locationServiceBegin(Game& g, const string& id, string& targetTab);
void locationServiceComplete(Game& g, const Job& job);
```

#### `adventure.h` · 11 条声明

头文件：[adventure.h](../src/core/adventure.h)。

namespace：`nz`

```cpp
const vector<string>& advKinds();
void ensureAdventure(Game& g);
void adventureBirth(Game& g);
bool adventureGear(Game& g, const string& name, const string& slot, int grade, double bonus, const string& element, const string& source);
void adventureEvent(Game& g, const string& kind);
void adventureExplore(Game& g, int days);
string adventureActionError(Game& g, const string& id);
void adventureBegin(Game& g, const string& id);
void adventureComplete(Game& g);
bool adventureChoose(Game& g, const string& optionId);
void adventureBattleEnd(Game& g, const Battle& b);
```

#### `dungeon.h` · 15 条声明

头文件：[dungeon.h](../src/core/dungeon.h)。

对象/枚举类型：`nz::DungeonRelicDef`、`nz::DungeonIntelOption`。

namespace：`nz`

```cpp
const vector<DungeonRelicDef>& dungeonRelics();
void ensureDungeons(Game& g);
void migrateDungeonRewards(Game& g);
void dungeonYearTick(Game& g);
string dungeonEnterError(const Game& g, const DungeonSite& site);
bool dungeonNodeAvailable(const DungeonRun& run, const string& nodeId);
string dungeonEnter(Game& g, const string& siteId);
string dungeonChoose(Game& g, const string& nodeId);
string dungeonClaim(Game& g, const string& rewardId);
string dungeonLeave(Game& g);
void dungeonAbandon(Game& g);
vector<DungeonIntelOption> dungeonIntelOptions(const Game& g);
string dungeonIntel(Game& g, const string& kind, const string& id);
void dungeonBattleComplete(Game& g, const Battle& battle, bool won);
string dungeonStateError(const Game& g);
```

### foundation · 基础工具与共享模型

[模块设计说明](../src/core/foundation/README.md)

#### `base.h` · 0 条声明

头文件：[base.h](../src/core/base.h)。

对象/枚举类型：`nz::Rng`、`nz::ItemStack`、`nz::LogEntry`、`nz::HistoryEntry`。

无显式自由函数声明；主要通过对象成员或下方的头内定义调用。

#### `model.h` · 10 条声明

头文件：[model.h](../src/core/model.h)。

对象/枚举类型：`nz::RealmDef`、`nz::SectDef`、`nz::LocationDef`、`nz::PlanDef`、`nz::TalentDef`、`nz::BuildStatDef`、`nz::Build`、`nz::Card`、`nz::EnemyCombat`、`nz::Battle`、`nz::Gear`、`nz::Arts`、`nz::NpcBond`、`nz::Person`、`nz::Npc`、`nz::Player`、`nz::SectState`、`nz::World`、`nz::Clock`、`nz::Job`、`nz::Payroll`、`nz::Bonds`、`nz::Adventure`、`nz::ChoiceOption`、`nz::Economy`、`nz::AdvContext`、`nz::Pending`、`nz::Ending`、`nz::BeastTide`、`nz::DungeonReward`、`nz::DungeonSite`、`nz::DungeonNode`、`nz::DungeonRun`、`nz::DungeonWorld`、`nz::AutoCheckpoint`、`nz::Game`、`nz::InnateRoll`。

namespace：`nz`

```cpp
const RealmDef& realmOf(int stage);
const SectDef* findSect(const string& id);
const LocationDef* findLoc(const string& id);
const TalentDef* findTalent(const string& id);
const PlanDef* findPlan(const string& id);
bool isCity(const string& locId);
Build buildCharacter(const vector<string>& traits, const map<string, int>& allocation, int budget = BASE_POINTS, unsigned seed = 0);
InnateRoll rollInnate(unsigned seed);
bool hasTalent(const Player& p, const string& id);
double legacyEfficiency(const Player& p);
```

#### `json.h` · 0 条声明

头文件：[json.h](../src/core/json.h)。

对象/枚举类型：`nz::json::Type`、`nz::json::Value`。

无显式自由函数声明；主要通过对象成员或下方的头内定义调用。

### interface · API 边界与视图

[模块设计说明](../src/core/interface/README.md)

#### `api.h` · 6 条声明

头文件：[api.h](../src/core/api.h)。

对象/枚举类型：`nz::api::Session`。

namespace：`nz::api`

```cpp
void bindSession(Session* s);
bool restoreGame(Session& s, const std::string& text, std::string* err);
Game& game();
json::Value battleView(const Game& g);
json::Value equipmentView(const Player& player, const Gear& gear);
http::Response handle(const http::Request& req);
```

#### `api_exploration.h` · 3 条声明

头文件：[api_exploration.h](../src/core/api_exploration.h)。

namespace：`nz::api`

```cpp
json::Value dungeonView(const Game& g);
json::Value caveView(const Game& g);
json::Value checkpointView(const Game& g);
```

#### `interface/routes.h` · 3 条声明

头文件：[interface/routes.h](../src/core/interface/routes.h)。

对象/枚举类型：`nz::api::RouteEffect`、`nz::api::RouteId`、`nz::api::RouteInfo`。

namespace：`nz::api`

```cpp
std::span<const RouteInfo> routeTable();
const RouteInfo& lookupRoute(std::string_view path);
const char* routePath(RouteId id);
```

### persistence · 存档

[模块设计说明](../src/core/persistence/README.md)

#### `save.h` · 9 条声明

头文件：[save.h](../src/core/save.h)。

对象/枚举类型：`nz::SaveSlotInfo`。

namespace：`nz`

```cpp
std::string saveToJson(const Game& g);
bool loadFromJson(const std::string& text, Game& out, std::string* err);
bool loadDetachedFromJson(const std::string& text, Game& out, std::string* err);
bool saveToFile(const Game& g, const std::string& path, std::string* err);
bool loadFromFile(const std::string& path, Game& out, std::string* err);
std::string defaultSaveDir();
void setSaveDirOverride(const std::string& dir);
std::string savePathFor(int slot);
std::vector<SaveSlotInfo> listSaveSlots();
```

### progression · 修炼与传承

[模块设计说明](../src/core/progression/README.md)

#### `arts.h` · 35 条声明

头文件：[arts.h](../src/core/arts.h)。

对象/枚举类型：`nz::NodeDef`、`nz::ManualInfo`、`nz::BuildInfo`、`nz::CombatHooks`、`nz::LifeHooks`、`nz::ArtMods`、`nz::BrewInfo`、`nz::ForgeInfo`、`nz::ForgeMaterialCost`、`nz::PillEffect`、`nz::PillUseInfo`、`nz::CombatPill`、`nz::ActionError`、`nz::AchievementDef`。

namespace：`nz`

```cpp
const vector<NodeDef>& artNodes();
const NodeDef* findNode(const string& id);
bool findManualInfo(const string& id, ManualInfo& out);
const vector<BuildInfo>& artBuilds();
const data::RecipeDef* findRecipe(const string& id);
const data::FurnaceDef* findFurnace(const string& id);
const data::HerbDef* findHerbById(const string& id);
const data::HerbDef* findHerbByName(const string& name);
vector<std::pair<string, int>> parseIngredients(const string& s);
int artLevel(int exp);
bool artHas(const Arts& a, const string& dao, int level);
int daoIndex(const string& dao);
const ArtMods& artModsOf(const Arts& a);
int artEscapeSpeed(const Player& p);
Arts artNpcLoadout(const Npc& npc);
void ensureArts(Game& g);
void artGain(Player& p, const string& dao, double amount);
void artSyncHerbs(Player& p);
double cultivationRateOf(const Player& p);
int artTier(const Player& p);
BrewInfo artBrewInfo(const Player& p, const string& recipeId);
ForgeInfo artForgeInfo(const Player& p, const string& mainId, const string& auxId, const string& coreId, const string& shapeId);
vector<ForgeMaterialCost> artForgeMaterials(const Player& p, const string& mainId, const string& auxId, const string& coreId);
PillEffect artPillEffect(const Player& p, const data::RecipeDef& r);
PillEffect mfCompletePillEffect(const data::RecipeDef& Recipe);
PillUseInfo artPillUseInfo(const Player& p, const data::RecipeDef& r);
string artUsePill(Game& g, const string& id);
CombatPill artCombatPill(const data::RecipeDef& r);
string artQuestError(const Game& g, const data::QuestDef* q);
void artRewardQuest(Game& g, const data::QuestDef& q);
string artAnnualError(const Game& g, const string& type, const string& id);
void artAnnual(Game& g, const string& type, const string& id);
ActionError artInstant(Game& g, const string& type, const string& id);
string artEquipError(const Player& p, const string& id);
int artProgress(const Player& p, const string& key);
const vector<AchievementDef>& artAchievements();
```

#### `cultivate.h` · 23 条声明

头文件：[cultivate.h](../src/core/cultivate.h)。

对象/枚举类型：`nz::cultivate::Breakdown`。

namespace：`nz::cultivate`

```cpp
double aptMul(const Player& p);
double insightMul(const Player& p);
double manualBase(const Player& p);
double realmMul(const Player& p);
int maxSupplyLevel();
int unlockedSupplyLevel(const Player& p);
int supplyCost(const Player& p, const std::string& kind, int level);
double stoneSupplyMul(const Player& p);
double qiSupplyMul(const Player& p);
double caveMul(const Player& p);
bool atCave(const Player& p);
bool qiActive(const Player& p);
double supplyMul(const Player& p);
double baseRate(const Player& p);
double rate(const Player& p);
Breakdown breakdown(const Player& p);
double breakthroughBonus(const Player& p);
int learnDays(const Player& p, int level);
double bodyHpBonus(int body);
int bodySoftCap();
double bodyHpCap();
std::string supplyName(const std::string& kind, int level);
std::string supplyDesc(const std::string& kind, int level);
```

#### `talents.h` · 7 条声明

头文件：[talents.h](../src/core/talents.h)。

namespace：`nz`

```cpp
ArtMods talentMods(const Player& player);
double talentCultivationMul(const Player& player);
int talentManaBonus(const Player& player);
string talentConflict(const vector<string>& traits);
void talentBirth(Game& game);
void talentMonthTick(Game& game);
void talentBreakthrough(Game& game, bool major);
```

#### `legacy.h` · 9 条声明

头文件：[legacy.h](../src/core/legacy.h)。

对象/枚举类型：`nz::legacy::Archive`、`nz::legacy::Milestone`、`nz::legacy::SettleResult`。

namespace：`nz::legacy`

```cpp
Archive create();
std::string newLifeId(uint32_t seed);
int talentBudget(const Archive& a);
std::string validate(const Archive& a);
std::vector<Milestone> milestonesOf(const Game& g, const Archive& a);
SettleResult settle(const Game& g, Archive& a);
std::vector<std::string> draft(uint32_t seed, int roll);
std::string toJson(const Archive& a);
Archive fromJson(const std::string& text, bool* ok = nullptr);
```

### simulation · 时间与编排

[模块设计说明](../src/core/simulation/README.md)

#### `sim.h` · 29 条声明

头文件：[sim.h](../src/core/sim.h)。

对象/枚举类型：`nz::BreakthroughInfo`、`nz::CreateOptions`。

namespace：`nz`

```cpp
void logEvent(Game& g, const string& category, const string& text, bool important = false, const string& npcId = "", const string& delta = "");
void npcHistory(Game& g, Npc& n, const string& text);
void npcNews(Game& g, Npc& n, const string& text, bool important = false);
void npcDies(Game& g, Npc& n, const string& reason);
void applyDamage(Game& g, int amount);
void swordGain(Game& g, int amount);
void worldYear(Game& g);
void refreshDaily(Game& g);
void monthComplete(Game& g, const Job& job);
void maybeChoice(Game& g);
bool hasDan(const Player& p);
void finish(Game& g, const string& cause = "暂别仙途");
double cultivationRate(const Player& p);
double alchemyChance(const Player& p);
int cultivationCost(const Player& p);
int marketPrice(const Game& g, const string& item);
int power(const Person& p);
int socialGiftCost(const Person& p);
double breakthroughChance(const Game& g);
int breakthroughCost(const Game& g);
BreakthroughInfo breakthroughInfo(const Game& g);
Game createGame(const CreateOptions& opts = {});
vector<int> populationCounts(const Game& g);
void refreshSects(Game& g);
void worldFromSeed(Game& g);
double legacyEfficiencyOf(const Player& p);
int power(const Person& p);
int relationOf(const Game& g, const string& npcId);
void addRelation(Game& g, const string& npcId, int delta);
```

#### `calendar.h` · 7 条声明

头文件：[calendar.h](../src/core/calendar.h)。

对象/枚举类型：`nz::CalDate`、`nz::CalRunResult`。

namespace：`nz`

```cpp
CalDate calendarDate(int days);
CalDate calendarDate(const Game& g);
string calendarLabel(const Game& g);
int calendarTake(Game& g, const string& key, double amount);
double calendarDetoxPerYear(const Player& player);
CalRunResult calendarRun(Game& g, int maxDays = 30);
void ensureCalendar(Game& g);
```

#### `play.h` · 10 条声明

头文件：[play.h](../src/core/play.h)。

对象/枚举类型：`nz::play::MarketItem`、`nz::play::SocialAction`、`nz::play::SocialResult`、`nz::play::CraftOption`、`nz::play::CraftResult`。

namespace：`nz::play`

```cpp
std::vector<MarketItem> marketList(const Game& g);
std::vector<MarketItem> gearList(const Game& g);
std::string marketBuy(Game& g, const std::string& kind, const std::string& id, int count);
std::vector<const Npc*> nearbyPeople(const Game& g);
const Npc* findPerson(const Game& g, const std::string& id);
std::vector<SocialAction> socialActions(const Game& g, const std::string& npcId);
SocialResult socialDo(Game& g, const std::string& npcId, const std::string& actionId);
std::string relationLabel(int value);
std::vector<CraftOption> craftOptions(const Game& g, const std::string& kind);
CraftResult craftDo(Game& g, const std::string& kind, const std::string& id);
```

#### `tide.h` · 8 条声明

头文件：[tide.h](../src/core/tide.h)。

对象/枚举类型：`nz::tide::FightResult`。

namespace：`nz::tide`

```cpp
int dangerBonus(const Game& g);
std::string statusText(const Game& g);
void yearTick(Game& g);
void monthTick(Game& g);
FightResult fight(Game& g);
void tideReward(Game& g, bool won);
std::string hunkerDown(Game& g);
void ensureTide(Game& g);
```

#### `checkpoint.h` · 4 条声明

头文件：[checkpoint.h](../src/core/checkpoint.h)。

namespace：`nz`

```cpp
void checkpointEnable(Game& game);
bool checkpointTick(Game& game);
void checkpointManual(Game& game, int slot);
bool checkpointRestore(Game& game, const string& reason);
```

#### `reward_receipt.h` · 0 条声明

头文件：[reward_receipt.h](../src/core/reward_receipt.h)。

对象/枚举类型：`nz::RewardReceipt`。

无显式自由函数声明；主要通过对象成员或下方的头内定义调用。

#### `tianji.h` · 3 条声明

头文件：[tianji.h](../src/core/tianji.h)。

朋友维护，仅接口对接。实现保留在 `src/core/tianji.cpp`；本轮不修改天机大比或其他多人逻辑。

对象/枚举类型：`nz::tianji::Window`、`nz::tianji::Store`。

namespace：`nz::tianji`

```cpp
Window windowFor(const Game& game);
bool freezePlayer(const Player& source, Player& frozen, std::string& error);
json::Value buildView(const Player& player);
```

### social · NPC 与宗门

[模块设计说明](../src/core/social/README.md)

#### `friendship.h` · 18 条声明

头文件：[friendship.h](../src/core/friendship.h)。

对象/枚举类型：`nz::friendship::GreetResult`、`nz::friendship::GiftOutcome`、`nz::friendship::GiftResult`、`nz::friendship::BagItem`、`nz::friendship::TradeResult`。

namespace：`nz::friendship`

```cpp
int creditOf(const Game& g, const std::string& npcId);
void addCredit(Game& g, const std::string& npcId, int delta);
bool spendCredit(Game& g, const std::string& npcId, int cost);
std::string interactionError(const Game& g, const std::string& npcId);
std::string creditLabel(int credit);
std::string affinityLabel(int affinity);
bool affinityAtLeast(const Game& g, const std::string& npcId, int need);
GreetResult greet(Game& g, const std::string& npcId);
GiftOutcome giftOutcome(const Game& g, const Npc& npc, int value, const std::string& kind = "", const std::string& id = "");
int creditFromValue(int value);
GiftResult giftItem(Game& g, const std::string& npcId, const std::string& kind, const std::string& id);
GiftResult giftHerbs(Game& g, const std::string& npcId, int quantity);
std::vector<BagItem> npcBag(const Game& g, const std::string& npcId);
TradeResult takeFromNpc(Game& g, const std::string& npcId, const std::string& kind, const std::string& id);
std::string npcTryEquip(Game& g, Npc& npc, const Gear& gear);
void ensureNpcBag(Game& g, Npc& npc);
void ensureNpcCultivation(Game& g, Npc& npc);
void ensureAllNpcData(Game& g);
```

#### `npc_learning.h` · 5 条声明

头文件：[npc_learning.h](../src/core/npc_learning.h)。

对象/枚举类型：`nz::NpcLesson`。

namespace：`nz`

```cpp
vector<NpcLesson> npcLessons(const Game& g, const string& npcId);
string npcLearnBegin(Game& g, const string& npcId, const string& kind, const string& id);
void npcLearnComplete(Game& g, const Job& job);
string personPortrait(const string& gender, const string& sect, bool player = false);
string beastPortrait(int stage);
```

#### `roster.h` · 7 条声明

头文件：[roster.h](../src/core/roster.h)。

对象/枚举类型：`nz::play::RosterEntry`、`nz::play::MessageResult`、`nz::play::TalentOption`、`nz::play::BuildCheck`、`nz::play::AllocOption`、`nz::play::PoisonInfo`。

namespace：`nz::play`

```cpp
std::vector<RosterEntry> roster(const Game& g, bool includeStrangers, const std::string& sortBy);
MessageResult sendMessage(Game& g, const std::string& npcId);
MessageResult inviteToMe(Game& g, const std::string& npcId);
std::vector<TalentOption> allTalents();
BuildCheck checkBuild(const std::vector<std::string>& traits, const std::map<std::string, int>& allocation, int budget);
std::vector<AllocOption> allocOptions(const std::map<std::string, int>& allocation);
PoisonInfo poisonInfo(const Game& g);
```

#### `sect_trial.h` · 12 条声明

头文件：[sect_trial.h](../src/core/sect_trial.h)。

对象/枚举类型：`nz::SectTrialDef`、`nz::SectTaskDef`。

namespace：`nz`

```cpp
const vector<SectTrialDef>& sectTrialDefs();
const SectTrialDef* findSectTrial(const string& sect);
string sectTrialError(const Game& g, const string& sect);
string sectTrialBegin(Game& g, const string& sect);
void sectTrialBattleComplete(Game& g, const Battle& battle, bool won, bool escaped = false);
void sectTrialLegacyComplete(Game& g, const Job& job);
string sectTrialStateError(const Game& g);
vector<const Npc*> sectMentorCandidates(const Game& g);
string sectMasterError(const Game& g, const string& npcId);
const vector<SectTaskDef>& sectTaskDefs();
const SectTaskDef* findSectTask(const string& id);
int sectTasksTakenThisMonth(const Game& g);
```

#### `society.h` · 23 条声明

头文件：[society.h](../src/core/society.h)。

对象/枚举类型：`nz::BondActionInfo`、`nz::StipendResult`。

namespace：`nz`

```cpp
string socNPCName(int index);
string socRank(const Person& p);
int socStipend(const Person& p);
void socAccrue(Game& g);
void socInitPayroll(Game& g, bool legacy = false);
void ensureSociety(Game& g);
bool storyRequiresMet(const Game& g, const data::StoryDef& s);
string storyError(const Game& g, const data::StoryDef* s);
string storyChoiceError(const Game& g, const data::StoryDef& s, int optionIndex);
string storyBegin(Game& g, const data::StoryDef& s, int optionIndex);
void storyComplete(Game& g, const data::StoryDef& s, int optionIndex);
const data::WildTaskDef* findWildTask(const string& id);
string wildActionError(const Game& g, const string& id);
void societyWildBegin(Game& g, const string& id);
bool bondCanTakeDisciple(const Game& g);
string bondMonthlyError(const Game& g, const string& kind);
BondActionInfo bondActionInfo(const Game& g, const string& kind);
string bondActionError(const Game& g, const string& kind);
void societyBondBegin(Game& g, const string& kind);
void societyBondComplete(Game& g, const string& kind, const Npc* npc);
void societyWildStart(Game& g, const Job& job);
void societyWildEnd(Game& g, const Battle& b);
StipendResult socCollectStipend(Game& g);
```

#### `renown.h` · 16 条声明

头文件：[renown.h](../src/core/renown.h)。

对象/枚举类型：`nz::renown::Tier`、`nz::renown::Entry`、`nz::renown::Source`。

namespace：`nz::renown`

```cpp
const std::vector<Tier>& tiers();
const Tier& tierOf(int renown);
int toNextTier(int renown);
double tierProgress(int renown);
double buyDiscount(int renown);
double sellBonus(int renown);
int sellLimit(int renown);
int marketCashCap(int renown);
int firstImpression(int renown);
double libraryDiscount(int renown);
double stipendBonus(int renown);
int sectRequirement(const std::string& sectId);
int masterRequirement();
std::string describe(int renown);
std::vector<Source> sources(const Game& g);
std::vector<std::string> sources();
```

### transport · HTTP 与密码

[模块设计说明](../src/core/transport/README.md)

#### `http.h` · 1 条声明

头文件：[http.h](../src/core/http.h)。

朋友维护，仅接口对接；实际实现位于 `src/core/http.cpp`，不在 transport 目录。

对象/枚举类型：`nz::http::Request`、`nz::http::Response`。

namespace：`nz::http`

```cpp
int serve(const std::string& host, int port, const std::string& staticDir, Handler api, std::function<void()> tick = {}, std::function<void()> shutdown = {});
```

#### `pwhash.h` · 5 条声明

头文件：[pwhash.h](../src/core/pwhash.h)。

朋友维护，仅接口对接；实际实现位于 `src/core/pwhash.cpp`，不在 transport 目录。

namespace：`nz::pw`

```cpp
std::string sha256(const std::string& data);
std::string pbkdf2(const std::string& pass, const std::string& salt, int iter, size_t len = 32);
std::string toHex(const std::string& raw);
std::string makeRecord(const std::string& pass);
bool verifyRecord(const std::string& record, const std::string& pass);
```

## 主统计外的头内自由函数定义

共 21 条：18 条 inline 定义、3 条函数模板定义。这里只列签名，函数体见原头文件。

### `base.h`

```cpp
inline uint32_t mixSeed(uint32_t seed) { /* 头文件内定义 */ }
inline double clamp(double n, double lo, double hi) { /* 头文件内定义 */ }
inline int clampInt(int n, int lo, int hi) { /* 头文件内定义 */ }
inline int jsRound(double n) { /* 头文件内定义 */ }
inline int jsFloor(double n) { /* 头文件内定义 */ }
inline double jsSqrt(double n) { /* 头文件内定义 */ }
template <typename T> const T& pick(Rng& r, const vector<T>& items) { /* 头文件内定义 */ }
inline int randInt(Rng& r, int a, int b) { /* 头文件内定义 */ }
```

### `model.h`

```cpp
inline const std::vector<RealmDef>& REALMS() { /* 头文件内定义 */ }
inline const std::vector<SectDef>& SECTS() { /* 头文件内定义 */ }
inline const std::vector<LocationDef>& LOCATIONS() { /* 头文件内定义 */ }
inline const std::vector<PlanDef>& PLANS() { /* 头文件内定义 */ }
inline const std::vector<TalentDef>& TALENTS() { /* 头文件内定义 */ }
inline const std::vector<BuildStatDef>& BUILD_STATS() { /* 头文件内定义 */ }
inline string realmName(int stage) { /* 头文件内定义 */ }
inline int battleCooldown(const Battle& b, const string& spellId) { /* 头文件内定义 */ }
inline void battleTickCooldowns(Battle& b) { /* 头文件内定义 */ }
inline double randomOf(Game& g) { /* 头文件内定义 */ }
inline int randIntOf(Game& g, int a, int b) { /* 头文件内定义 */ }
template <typename T> const T& pickOf(Game& g, const vector<T>& items) { /* 头文件内定义 */ }
template <typename T> T* pickOf(Game& g, vector<T*>& items) { /* 头文件内定义 */ }
```

## 重要对象与成员入口（不计自由函数声明）

| 对象/类型 | 头文件 | 主要入口或用途 |
| --- | --- | --- |
| `nz::Game` / `Player` / `Npc` | `model.h` | 共享运行状态；由领域入口修改，无统一成员服务 API |
| `nz::Battle` / `EnemyCombat` / `Card` | `model.h` | 斗法状态、对手卡牌与 UID；成员辅助不计入主表 |
| `nz::Job` / `Clock` / `DungeonRun` / `AutoCheckpoint` | `model.h` | 在途工作、日历、秘境、检查点的持久状态 |
| `nz::Rng` | `base.h` | 构造 `Rng(seed)`、成员 `next()` 推进 LCG |
| `nz::json::Value`、`Type`、别名 `Array` / `Object` | `json.h` | 静态成员 `Value::parse(text, err)`；成员 `dump(indent)`、`as...`、`operator[]`、`set` / `push` |
| `nz::api::Session` | `api.h` | 保存 Game、started、lastAutomatic 与 writeError；通过 bindSession/restoreGame 使用 |
| `nz::api::RouteInfo` / `RouteId` / `RouteEffect` | `interface/routes.h` | 路由方法、效应及进行中状态策略 |
| `nz::content::DaoId` / `SectId` / `SpellKind` / `BattleKind` | `content/game_ids.h` | 类型化规则类别，stableId 与 legacy 值分离 |
| `nz::content::TextId` / `TextArg` | `content/text_table.h` | 文案目录与命名参数 |
| `nz::SpellDef` / `EnemySpec` / `TurnResult` / `Preview` | `battle.h` | 神通、开战参数、回合结果与预览；Preview 内还有 Status 类型 |
| `nz::ArtifactAbility` / `SwordDuel` | `artifact.h` / `sword.h` | 法器能力与剑斗计算结果 |
| `nz::ArtMods` / `CombatHooks` / `LifeHooks` | `arts.h` | 战斗与生活效果的查询结果 |
| `nz::BrewInfo` / `ForgeInfo` / `PillUseInfo` / `cultivate::Breakdown` | `arts.h` / `cultivate.h` | 制作、丹药与修炼数值预览 |
| `nz::shop::ActionQuote` / `SellResult` | `shop.h` | 交易资格/报价与动作结果 |
| `nz::legacy::Archive` / `SettleResult` | `legacy.h` | 跨世传承及结算；与单世 Game 分开 |
| `nz::RewardReceipt` | `reward_receipt.h` | 构造 `RewardReceipt(const Game&)` 记录动作前数据；成员 `changes(const Game&) const` 生成变化文字 |
| `nz::tianji::Store` | `tianji.h` | 构造 Store(directory)；公共成员 healthy/error/active/observe/hint/status/board/handle，管理服务端账号留影与挑战 |
| `nz::http::Request` / `Response`，别名 `Handler` | `http.h` | HTTP 输入/输出与注入回调；Handler 是函数类型，不是函数声明 |

上方各公共头小节列出了该头实际定义的顶层对象/枚举；本表挑出主要状态与调用型对象。类型前置声明、私有嵌套类型、数据成员均未计入函数数量。

## interface 内部协作声明（公共统计外）

以下 15 条显式自由函数声明用于视图实现之间共享，并不表示新增 HTTP API。调用方优先使用 `api.h` / `api_exploration.h`；视图不得推进时间、消耗 RNG、绑定会话或访问存档。

### `interface/views.h` · 14 条内部声明

namespace：`nz::api::view`。类型 `ViewContext` 定义于 views.h，用显式上下文传入账号拥有的展示事实。

```cpp
json::Value state(const Game& game, const ViewContext& context = {});
json::Value mfAchievementsView(const Game& GameState, const legacy::Archive& ArchiveState);
json::Value battle(const Game& game);
json::Value equipment(const Player& player, const Gear& gear);
json::Value spellPreview(const Player& player, const SpellDef& spell);
json::Value artifactView(const Player& player, const ArtifactAbility& ability);
json::Value artifactPassiveView(const Player& player, const Gear& gear);
json::Value actionQuote(const Game& game, const std::string& path, const shop::ActionQuote& quote);
void appendPlayerState(const Game& game, json::Value& state);
void appendArtsState(const Game& game, json::Value& state);
void appendWorldState(const Game& game, json::Value& state);
void appendSocialState(const Game& game, json::Value& state);
void appendEconomyState(const Game& game, json::Value& state);
void appendEquipmentState(const Game& game, json::Value& state);
```

### `interface/view_policy.h` · 2 条内部声明

namespace：`nz::api::view`。

```cpp
std::string actionBlockReason(const Game& game, const std::string& path);
std::string breakthroughReason(const Game& game, const BreakthroughInfo& info);
```

## 维护与核对

新增或改变公共函数时，应更新所属模块 README 和本清单。单纯新增 struct 成员或 HTTP 路由不能直接增加本统计；新增重载须逐条列出。把函数从声明改成 inline/template 头内定义时，主表和补充表的归属也需改变。

源码的实际声明是最终依据。内部 codec、视图 helper 与 cpp 局部函数可随实现演进，不应在对外统计里扩大为稳定扩展接口。

## 本版新增公开入口

2026-10-14 便利补充（不改写历史统计）：`society.h` 中 `nz::mfQuoteStipend` / `mfCollectStipend`，旧 `socCollectStipend` 保留为薄适配；`exploration/service_navigation.h` 中 `nz::Exploration::mfQuoteServiceRoute` / `mfQuoteMaterialRoute`。共 4 条新领域入口；`interface/service_views.h` 的 3 条 JSON 编码/组合函数为内部协议协作，不计为长期领域扩展 API。只读报价不累计俸禄、不买材料、不消耗活动 RNG。

- crafting/material_supply.h：materialQuote、buyMaterials、materialQuoteJson、recordForgeMaterials、salvageQuote、salvageMaterials、salvageQuoteJson（7条；namespace nz::crafting）。
- adventure.h：adventureHarvestHerbs（具体药材奖励1条）。
- npc_learning.h：npcPortrait(id,gender,sect)、npcPortrait(Npc)（稳定画像2条）。

### 2026-10-13 战斗与药材补充

以下入口不改写上方历史数量，也不新增 HTTP 路由。所有查询不推进时间、不消耗随机数、不写存档。

| 公共头 | namespace 与调用入口 | 契约 |
| --- | --- | --- |
| `combat/battle_scaling.h` | `nz::mfBattleDamageBasisFromPower(BasePower)`、`mfBattleDamageBasis(Stage)` | 返回统一伤害基数 `战力 × 0.3 + 18`，具体技能保留各自倍率与取整流程 |
| `arts.h` | `nz::mfBreakthroughPillStage(RecipeId)` | 返回已登记突破丹对应的准备阶段；非突破丹或未知 ID 返回空值 |
| `crafting/herb_acquisition.h` | `nz::crafting::mfEarliestRecipeCraftStage(RecipeId)` | 返回丹方品阶与实际炉容量都满足的最早阶段；非法目录返回 -1 |
| 同上 | `mfHerbAcquisitionStage(HerbId)` | 返回按所有丹方最早用途推导的获取阶段；未知 ID 返回 -1 |
| 同上 | `mfIsPlantHerb(HerbId)` | 统一采集与灵田的草药分类，稳定 ID 排除妖丹 |
| 同上 | `mfGatherableHerbs(Character, LocationId)` | 返回地点生境与角色阶段允许的真实采集池；未知地点或非法阶段为空 |
| `content/herb_habitats.h` | `nz::content::mfHerbHabitatIds(LocationId)` | 返回稳定地点 ID 对应的药材 ID 常量目录；未知地点为空 |

维护与实际规则见 [丹药效果](PILL_EFFECTS.md)、[药材取得](HERB_ACQUISITION.md) 和 combat/crafting 模块 README。
