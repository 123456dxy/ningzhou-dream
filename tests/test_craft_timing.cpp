#include "harness.h"

#include "core/arts.h"
#include "core/api.h"
#include "core/calendar.h"
#include "core/crafting/craft_timing.h"
#include "core/crafting/craft_prices.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/renown.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/talents.h"
#include "data/reference_tables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace nztest {
namespace {
using namespace nz;

// 用途：建立无生活加成的隔离制作夹具；流程：时间和资金回归；用法：按值取得新游戏；返回：不关联玩家存档的游戏快照。
Game mfCraftTimingGame() {
    CreateOptions Options;
    Options.seed = 4519;
    Options.traitsExplicit = true;
    Game Result = createGame(Options);
    Result.pending.reset();
    Result.job.reset();
    Result.npcs.clear();
    Result.world.market = 1.0;
    Result.player.location = "wuling";
    Result.player.stones = 10000000;
    Result.player.alchemy = 0;
    Result.player.traits.clear();
    Result.player.sect.clear();
    Result.player.renown = 0;
    Result.player.arts.main.clear();
    Result.player.arts.aux.clear();
    Result.player.arts.manuals.clear();
    Result.player.arts.nodes.clear();
    Result.player.arts.exp["器"] = 0;
    Result.player.arts.furnace = "furnace-7";
    Result.player.arts.durability = 100;
    Result.player.arts.herbs.clear();
    Result.player.herbs = 0;
    return Result;
}

// 用途：备齐真实丹方批次原料；流程：开炉与存读档；用法：传隔离游戏、丹方和炉数；返回：无，按目录材料份数写入夹具。
void mfStockCraftTimingRecipe(Game& State, const string& RecipeId, int BatchCount) {
    const auto* Recipe = findRecipe(RecipeId);
    for (const auto& [HerbName, HerbCount] : parseIngredients(Recipe->ingredients)) {
        State.player.arts.herbs[HerbName] += HerbCount * BatchCount;
        State.player.herbs += HerbCount * BatchCount;
    }
}

// 用途：从真实目录找指定品阶丹方；流程：逐阶时长测试；返回：存在的稳定丹方 ID，目录缺阶则为空。
string mfRecipeIdForTimingGrade(int Grade) {
    for (const auto& Recipe : data::RecipeDef_TABLE())
        if (Recipe.grade == Grade) return Recipe.id;
    return {};
}

// 用途：计算独立验证所需的原有预算底座；流程：境界乘区回归；返回：声望额度与可收购最贵单丹价的较高值。
int mfMarketBudgetBase(const Game& State) {
    int BaseBudget = renown::marketCashCap(State.player.renown);
    const int MarketTier = ecoMarketTier(State.player);
    for (const auto& Recipe : data::RecipeDef_TABLE())
        if (Recipe.grade <= MarketTier)
            BaseBudget = std::max(BaseBudget, shop::salePrice(State, "potion", Recipe.id));
    for (int Grade = 1; Grade <= MarketTier; ++Grade) {
        const auto Quote = crafting::mfQuoteStandardGearPrice(State, Grade);
        if (Quote.Valid) BaseBudget = std::max(BaseBudget, Quote.SellPrice);
    }
    return BaseBudget;
}

// 所属测试模块，拥有独立Session；在测试调用期间绑定，离开作用域还原默认绑定，不读写真实账号。
struct CraftTimingSessionScope {
    api::Session Session;
    CraftTimingSessionScope() { api::bindSession(&Session); }
    ~CraftTimingSessionScope() { api::bindSession(nullptr); }
};
} // namespace

// 用途：验证时长、冻结工作和月预算境界乘区；流程：核心测试；返回：无，失败写入测试计数；仅使用隔离夹具。
void mfRunCraftTimingTests() {
    std::printf("\n--- 制作时长与坊市境界乘区 ---\n");
    {
        const Player Plain = mfCraftTimingGame().player;
        const std::array<int, 6> BrewingDays{4, 5, 6, 7, 8, 9};
        const std::array<int, 5> ForgingDays{30, 60, 120, 240, 480};
        for (int Grade = 1; Grade <= 6; ++Grade)
            checkEq(ecoCraftDays(Plain, "brew", Grade), BrewingDays[Grade - 1],
                    "无加成炼丹每品阶每炉增加一天：" + std::to_string(Grade));
        for (int Grade = 1; Grade <= 5; ++Grade)
            checkEq(ecoCraftDays(Plain, "forge", Grade), ForgingDays[Grade - 1],
                    "无加成炼器一阶30天，此后逐阶翻倍：" + std::to_string(Grade));
        checkEq(crafting::mfBaseCraftDurationDays(crafting::CraftKind::Brewing, 0), 0,
                "统一时长严格拒绝零品阶");
        checkEq(crafting::mfBaseCraftDurationDays(crafting::CraftKind::Forging, 6), 0,
                "统一时长不凭空允许六阶炼器");
        check(!crafting::mfParseCraftKind("unknown"), "未知制作键不回退到炼器");
        checkEq(ecoCraftDays(Plain, "unknown", 1), 0, "兼容时长接口明确拒绝未知制作键");
        Player Boundary = Plain;
        Boundary.stage = -3;
        checkEq(mfMarketRealmMultiplier(Boundary), 1, "负境界乘区钳位到炼气");
        Boundary.stage = std::numeric_limits<int>::max();
        checkEq(mfMarketRealmMultiplier(Boundary), 16, "异常高境界乘区饱和且位移不溢出");
    }
    {
        Game Legacy = mfCraftTimingGame();
        mfStockCraftTimingRecipe(Legacy, "5001", 1);
        ecoBegin(Legacy, "brew", "5001");
        check(Legacy.job.has_value(), "旧时长合同夹具已有合法工作");
        if (Legacy.job) {
            Legacy.job->total = 100;
            Legacy.job->remaining = 73;
            Game Restored;
            string Error;
            check(loadFromJson(saveToJson(Legacy), Restored, &Error), "旧100天炼丹合同合法读回：" + Error);
            if (Restored.job) {
                checkEq(Restored.job->total, 100, "新规则不追改存档旧工作总时长");
                checkEq(Restored.job->remaining, 73, "新规则不追改存档旧工作剩余73天");
            }
        }
    }
    {
        Player Artisan = mfCraftTimingGame().player;
        Artisan.alchemy = 500;
        Artisan.arts.exp["器"] = 100000;
        Artisan.arts.nodes = {"11-3"};
        Artisan.arts.main = "forge";
        Artisan.arts.manuals = {{"forge", 6}};
        Artisan.traits = {"artisan"};
        const double SpeedBonus = talentMods(Artisan).forgeSpeed;
        check(SpeedBonus <= 0.6, "功法悟道匠心加成维持六成减时上限");
        checkEq(ecoCraftDays(Artisan, "forge", 5),
                static_cast<int>(std::ceil(480.0 * 0.7 * (1.0 - SpeedBonus))),
                "高阶炼器基础480天与原能力及生活减时依次结算");
        checkEq(ecoCraftDays(Artisan, "brew", 6), 7, "六品丹术减时最多三成，9天向上取整7天");
        const int BrewingWithArtisan = ecoCraftDays(Artisan, "brew", 4);
        Artisan.traits.clear();
        checkEq(ecoCraftDays(Artisan, "brew", 4), BrewingWithArtisan, "匠心不缩短炼丹时间");
        check(ecoCraftDays(Artisan, "forge", 1) >= 2, "一阶炼器减时保留最少两天");
    }
    for (const int BatchCount : {1, 3, 5, 10}) {
        Game State = mfCraftTimingGame();
        State.player.stage = 6;
        const string RecipeId = mfRecipeIdForTimingGrade(3);
        mfStockCraftTimingRecipe(State, RecipeId, BatchCount);
        const auto Quote = ecoActionInfo(State, "brew", RecipeId, BatchCount);
        checkEq(Quote.days, 6 * BatchCount, "三品批量开炉逐炉6天乘真实炉数");
        ecoBegin(State, "brew", RecipeId, BatchCount);
        check(State.job.has_value(), "批量炼丹建立真实在途工作");
        if (!State.job) continue;
        checkEq(State.job->total, Quote.days, "实际工作总天数与预览完全一致");
        checkEq(State.job->remaining, Quote.days, "开工冻结批量剩余天数");
        State.player.alchemy = 500;
        Game Restored;
        string Error;
        check(loadFromJson(saveToJson(State), Restored, &Error), "批量在途工作可保存读回：" + Error);
        if (!Restored.job) continue;
        checkEq(Restored.job->total, Quote.days, "读档不按开工后提高的丹术重算总天数");
        checkEq(Restored.job->remaining, Quote.days, "读档保留剩余时间合同");
        const int RemainingBefore = Restored.job->remaining;
        const auto Run = calendarRun(Restored, 1);
        check(Run.ok, "冻结炼丹工作真实推进一天");
        checkEq(Restored.job ? Restored.job->remaining : 0, RemainingBefore - 1,
                "日历只扣实际一天，不改写新公式后的在途报价");
    }
    {
        Game State = mfCraftTimingGame();
        State.player.stage = 6;
        State.player.arts.exp["器"] = 100;
        State.player.arts.materials = {{"gold", 4}, {"crystal", 3}, {"soul", 3}};
        const string ForgeId = "gold|crystal|soul|sword";
        const auto Quote = ecoActionInfo(State, "forge", ForgeId);
        check(Quote.valid, "三阶真实炼器配方可报价");
        ecoBegin(State, "forge", ForgeId);
        check(State.job.has_value(), "三阶真实炼器建立在途工作");
        if (State.job) {
            checkEq(State.job->total, Quote.days, "真实铸炼与工坊预览使用同一时长");
            State.player.arts.exp["器"] = 100000;
            State.player.traits = {"artisan"};
            Game Restored;
            string Error;
            check(loadFromJson(saveToJson(State), Restored, &Error), "炼器在途工作可存读档：" + Error);
            if (Restored.job) checkEq(Restored.job->total, Quote.days, "读档不因新器道/天赋重算已投入的铸造时间");
        }
        State.job.reset();
        State.player.stones = 10000000;
        const auto Commission = ecoActionInfo(State, "commission", "forge");
        check(Commission.valid, "当前境界有代铸委托报价");
        checkEq(Commission.days, ecoCraftDays(State.player, "forge", 3), "代铸与自行炼器使用同一品阶时长");
        const auto BrewingCommission = ecoActionInfo(State, "commission", "brew");
        check(BrewingCommission.valid, "当前境界有代炼委托报价");
        if (const auto* Recipe = findRecipe(BrewingCommission.orderId))
            checkEq(BrewingCommission.days, ecoCraftDays(State.player, "brew", Recipe->grade),
                    "代炼与自行炼丹使用同一品阶时长");
    }
    for (const int Fame : {0, 250}) {
        for (int MajorRealm = 0; MajorRealm <= 4; ++MajorRealm) {
            Game State = mfCraftTimingGame();
            State.player.stage = MajorRealm * 3;
            State.player.renown = Fame;
            const int RealmMultiplier = 1 << MajorRealm;
            const int Budget = mfMarketBudgetBase(State) * 3 * RealmMultiplier;
            checkEq(ecoMarketBudget(State), Budget, "完整基础预算先翻三倍再按大境界乘1/2/4/8/16");
            State.economy.month = -1;
            ensureEconomy(State);
            checkEq(State.economy.marketCash, Budget, "初始化资金按统一境界预算补齐");
            State.economy.marketCash = 3;
            State.player.stage = 12;
            State.player.renown = 0;
            ecoRefresh(State);
            checkEq(State.economy.marketCash, 3, "同月晋阶或声望改变不重复注资");
            const int CurrentBudget = ecoMarketBudget(State);
            checkEq(State.economy.marketCash, 3, "只读新预算不会修改本月余额");
            Game Restored;
            string Error;
            check(loadFromJson(saveToJson(State), Restored, &Error), "预算资金可存读档：" + Error);
            checkEq(Restored.economy.marketCash, 3, "读档不重新发放当月预算");
            Restored.clock.days += 30;
            ecoRefresh(Restored);
            checkEq(Restored.economy.marketCash, CurrentBudget, "下一月按当前境界和声望补充资金");
            ecoRefresh(Restored);
            checkEq(Restored.economy.marketCash, CurrentBudget, "重复换月刷新不会叠加额度");
        }
    }
    {
        Game TripleBudget = mfCraftTimingGame();
        TripleBudget.player.renown = 250;
        checkEq(ecoMarketBudget(TripleBudget), 7200, "高声望炼气2400原底座基础翻三倍为7200");
        TripleBudget.player.stage = 3;
        checkEq(ecoMarketBudget(TripleBudget), mfMarketBudgetBase(TripleBudget) * 6,
                "筑基总预算叠加基础3倍和境界2倍为原底座6倍");
        TripleBudget.player.stage = 12;
        TripleBudget.world.market = 30000.0;
        const long long ExtremeRawBudget = static_cast<long long>(mfMarketBudgetBase(TripleBudget)) * 48;
        check(ExtremeRawBudget > std::numeric_limits<int>::max(), "异常极端行情夹具覆盖32位乘法溢出区间");
        checkEq(ecoMarketBudget(TripleBudget), 1000000000, "64位计算异常极端预算后饱和到十亿，不溢出负值");
        TripleBudget.player.renown = std::numeric_limits<int>::max();
        checkEq(ecoMarketBudget(TripleBudget), 1000000000, "异常大声望先钳位封顶，不发生声望预算乘法溢出");
    }
    {
        CraftTimingSessionScope Scope;
        Scope.Session.g = mfCraftTimingGame();
        Scope.Session.started = true;
        auto& State = Scope.Session.g;
        State.player.stage = 6;
        State.player.arts.exp["器"] = 100;
        State.player.arts.materials = {{"gold", 4}, {"crystal", 3}, {"soul", 3}};
        const string BeforeView = saveToJson(State);
        const auto ViewReply = api::handle({"POST", "/api/state", "{}", ""});
        checkEq(ViewReply.status, 200, "真实状态API返回制作时长与预算");
        const auto View = json::Value::parse(ViewReply.body)["state"];
        checkEq(View["marketRealmMultiplier"].asInt(), 4, "真实状态API公布金丹预算倍率4");
        checkEq(View["marketBudget"].asInt(), ecoMarketBudget(State), "API预算与换月使用同一报价");
        checkStr(View["marketBudgetRule"].asString(), mfMarketBudgetRule(), "API规则说明与权威预算文字一致");
        bool FoundRecipe = false;
        for (const auto& Entry : View["brewOptions"].asArray()) if (Entry["id"].asString() == "5001") {
            FoundRecipe = true;
            checkEq(Entry["days"].asInt(), 4, "真实状态API一品每炉展示4天");
        }
        check(FoundRecipe, "真实状态API含已有一品丹方");
        const auto ForgeReply = api::handle({"POST", "/api/forge-preview",
            "{\"main\":\"gold\",\"aux\":\"crystal\",\"core\":\"soul\",\"shape\":\"sword\"}", ""});
        checkEq(ForgeReply.status, 200, "真实炼器预览API接受合法三料");
        const auto ForgeView = json::Value::parse(ForgeReply.body)["info"];
        checkEq(ForgeView["days"].asInt(), ecoCraftDays(State.player, "forge", 3),
                "真实炼器预览API与工作时长一致");
        check(saveToJson(State) == BeforeView, "状态和炼器预览不充值、不改时钟/RNG/工作");

        mfStockCraftTimingRecipe(State, "5001", 3);
        const int StartDay = State.clock.days;
        const auto CraftReply = api::handle({"POST", "/api/craft", "{\"kind\":\"brew\",\"id\":\"5001\",\"batch\":3}", ""});
        checkEq(CraftReply.status, 200, "真实炼丹API完成一品三炉");
        checkEq(State.clock.days - StartDay, 12, "真实炼丹API实际消耗每炉4天共12天");
        check(!State.job, "三炉时间结算完成不遗留假工作");
    }
}
} // namespace nztest
