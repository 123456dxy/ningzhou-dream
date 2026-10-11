// 存档读写自测：JSON 往返、字段保真、文件读写。
#include "harness.h"

#include "core/arts.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

using namespace nz;

namespace nztest {

namespace {

// 造一个内容尽量丰富的存档，覆盖各模块字段
Game makeRichGame() {
    CreateOptions opts;
    opts.name = "沈知微";
    opts.gender = "女";
    opts.seed = 20261004;
    opts.traits = {"sword", "luck"};
    // 天赋 4+5=9 点，属性再给 1 点，正好用满基础预算 10 点
    opts.allocation = {{"apt", 1}};
    Game g = createGame(opts);

    // 推进几下，让世界里也有内容
    g.clock.days = 360 * 3 + 30 * 5 + 12;
    g.year = 3;
    g.player.age = 19;
    g.player.stones = 1234;
    g.player.herbs = 42;
    g.player.sword = 77;
    g.player.renown = 9;
    g.player.stage = 3;
    g.player.highestStage = 3;
    g.player.sect = "jinhong";
    g.player.location = "jinhong";

    // 洞府
    Arts& a = g.player.arts;
    a.points = 7;
    a.nodes = {"8-1", "6-1", "7-1"};
    a.exp["剑"] = 145;
    a.manuals = {{"tuna", 3}, {"sword", 2}};
    a.main = "sword";
    a.aux = {"tuna"};
    a.spells = {"sword", "rain", "water", "wood"};
    a.herbs = {{"银月花", 5}, {"流莹草", 3}};
    a.potions = {{"5001", 2}};
    a.materials = {{"iron", 6}, {"jade", 3}};
    a.poison = 12;
    a.heart = 8;
    a.mind = 4;
    a.progress.brew = 2;
    a.progress.forge = 1;
    a.progress.wins = 5;
    a.progress.wealth = 900;
    a.quests = {{"school", 2}};
    a.furnace = "furnace-3";
    a.durability = 22;
    a.aptPills = 3;

    // 法器 + 装备
    Gear gear;
    gear.id = "gear-100-0";
    gear.name = "遗府1品灵剑";
    gear.slot = "weapon";
    gear.element = "剑";
    gear.polarity = "天阳";
    gear.grade = 1;
    gear.bonus = 0.035;
    gear.inscribed = true;
    gear.craftCost = 35;
    gear.dimensions = {300, 280, 320, 290, 310};
    a.gear.push_back(gear);
    a.equipped["weapon"] = "gear-100-0";

    // 营生
    g.economy.contribution = 45;
    g.economy.taskCounts = {{"errand", 2}};
    g.economy.huntCounts = 3;
    g.economy.month = 41;
    g.economy.marketCash = 650;
    g.economy.books = {"tuna", "bull", "sword"};

    // 人情
    g.bonds.payroll.sect = "jinhong";
    g.bonds.payroll.balance = 96;
    g.bonds.payroll.credited = 9;
    g.bonds.lessons = 4;
    g.bonds.notoriety = 5;
    g.bonds.stories = {{"letter", 2}};

    // 游历
    g.adventure.since = 18;
    g.adventure.total = 143;
    g.adventure.seq = 6;
    g.adventure.events = 6;
    g.adventure.wins = 4;
    g.adventure.lastKind = "bandit";
    g.adventure.backhillMonth = 40;

    // NPC 与纪事已经在 createGame 时生成
    return g;
}

} // namespace

void runSaveTests() {
    std::printf("\n-- 存档读写 --\n");

    Game original = makeRichGame();
    const std::string text = saveToJson(original);
    check(!text.empty(), "能序列化出存档文本");
    check(text.find("ningzhou-dream-cpp") != std::string::npos,
          "存档带格式标识");
    check(text.find("沈知微") != std::string::npos, "中文按 UTF-8 原样写入");

    Game loaded;
    std::string err;
    const bool ok = loadFromJson(text, loaded, &err);
    check(ok, "能读回存档" + (ok ? std::string() : ("：" + err)));

    if (ok) {
        // ---- 顶层字段 ----
        checkEq(loaded.version, original.version, "版本一致");
        checkEq(loaded.rng, original.rng, "随机数状态一致");
        checkEq(loaded.seed, original.seed, "种子一致");
        checkEq(loaded.year, original.year, "年份一致");
        checkEq(loaded.eventSeq, original.eventSeq, "事件序号一致");
        checkEq(loaded.npcSeq, original.npcSeq, "NPC 序号一致");
        checkStr(loaded.plan, original.plan, "当前计划一致");

        // ---- 玩家 ----
        checkStr(loaded.player.name, original.player.name, "角色名一致");
        checkStr(loaded.player.gender, original.player.gender, "性别一致");
        checkEq(loaded.player.age, original.player.age, "年龄一致");
        checkEq(loaded.player.stage, original.player.stage, "境界一致");
        checkEq(loaded.player.stones, original.player.stones, "灵石一致");
        checkEq(loaded.player.herbs, original.player.herbs, "灵药一致");
        checkEq(loaded.player.sword, original.player.sword, "剑意一致");
        checkEq(loaded.player.renown, original.player.renown, "声望一致");
        checkStr(loaded.player.sect, original.player.sect, "宗门一致");
        checkStr(loaded.player.location, original.player.location, "地点一致");
        checkEq(static_cast<long long>(loaded.player.traits.size()),
                static_cast<long long>(original.player.traits.size()),
                "天赋数量一致");

        // ---- 洞府 ----
        const Arts& la = loaded.player.arts;
        const Arts& oa = original.player.arts;
        checkEq(la.points, oa.points, "悟道点一致");
        checkEq(static_cast<long long>(la.nodes.size()),
                static_cast<long long>(oa.nodes.size()), "道法节点数量一致");
        checkEq(la.exp.at("剑"), oa.exp.at("剑"), "剑道经验一致");
        checkEq(la.manuals.at("sword"), oa.manuals.at("sword"), "功法层数一致");
        checkStr(la.main, oa.main, "主修功法一致");
        checkEq(static_cast<long long>(la.spells.size()),
                static_cast<long long>(oa.spells.size()), "携带神通数量一致");
        checkEq(la.herbs.at("银月花"), oa.herbs.at("银月花"), "分类药材一致");
        checkEq(la.potions.at("5001"), oa.potions.at("5001"), "丹药数量一致");
        checkEq(la.materials.at("iron"), oa.materials.at("iron"), "炼器材料一致");
        checkEq(la.poison, oa.poison, "丹毒一致");
        checkEq(la.heart, oa.heart, "心境一致");
        checkEq(la.progress.brew, oa.progress.brew, "炼丹次数一致");
        checkEq(la.progress.wins, oa.progress.wins, "斗法胜场一致");
        checkEq(la.progress.wealth, oa.progress.wealth, "财富记录一致");
        checkStr(la.furnace, oa.furnace, "丹炉一致");
        checkEq(la.durability, oa.durability, "丹炉耐久一致");
        checkEq(la.aptPills, oa.aptPills, "资质丹额度一致");

        // 法器
        checkEq(static_cast<long long>(la.gear.size()),
                static_cast<long long>(oa.gear.size()), "法器数量一致");
        if (!la.gear.empty() && !oa.gear.empty()) {
            checkStr(la.gear[0].id, oa.gear[0].id, "法器 id 一致");
            checkStr(la.gear[0].name, oa.gear[0].name, "法器名一致");
            checkStr(la.gear[0].slot, oa.gear[0].slot, "法器槽位一致");
            checkNear(la.gear[0].bonus, oa.gear[0].bonus, "法器加成一致",
                      1e-9);
            check(la.gear[0].inscribed == oa.gear[0].inscribed,
                  "铭刻状态一致");
            checkEq(static_cast<long long>(la.gear[0].dimensions.size()), 5,
                    "五维数量一致");
        }
        checkEq(static_cast<long long>(la.equipped.size()),
                static_cast<long long>(oa.equipped.size()), "装备槽数量一致");
        checkStr(la.equipped.at("weapon"), oa.equipped.at("weapon"),
                 "武器槽一致");

        // ---- 世界 ----
        checkEq(loaded.world.herbs, original.world.herbs, "世界灵药一致");
        checkEq(loaded.world.coreStock, original.world.coreStock, "灵核一致");
        checkEq(loaded.world.pillStock, original.world.pillStock, "丹药库存一致");
        checkStr(loaded.world.mineOwner, original.world.mineOwner,
                 "灵脉归属一致");
        checkEq(static_cast<long long>(loaded.world.sects.size()),
                static_cast<long long>(original.world.sects.size()),
                "宗门数量一致");
        checkEq(loaded.world.sects.at("jinhong").treasury,
                original.world.sects.at("jinhong").treasury, "宗门府库一致");

        // ---- 月历 ----
        checkEq(loaded.clock.days, original.clock.days, "日历天数一致");
        checkEq(loaded.clock.ageBase, original.clock.ageBase, "年龄基准一致");

        // ---- 营生 ----
        checkEq(loaded.economy.contribution, original.economy.contribution,
                "宗门贡献一致");
        checkEq(loaded.economy.huntCounts, original.economy.huntCounts,
                "猎妖次数一致");
        checkEq(loaded.economy.month, original.economy.month, "刷新月份一致");
        checkEq(loaded.economy.marketCash, original.economy.marketCash,
                "坊市资金一致");
        checkEq(static_cast<long long>(loaded.economy.books.size()),
                static_cast<long long>(original.economy.books.size()),
                "功法抄本数量一致");

        // ---- 人情 ----
        checkEq(loaded.bonds.payroll.balance, original.bonds.payroll.balance,
                "待领俸禄一致");
        checkEq(loaded.bonds.lessons, original.bonds.lessons, "授业次数一致");
        checkEq(loaded.bonds.notoriety, original.bonds.notoriety, "恶名一致");
        checkEq(static_cast<long long>(loaded.bonds.stories.size()),
                static_cast<long long>(original.bonds.stories.size()),
                "已完成支线数量一致");

        // ---- 游历 ----
        checkEq(loaded.adventure.since, original.adventure.since,
                "游历累计天数一致");
        checkEq(loaded.adventure.total, original.adventure.total,
                "游历总天数一致");
        checkEq(loaded.adventure.seq, original.adventure.seq, "遇事序号一致");
        checkEq(loaded.adventure.wins, original.adventure.wins,
                "游历胜场一致");
        checkStr(loaded.adventure.lastKind, original.adventure.lastKind,
                 "上次遇事类型一致");
        checkEq(loaded.adventure.backhillMonth,
                original.adventure.backhillMonth, "后山探访月份一致");

        // ---- NPC 与纪事 ----
        checkEq(static_cast<long long>(loaded.npcs.size()),
                static_cast<long long>(original.npcs.size()), "NPC 数量一致");
        checkEq(static_cast<long long>(loaded.logs.size()),
                static_cast<long long>(original.logs.size()), "纪事条数一致");
        if (!loaded.npcs.empty() && !original.npcs.empty()) {
            checkStr(loaded.npcs[0].id, original.npcs[0].id, "首个 NPC id 一致");
            checkStr(loaded.npcs[0].name, original.npcs[0].name,
                     "首个 NPC 名字一致");
            check(loaded.npcs[0].alive == original.npcs[0].alive,
                  "首个 NPC 存活状态一致");
        }
        if (!loaded.logs.empty() && !original.logs.empty()) {
            checkStr(loaded.logs.back().text, original.logs.back().text,
                     "最后一条纪事文本一致");
        }

        // ---- 二次往返应完全稳定 ----
        const std::string text2 = saveToJson(loaded);
        checkStr(text2, text, "再次序列化与首次完全一致（往返稳定）");
    }

    std::printf("\n-- 存档异常处理 --\n");
    {
        Game g;
        std::string e;
        check(!loadFromJson("", g, &e), "空文本被拒绝");
        check(!e.empty(), "空文本给出错误原因");
    }
    {
        Game g;
        std::string e;
        check(!loadFromJson("{\"format\":\"其它游戏\"}", g, &e),
              "格式不匹配被拒绝");
        check(e.find("格式") != std::string::npos, "给出格式不匹配提示");
    }
    {
        Game g;
        std::string e;
        check(!loadFromJson("{不是合法 JSON", g, &e), "非法 JSON 被拒绝");
        check(!e.empty(), "非法 JSON 给出错误原因");
    }

    std::printf("\n-- JSON 基础读写 --\n");
    {
        json::Value o(json::Object{});
        o.set("名字", json::Value(std::string("沈知微")));
        o.set("年龄", json::Value(19));
        o.set("活着", json::Value(true));
        o.set("空值", json::Value());
        json::Array arr;
        arr.push_back(json::Value(1));
        arr.push_back(json::Value(2));
        o.set("数列", json::Value(std::move(arr)));

        const std::string dumped = o.dump();
        std::string err;
        json::Value back = json::Value::parse(dumped, &err);
        check(err.empty(), "自产 JSON 能被自己解析");
        checkStr(back["名字"].asString(), "沈知微", "中文字符串往返一致");
        checkEq(back["年龄"].asInt(), 19, "整数往返一致");
        check(back["活着"].asBool(), "布尔往返一致");
        check(back["空值"].isNull(), "null 往返一致");
        checkEq(static_cast<long long>(back["数列"].asArray().size()), 2,
                "数组往返一致");
    }
    {
        // 浮点数与负数
        json::Value o(json::Object{});
        o.set("小", json::Value(0.825));
        o.set("负", json::Value(-42));
        const std::string dumped = o.dump();
        json::Value back = json::Value::parse(dumped);
        checkNear(back["小"].asNumber(), 0.825, "小数往返一致", 1e-9);
        checkEq(back["负"].asInt(), -42, "负数往返一致");
    }
    {
        // 字符串转义
        json::Value o(json::Object{});
        o.set("引号", json::Value(std::string("他说\"你好\"")));
        o.set("换行", json::Value(std::string("第一行\n第二行")));
        const std::string dumped = o.dump();
        json::Value back = json::Value::parse(dumped);
        checkStr(back["引号"].asString(), "他说\"你好\"", "引号转义往返一致");
        checkStr(back["换行"].asString(), "第一行\n第二行", "换行转义往返一致");
    }
}

} // namespace nztest
