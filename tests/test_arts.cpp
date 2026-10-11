// 洞府加成自测：对照 JS 版 artMods 的实际输出。
//
// 期望值来自 tools/artmods-check.mjs（JS 版实际运行结果），
// 用来确认 C++ 版的加成计算与 JS 版一致。
#include "harness.h"

#include "core/arts.h"
#include "core/model.h"

using namespace nz;

namespace nztest {

namespace {

// 造一个「JS ensureArts 之后的初始洞府」
Arts freshArts() {
    Arts a;
    a.focus = "气";
    for (int i = 0; i < 12; ++i) a.exp[ART_DAOS[i]] = 0;
    a.manuals["tuna"] = 1;
    a.main = "tuna";
    return a;
}

} // namespace

void runArtModsTests() {
    std::printf("\n-- 洞府加成（对照 JS 版 artMods）--\n");

    const Arts base = freshArts();

    // 初始：吐纳功 1 层 -> 修炼 +2%
    {
        const ArtMods& m = artModsOf(base);
        checkNear(m.cult, 1.02, "初始修炼加成 1.02");
        checkNear(m.attack, 1.0, "初始伤害加成 1.0");
        checkEq(m.mana, 8, "初始灵力上限 8");
    }

    // 节点：气道 1/2/3 层 + 体道 1/3 层 + 剑道 1 层
    {
        Arts b = base;
        b.nodes = {"8-1", "8-2", "8-3", "6-1", "6-3", "7-1"};
        const ArtMods& m = artModsOf(b);
        checkNear(m.cult, 1.12, "气道三层+体道后修炼 1.12");
        checkNear(m.hp, 1.35, "体道一/三层后气血 1.35");
    }

    // 功法：主修蛮牛护体功 3 层，辅修吐纳功 5 层
    {
        Arts b = base;
        b.nodes = {"8-1", "8-2", "8-3", "6-1", "6-3", "7-1"};
        b.manuals = {{"tuna", 5}, {"bull", 3}};
        b.main = "bull";
        b.aux = {"tuna"};
        const ArtMods& m = artModsOf(b);
        checkNear(m.cult, 1.1, "主修蛮牛+辅修吐纳后修炼 1.1");
        checkNear(m.hp, 1.5, "蛮牛护体功 3 层后气血 1.5");
    }

    // 法器：天阳金系剑，bonus 0.15，铭刻 +12%
    {
        Arts b = base;
        b.nodes = {"8-1", "8-2", "8-3", "6-1", "6-3", "7-1"};
        b.manuals = {{"tuna", 5}, {"bull", 3}};
        b.main = "bull";
        b.aux = {"tuna"};
        Gear gear;
        gear.id = "g1"; gear.name = "试剑"; gear.slot = "weapon";
        gear.element = "金"; gear.polarity = "天阳";
        gear.grade = 2; gear.bonus = 0.15; gear.inscribed = true; gear.price = 100;
        b.gear.push_back(gear);
        b.equipped["weapon"] = "g1";
        const ArtMods& m = artModsOf(b);
        checkNear(m.attack, 1.183, "带天阳法器后伤害 1.183");
    }

    // 丹毒 60 会削弱修炼
    {
        Arts b = base;
        b.nodes = {"8-1", "8-2", "8-3", "6-1", "6-3", "7-1"};
        b.manuals = {{"tuna", 5}, {"bull", 3}};
        b.main = "bull";
        b.aux = {"tuna"};
        b.poison = 60;
        const ArtMods& m = artModsOf(b);
        checkNear(m.cult, 0.825, "丹毒 60 时修炼降到 0.825");
    }

    // artHas 的节点判定
    {
        Arts b;
        b.nodes = {"0-1", "3-2"};
        check(artHas(b, "金", 1), "金道一层已点亮");
        check(artHas(b, "火", 2), "火道二层已点亮");
        check(!artHas(b, "金", 2), "金道二层未点亮");
        check(!artHas(b, "水", 1), "水道一层未点亮");
    }

    // artGain：经验与悟道点的对应关系
    {
        Player p;
        p.arts = freshArts();
        artGain(p, "剑", 35);   // 过 30 升一层 -> +1
        checkEq(p.arts.exp["剑"], 35, "获得 35 点剑道经验");
        checkEq(p.arts.points, 1, "升到初窥门径得 1 点悟道点");
        artGain(p, "剑", 80);   // 累计 115，只跨过 100 这一层 -> 再 +1
        checkEq(p.arts.exp["剑"], 115, "累计 115 点剑道经验");
        checkEq(p.arts.points, 2, "累计 115 时共 2 点悟道点（与 JS 一致）");
    }

    // 辅修不提供修炼效率加成（JS: 辅修跳过 cult/brew/forge）
    {
        Arts mainOnly = freshArts();
        mainOnly.manuals = {{"tuna", 5}, {"bull", 3}};
        mainOnly.main = "bull";
        const double cultMainOnly = artModsOf(mainOnly).cult;

        Arts withAux = mainOnly;
        withAux.aux = {"tuna"};
        const double cultWithAux = artModsOf(withAux).cult;
        checkNear(cultWithAux, cultMainOnly,
                  "加辅修不改变修炼效率（与 JS 一致）");
    }

    // 道法等级换算
    checkEq(artLevel(0), 0, "0 经验为未入门");
    checkEq(artLevel(30), 1, "30 经验为初窥门径");
    checkEq(artLevel(100), 2, "100 经验为略有小成");
    checkEq(artLevel(300), 3, "300 经验为融会贯通");
    checkEq(artLevel(900), 4, "900 经验为道之真境");
    checkEq(artLevel(2000), 5, "2000 经验为大道已成");
    checkEq(artLevel(9999), 5, "超过上限仍是大道已成");

    // 上限压制
    {
        Arts b = freshArts();
        // 堆很多伤害加成，检查 2.0 上限
        b.manuals = {{"tuna", 1}, {"fire", 200}};
        b.main = "fire";
        const ArtMods& m = artModsOf(b);
        check(m.attack <= 2.0 + 1e-9, "伤害加成不超过 2.0 上限");
        check(m.hp <= 2.5 + 1e-9, "气血加成不超过 2.5 上限");
        check(m.mana <= 30, "灵力上限不超过 30");
        check(m.sword <= 1.8 + 1e-9, "剑系加成不超过 1.8 上限");
    }
}

} // namespace nztest
