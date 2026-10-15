#include "core/interface/routes.h"
#include "core/roster.h"
#include "core/interface/views.h"
#include "core/content/green_bottle_content.h"
#include "core/interface/view_helpers.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/play.h"
#include "core/shop.h"
#include "core/interface/view_policy.h"
#include "core/interface/craft_price_views.h"
#include "data/reference_tables.h"
#include <algorithm>

namespace nz::api::view {

namespace {
// 用途：由稳定物品定义查询品阶；流程：坊市分类；用法：传目录条目类型和ID；返回：真实品阶，未知条目返回0，不从展示名称推断。
int marketGrade(const play::MarketItem& item) {
    static const auto grades = [] {
        std::map<std::pair<string, string>, int> result;
        for (const auto& recipe : data::RecipeDef_TABLE()) result[{"potion", recipe.id}] = recipe.grade;
        for (const auto& herb : data::HerbDef_TABLE()) result[{"herb", herb.id}] = herb.grade;
        for (const auto& material : data::MaterialDef_TABLE()) result[{"material", material.id}] = material.grade;
        for (const auto& shape : data::ShapeDef_TABLE())
            for (int grade = 1; grade <= 6; ++grade) result[{"gear", shape.id + "-" + std::to_string(grade)}] = grade;
        return result;
    }();
    const auto found = grades.find({item.kind, item.id});
    return found == grades.end() ? 0 : found->second;
}
}

// 用途：叠加路由状态阻塞并编码领域动作报价。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, const std::string& path, const shop::ActionQuote& quote，按本模块入口调用。
// 返回值：可直接供接口序列化的JSON值。
Value actionQuote(const Game& game, const std::string& path, const shop::ActionQuote& quote) {
    Value out(json::Object{});
    const string blocked = actionBlockReason(game, path);
    const string reason = blocked.empty() ? quote.reason : blocked;
    out.set("enabled", Value(reason.empty() && quote.enabled)); out.set("reason", str(reason));
    out.set("costStones", num(quote.costStones)); out.set("returnStones", num(quote.returnStones));
    return out;
}

// 用途：追加交易制作目录服药效果和物品词表。
// 参与流程：HTTP请求分派、前端状态或动作预览。
// 使用方式：传入const Game& game, Value& state，按本模块入口调用。
// 返回值：无；完成说明所述写入或展示操作。
void appendEconomyState(const Game& game, Value& state) {
    mfAppendEconomyState(game, state, shop::mfMarketSaleContext(game));
}

void mfAppendEconomyState(const Game& GameState, Value& State, const shop::MarketSaleContext& MarketContext) {

    // Full current-player catalog, including unowned and high-grade pills.
    // enabled/reason describe using one pill, not buying or brewing it.
    {
        json::Array catalog;
        const string blocked = actionBlockReason(GameState, routePath(RouteId::UsePill));
        for (const auto& recipe : data::RecipeDef_TABLE()) {
            const PillUseInfo info = artPillUseInfo(GameState.player, recipe);
            Value item(json::Object{});
            item.set("id", str(recipe.id)); item.set("name", str(recipe.name));
            item.set("pricing", mfCraftPriceView(crafting::mfQuotePotionPrice(GameState, recipe.id), GameState, "potion", recipe.id, MarketContext));
            item.set("kind", str(recipe.kind)); item.set("grade", num(recipe.grade)); item.set("dao", str(recipe.dao));
            item.set("owned", num(info.owned)); item.set("effect", str(info.effect));
            item.set("effectValue", num(info.effectValue)); item.set("baseEffect", str(info.baseEffect.text));
            const auto CompleteEffect = mfCompletePillEffect(recipe);
            item.set("catalogEffect", str(CompleteEffect.text));
            if (recipe.kind == "xp") item.set("catalogEffectValue", num(CompleteEffect.value));
            item.set("usage", str(info.usage)); item.set("used", num(info.used));
            item.set("toleranceLimit", num(info.toleranceLimit)); item.set("toleranceRemaining", num(info.toleranceRemaining));
            item.set("toleranceUnlimited", Value(info.toleranceUnlimited)); item.set("overTier", Value(info.overTier));
            item.set("basePoison", num(recipe.poison)); item.set("poison", num(info.poison));
            item.set("poisonAfter", num(info.poisonAfter)); item.set("poisonDelta", num(info.poisonDelta));
            item.set("requiredStage", num(info.requiredStage));
            item.set("requiredRealm", str(info.requiredStage >= 0 ? realmName(info.requiredStage) : ""));
            const string reason = blocked.empty() ? info.reason : blocked;
            item.set("enabled", Value(reason.empty())); item.set("reason", str(reason));
            const auto SaleQuote = shop::mfQuoteMaxPotionSale(GameState, recipe.id, MarketContext);
            const string SaleBlocked = actionBlockReason(GameState, routePath(RouteId::Sell));
            Value MaxSale(json::Object{});
            MaxSale.set("enabled", Value(SaleBlocked.empty() && SaleQuote.Enabled));
            MaxSale.set("count", num(SaleQuote.Count));
            MaxSale.set("unitPrice", num(SaleQuote.UnitPrice));
            MaxSale.set("totalGain", num(SaleQuote.TotalGain));
            MaxSale.set("reason", str(SaleBlocked.empty() ? SaleQuote.Reason : SaleBlocked));
            MaxSale.set("limitReason", str(SaleQuote.LimitReason));
            item.set("maxSale", std::move(MaxSale));
            catalog.push_back(std::move(item));
        }
        State.set("pillCatalog", Value(std::move(catalog)));
    }

    // 全量物品词表供野外行囊筛选；不表示当前位置可购买。
    {
        json::Array herbs, materials;
        for (const auto& herb : data::HerbDef_TABLE()) {
            Value item(json::Object{}); item.set("id",str(herb.id)); item.set("name",str(herb.name)); item.set("grade",num(herb.grade));
            item.set("desc",str(std::to_string(herb.grade)+"品草药 · 主"+herb.main+" · 辅"+herb.secondary)); herbs.push_back(std::move(item));
        }
        for (const auto& material : data::MaterialDef_TABLE()) {
            Value item(json::Object{}); item.set("id",str(material.id)); item.set("name",str(material.name)); item.set("grade",num(material.grade));
            item.set("desc",str(std::to_string(material.grade)+"阶材料 · "+material.element+" · "+material.polarity)); materials.push_back(std::move(item));
        }
        for (const auto& Definition : content::mfGreenBottleMaterials()) {
            const auto& Material = Definition.Material;
            Value Item(json::Object{}); Item.set("id", Material.id); Item.set("name", Material.name); Item.set("grade", Material.grade);
            Item.set("desc", std::to_string(Material.grade) + "阶专属秘境灵材 · 可恢复绿瓶灵性或作炼器配料");
            materials.push_back(std::move(Item));
        }
        State.set("herbCatalog",Value(std::move(herbs))); State.set("materialCatalog",Value(std::move(materials)));
    }

    // ---- 坊市（只在城池有货）----
    {
        json::Array arr;
        for (const auto& m : play::marketList(GameState)) {
            Value entry(json::Object{});
            entry.set("kind", str(m.kind));
            entry.set("id", str(m.id));
            entry.set("name", str(m.name));
            entry.set("desc", str(m.desc));
            entry.set("price", num(m.price));
            entry.set("sellPrice", num(m.sellPrice));
            entry.set("owned", num(m.owned));
            entry.set("grade", num(marketGrade(m)));
            if (m.kind == "potion") {
                entry.set("pricing", mfCraftPriceView(crafting::mfQuotePotionPrice(GameState, m.id), GameState, "potion", m.id, MarketContext));
                if (const auto* recipe = findRecipe(m.id)) entry.set("catalogEffect", str(mfCompletePillEffect(*recipe).text));
            }
            arr.push_back(std::move(entry));
        }
        for (const auto& m : play::gearList(GameState)) {
            Value entry(json::Object{});
            entry.set("pricing", mfCraftPriceView(crafting::mfQuoteStandardGearPrice(GameState, marketGrade(m)), GameState, "gear", m.id, MarketContext));
            entry.set("kind", str(m.kind));
            entry.set("id", str(m.id));
            entry.set("name", str(m.name));
            entry.set("desc", str(m.desc));
            entry.set("price", num(m.price));
            entry.set("sellPrice", num(m.sellPrice));
            entry.set("owned", num(m.owned));
            entry.set("grade", num(marketGrade(m)));
            arr.push_back(std::move(entry));
        }
        State.set("market", Value(std::move(arr)));
        State.set("inCity", Value(ecoIsCity(GameState.player.location)));
        const bool AtHome = !GameState.player.cave.empty() && GameState.player.cave == GameState.player.location;
        State.set("workshopAvailable", Value(ecoIsCity(GameState.player.location) || AtHome));
    }

    // ---- 炼丹选项（带成丹率和缺料原因）----
    {
        json::Array arr;
        for (const auto& c : play::craftOptions(GameState, "brew")) {
            Value entry(json::Object{});
            entry.set("pricing", mfCraftPriceView(crafting::mfQuotePotionPrice(GameState, c.id), GameState, "potion", c.id, MarketContext));
            entry.set("id", str(c.id));
            entry.set("name", str(c.name));
            entry.set("desc", str(c.desc));
            entry.set("chance", num(c.chance));
            entry.set("output", num(c.output));
            // 数量和收益仅由后端报价；期望收入不承诺成功或即时售出。
            const int salePrice = shop::salePrice(GameState, "potion", c.id);
            entry.set("sellPrice", num(salePrice));
            entry.set("expectedOutput", num(c.chance * c.output));
            entry.set("expectedSaleIncome", num(c.chance * c.output * salePrice));
            entry.set("stoneCost", num(c.stoneCost));
            entry.set("days", num(c.days));
            entry.set("enabled", Value(c.enabled));
            entry.set("reason", str(c.reason));
            if (const auto* recipe = findRecipe(c.id)) {
                const PillUseInfo pill = artPillUseInfo(GameState.player, *recipe);
                entry.set("grade", num(recipe->grade)); entry.set("baseEffect", str(pill.baseEffect.text));
                entry.set("catalogEffect", str(mfCompletePillEffect(*recipe).text));
                entry.set("effect", str(pill.effect)); entry.set("usage", str(pill.usage));
                entry.set("poison", num(pill.poison)); entry.set("poisonAfter", num(pill.poisonAfter));
                const auto* furnace = findFurnace(GameState.player.arts.furnace);
                entry.set("durabilityCost", num(artHas(GameState.player.arts, "丹", 4) && furnace && furnace->grade > recipe->grade ? 0 : 1));
                json::Array ingredients;
                for (const auto& [name, required] : parseIngredients(recipe->ingredients)) {
                    const auto* herb = findHerbByName(name); if (!herb) continue;
                    const auto owned = GameState.player.arts.herbs.find(name);
                    Value material(json::Object{}); material.set("id", str(herb->id)); material.set("name", str(name));
                    material.set("required", num(required)); material.set("owned", num(owned == GameState.player.arts.herbs.end() ? 0 : owned->second));
                    ingredients.push_back(std::move(material));
                }
                entry.set("ingredients", Value(std::move(ingredients)));
            }
            arr.push_back(std::move(entry));
        }
        State.set("brewOptions", Value(std::move(arr)));
    }

    // ---- 丹毒明细（让玩家看得见）----
    {
        const play::PoisonInfo pi = play::poisonInfo(GameState);
        Value entry(json::Object{});
        entry.set("current", num(pi.current));
        entry.set("threshold", num(pi.threshold));
        entry.set("cultPenalty", num(pi.cultPenalty));
        entry.set("detoxPerYear", num(pi.detoxPerYear));
        entry.set("daysToClear", num(pi.daysToClear));
        entry.set("affecting", Value(pi.affecting));
        entry.set("summary", str(pi.summary));
        State.set("poison", std::move(entry));
    }
    State.set("marketCash", num(GameState.economy.marketCash));
    State.set("marketBudget", num(MarketContext.NextMonthBudget));
    State.set("marketRefreshCycleDays", num(MarketContext.RefreshCycleDays));
    State.set("marketDaysUntilRefresh", num(MarketContext.DaysUntilRefresh));
    State.set("marketSaleCountUnlimited", true);
    State.set("marketTier", num(ecoMarketTier(GameState.player)));
    State.set("marketRealmMultiplier", num(mfMarketRealmMultiplier(GameState.player)));
    State.set("marketBudgetRule", str(mfMarketBudgetRule()));
    State.set("coreBuyPrice", num(shop::corePrice(GameState, true)));
    State.set("pillBuyPrice", num(shop::pillPrice(GameState, true)));
    {
        json::Array arr;
        for (const auto* f : shop::buyableFurnaces(GameState)) {
            Value entry(json::Object{});
            entry.set("id", str(f->id));
            entry.set("name", str(f->name));
            entry.set("grade", num(f->grade));
            entry.set("capacity", num(f->capacity));
            entry.set("price", num(f->price > 0 ? f->price : 100 * f->grade * f->grade));
            entry.set("current", Value(GameState.player.arts.furnace == f->id));
            arr.push_back(std::move(entry));
        }
        State.set("furnaces", Value(std::move(arr)));
        State.set("furnaceRepair", actionQuote(GameState, routePath(RouteId::FurnaceRepair), shop::furnaceRepairQuote(GameState)));
    }
}

} // namespace nz::api::view
