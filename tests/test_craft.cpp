// 炼丹与炼器自测：对照 JS 版 artBrewInfo / artForgeInfo。
//
// 期望值来自 tools/craft-check.mjs（JS 版实际运行结果）。
#include "harness.h"

#include "core/arts.h"
#include "core/model.h"
#include "data/reference_tables.h"

using namespace nz;
using namespace nz::data;

namespace nztest {

namespace {

Player makeCrafter(int stage = 0, int alchemy = 30,
                   const string& sect = "") {
    Player p;
    p.stage = stage;
    p.alchemy = alchemy;
    p.sect = sect;
    p.health = 100;
    p.stones = 5000;
    p.herbs = 100;
    p.apt = 60;
    p.luck = 45;
    p.insight = 20;
    p.sword = 20;
    p.build.budget = 10;
    for (int i = 0; i < 12; ++i) p.arts.exp[ART_DAOS[i]] = 0;
    p.arts.manuals["tuna"] = 1;
    p.arts.main = "tuna";
    p.arts.furnace = "furnace-1";
    p.arts.durability = 40;
    return p;
}

} // namespace

void runCraftTests() {
    std::printf("\n-- 炼丹（对照 JS 版 artBrewInfo）--\n");
    {
        const Player p = makeCrafter(0, 30);
        const BrewInfo b = artBrewInfo(p, "5001");
        check(b.valid, "化淤丹丹方有效");
        checkStr(b.recipeName, "化淤丹", "丹方名称正确");
        checkEq(b.grade, 1, "品阶为 1");
        checkEq(b.cost, 14, "开炉需 14 灵石");
        checkNear(b.chance, 0.671475, "成丹率 0.671475", 1e-6);
        checkEq(b.count, 3, "需要 3 份药材");
    }
    {
        // 丹术 80 + 离火门加成
        const Player p = makeCrafter(0, 80, "lihuo");
        const BrewInfo b = artBrewInfo(p, "5001");
        checkNear(b.chance, 0.907492, "丹术80+离火门成丹率 0.907492", 1e-6);
    }
    {
        // 不存在的丹方
        const Player p = makeCrafter();
        check(!artBrewInfo(p, "nope").valid, "未知丹方返回无效");
    }

    std::printf("\n-- 炼器（对照 JS 版 artForgeInfo）--\n");
    {
        const Player p = makeCrafter(6);
        const ForgeInfo f = artForgeInfo(p, "iron", "jade", "wood", "sword");
        check(f.valid, "锻剑铁+岩玉+桃木→剑 组合有效");
        checkEq(f.grade, 1, "品阶为 1");
        checkNear(f.util, 0.387273, "利用率 0.387273", 1e-6);
        checkEq(f.cost, 33, "炼器需 33 灵石");
        checkNear(f.bonus, 0.025, "加成 0.025", 1e-9);
        checkNear(f.chance, 0.6, "成功率 0.6", 1e-6);
    }
    {
        const Player p = makeCrafter(6);
        const ForgeInfo f = artForgeInfo(p, "gold", "crystal", "soul", "armor");
        checkEq(f.grade, 3, "庚金组合品阶 3");
        checkNear(f.util, 0.783636, "庚金组合利用率 0.783636", 1e-6);
        checkEq(f.cost, 69, "庚金组合需 69 灵石");
        checkNear(f.bonus, 0.156, "庚金组合加成 0.156", 1e-9);
    }
    {
        const Player p = makeCrafter(6);
        const ForgeInfo f = artForgeInfo(p, "dragon", "meteor", "black", "pearl");
        checkEq(f.grade, 4, "龙鳞组合品阶 4");
        checkNear(f.util, 1.0, "龙鳞组合利用率封顶 1.0", 1e-6);
        checkEq(f.cost, 87, "龙鳞组合需 87 灵石");
        checkNear(f.bonus, 0.257, "龙鳞组合加成 0.257", 1e-9);
    }
    {
        // 器道二层把利用率提高 15%
        Player p = makeCrafter(6);
        p.arts.nodes = {"11-1", "11-2"};
        const ForgeInfo f = artForgeInfo(p, "iron", "jade", "wood", "sword");
        checkNear(f.util, 0.445364, "器道二层利用率 0.445364", 1e-6);
    }
    {
        const Player p = makeCrafter(6);
        check(!artForgeInfo(p, "nope", "jade", "wood", "sword").valid,
              "未知材料返回无效");
        check(!artForgeInfo(p, "iron", "jade", "wood", "nope").valid,
              "未知器型返回无效");
    }

    std::printf("\n-- 丹药效果（对照 JS 版 artPillEffect）--\n");
    {
        const RecipeDef* r = nullptr;
        for (const auto& x : RecipeDef_TABLE())
            if (x.id == "5001") r = &x;
        check(r != nullptr, "能查到化淤丹");
        if (r) {
            const Player p = makeCrafter(3);
            const PillEffect e = artPillEffect(p, *r);
            checkStr(r->kind, "heal", "化淤丹属于治疗类");
            checkEq(e.value, 20, "化淤丹恢复 20 气血");
            checkStr(e.text, "恢复气血 20", "化淤丹效果文案正确");
        }
    }
    {
        const RecipeDef* r = nullptr;
        for (const auto& x : RecipeDef_TABLE())
            if (x.id == "5005") r = &x;
        if (r) {
            const Player p = makeCrafter(3);
            const PillEffect e = artPillEffect(p, *r);
            checkStr(r->kind, "combat", "凝神丹属于战斗类");
            check(e.text.find("开局灵力 +1") != string::npos &&
                  e.text.find("仅开局生效") != string::npos &&
                  e.text.find("容量及每轮恢复量不变") != string::npos,
                  "凝神丹文案符合真实首轮灵力收益，不虚报容量或每轮恢复量");
        }
    }

    std::printf("\n-- 品阶上限 --\n");
    {
        Player p = makeCrafter(0);
        checkEq(artTier(p), 2, "炼气初期品阶上限 2");
        p.stage = 2;
        checkEq(artTier(p), 3, "炼气后期品阶上限 3");
        p.stage = 6;
        checkEq(artTier(p), 4, "金丹初期品阶上限 4");
        p.stage = 12;
        checkEq(artTier(p), 6, "化神品阶上限 6");
    }

    std::printf("\n-- 限时机缘 --\n");
    {
        checkEq(static_cast<long long>(QuestDef_TABLE().size()), 12, "机缘 12 项");
        checkEq(static_cast<long long>(AchievementDef_TABLE().size()), 14, "成就 14 项");

        // 永安学堂：第 1–20 年、武陵城、需 20 灵石
        const QuestDef* school = nullptr;
        for (const auto& q : QuestDef_TABLE())
            if (q.id == "school") school = &q;
        check(school != nullptr, "能查到永安学堂");
        if (school) {
            checkStr(school->location, "wuling", "永安学堂在武陵城");
            checkEq(school->start, 1, "起始年份 1");
            checkEq(school->end, 20, "结束年份 20");
            checkEq(school->price, 20, "需 20 灵石");
        }

        // 前置依赖：再见星铃需要先完成星宫有铃
        const QuestDef* again = nullptr;
        for (const auto& q : QuestDef_TABLE())
            if (q.id == "again") again = &q;
        if (again) checkStr(again->requiresQuest, "star", "再见星铃前置为星宫有铃");

        // 剑壁参悟需要四次
        const QuestDef* wall = nullptr;
        for (const auto& q : QuestDef_TABLE())
            if (q.id == "swordwall") wall = &q;
        if (wall) checkEq(wall->visits, 4, "剑壁参悟需四次");
    }
}

} // namespace nztest
