// 静态数据表与数值工具自测。
#include <cmath>

#include "harness.h"

#include "core/base.h"
#include "core/model.h"
#include "data/reference_tables.h"

using namespace nz;

namespace nztest {
using namespace nz::data;

void runDataTests() {
    std::printf("\n-- 静态数据表 --\n");
    checkEq(static_cast<long long>(HerbDef_TABLE().size()), 157, "药材 157 种");
    checkEq(static_cast<long long>(RecipeDef_TABLE().size()), 126, "丹方 126 张");
    checkEq(static_cast<long long>(FurnaceDef_TABLE().size()), 7, "丹炉 7 座");
    checkEq(static_cast<long long>(ManualDef_TABLE().size()), 26, "拓展功法 26 部");
    checkEq(static_cast<long long>(SpellDef_TABLE().size()), 28, "拓展神通 28 种");
    checkEq(static_cast<long long>(GuideDef_TABLE().size()), 8, "打法攻略 8 篇");

    const auto& herbs = HerbDef_TABLE();
    check(!herbs.empty() && herbs[0].name == "银月花", "首个药材为「银月花」");
    check(!herbs.empty() && herbs[0].grade == 1, "首个药材品阶为 1");

    const auto& recipes = RecipeDef_TABLE();
    check(!recipes.empty() && recipes[0].name == "化淤丹", "首张丹方为「化淤丹」");
    check(!recipes.empty() && !recipes[0].ingredients.empty(),
          "首张丹方带有药材需求");

    std::printf("\n-- 规则表 --\n");
    checkEq(static_cast<long long>(REALMS().size()), 13, "境界 13 级");
    checkEq(static_cast<long long>(SECTS().size()), 5, "宗门 5 家");
    checkEq(static_cast<long long>(LOCATIONS().size()), 28, "宁州地点28处");
    checkEq(static_cast<long long>(PLANS().size()), 5, "月度计划 5 种");
    checkEq(static_cast<long long>(TALENTS().size()), 44, "天赋 44 项");
    checkEq(static_cast<long long>(BUILD_STATS().size()), 5, "可分配属性 5 项");

    checkStr(realmName(0), "炼气初期", "境界 0 为炼气初期");
    checkStr(realmName(12), "化神", "境界 12 为化神");
    checkStr(realmName(99), "化神", "越界时取最高境界");
    check(findSect("jinhong") != nullptr, "能查到金虹剑派");
    check(findSect("nope") == nullptr, "查不到的门派返回空");
    check(isCity("wuling"), "武陵城是城池");
    check(!isCity("fanghu"), "方壶山不是城池");
}

void runRuleTests() {
    std::printf("\n-- 数值工具 --\n");
    checkEq(jsRound(2.5), 3, "jsRound(2.5) = 3");
    checkEq(jsRound(-2.5), -2, "jsRound(-2.5) = -2（与 JS 一致）");
    checkEq(jsFloor(2.9), 2, "jsFloor(2.9) = 2");
    check(clamp(150.0, 0.0, 100.0) == 100.0, "clamp 上限生效");
    check(clamp(-5.0, 0.0, 100.0) == 0.0, "clamp 下限生效");
    check(jsSqrt(-1.0) == 0.0, "负数开方返回 0（不产生 NaN）");

    std::printf("\n-- 随机数（对照 JS 的 LCG）--\n");
    {
        // 与 JS 版逐值对照过：种子 20261004 的序列完全相同。
        // 这一项保证两版在同一存档上推进得到同样的结果。
        Game g;
        g.rng = 20261004;
        const double want[5] = {0.4373442221, 0.6273535262, 0.8642430541,
                                0.4056338370, 0.8985748214};
        bool allMatch = true;
        for (int i = 0; i < 5; ++i) {
            const double got = randomOf(g);
            if (std::fabs(got - want[i]) > 1e-9) allMatch = false;
        }
        check(allMatch, "种子 20261004 的前 5 个随机数与 JS 版一致");
    }

    std::printf("\n-- 人物生成（对照 JS 版 buildCharacter）--\n");
    {
        struct Case {
            vector<string> traits;
            map<string, int> alloc;
            int budget;
            int apt, luck, insight, sword, alchemy, body, lifeBonus, stones, herbs, spent;
        };
        // 基准资质 90 / 悟性 37（初始掷出的中值；seed=0 时固定用这个）
        const std::vector<Case> cases = {
            {{"sword"}, {{"apt", 6}}, 10, 114, 45, 37, 18, 5, 0, 0, 100, 3, 10},
            {{"root", "steady"}, {}, 10, 112, 45, 37, 2, 5, 0, 0, 70, 3, 10},
            {{"herb", "merchant"}, {{"wealth", 3}}, 10, 90, 45, 37, 2, 25, 0, 0, 160, 7, 10},
            {{"luck", "body"}, {{"luck", 1}}, 10, 90, 71, 37, 2, 5, 2, 0, 100, 3, 9},
        };
        const char* labels[4] = {
            "剑心通明 + 资质6",
            "天灵根 + 道心坚毅",
            "丹道初识+商贾世家 + 家境3",
            "福缘深厚+淬体根骨 + 福缘1",
        };
        for (size_t i = 0; i < cases.size(); ++i) {
            const Case& c = cases[i];
            Build b = buildCharacter(c.traits, c.alloc, c.budget);
            bool ok = b.apt == c.apt && b.luck == c.luck && b.insight == c.insight &&
                      b.sword == c.sword && b.alchemy == c.alchemy &&
                      b.body == c.body && b.lifeBonus == c.lifeBonus &&
                      b.stones == c.stones && b.herbs == c.herbs &&
                      b.spent == c.spent;
            std::printf("  [%s] %s\n", ok ? "通过" : "失败", labels[i]);
            if (!ok) {
                std::printf("        得到 资质%d 福缘%d 悟性%d 剑意%d 丹术%d 体魄%d 寿元%d 灵石%d 灵药%d 花费%d\n",
                            b.apt, b.luck, b.insight, b.sword, b.alchemy,
                            b.body, b.lifeBonus, b.stones, b.herbs, b.spent);
                std::printf("        期望 资质%d 福缘%d 悟性%d 剑意%d 丹术%d 体魄%d 寿元%d 灵石%d 灵药%d 花费%d\n",
                            c.apt, c.luck, c.insight, c.sword, c.alchemy,
                            c.body, c.lifeBonus, c.stones, c.herbs, c.spent);
                ++failures();
            }
        }
    }

    std::printf("\n-- 入参校验（对应 JS 的 throw）--\n");
    {
        const map<string, int> none = {};

        bool threw = false;
        try { buildCharacter({"sword", "herb", "root", "luck"}, none, 10); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "超过三项天赋会被拒绝");

        threw = false;
        try { buildCharacter({"sword", "sword"}, none, 10); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "重复天赋会被拒绝");

        threw = false;
        try { buildCharacter({"sword"}, map<string, int>{{"apt", 99}}, 10); }
        catch (const std::invalid_argument&) { threw = true; }
        check(threw, "属性点超出上限会被拒绝");

        threw = false;
        try { buildCharacter({"root"}, none, 10); }
        catch (const std::invalid_argument&) { threw = true; }
        check(!threw, "花费低于预算仍然合法");
    }
}

} // namespace nztest
