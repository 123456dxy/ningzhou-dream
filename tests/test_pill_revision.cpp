#include "harness.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/friendship.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/social/npc_consumables.h"

#include <array>

namespace nztest {
namespace {
nz::Game mfRevisionGame() {
    nz::CreateOptions Options; Options.seed = 20261016; Options.traitsExplicit = true;
    auto State = nz::createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear(); State.logs.clear();
    auto& Player = State.player;
    Player.stage = Player.highestStage = 12; Player.age = 16; Player.lifespan = 2000;
    Player.health = 1; Player.lifeBonus = Player.PillLifeBonus = 0;
    Player.traits.clear(); Player.talent.clear(); Player.body = 0;
    Player.arts = nz::Arts{};
    Player.arts.main = "tuna"; Player.arts.manuals["tuna"] = 1;
    return State;
}

nz::Npc mfRevisionNpc(int RemainingYears) {
    nz::Npc Person; Person.id = "pill-revision-npc"; Person.name = "临寿同道";
    Person.stage = 12; Person.age = 2000 - RemainingYears; Person.lifespan = 2000;
    Person.dao = "水"; Person.health = 100;
    return Person;
}
}

void mfRunPillRevisionTests() {
    using namespace nz;
    std::printf("\n--- 丹药与续命正式三改 ---\n");
    for (const auto& [Id, Amount] : std::array<std::pair<const char*, int>, 5>{{
            {"5001",40},{"5101",200},{"5201",800},{"5301",2000},{"5501",0}}}) {
        auto State = mfRevisionGame(); State.player.arts.potions[Id] = 1;
        const auto Before = saveToJson(State);
        const auto Quote = artPillUseInfo(State.player, *findRecipe(Id));
        checkStr(saveToJson(State), Before, "绝对气血服丹报价纯查询");
        check(artUsePill(State, Id).empty(), "疗伤丹真实服用成功");
        const int Expected = Amount ? Amount + 1 : mfPlayerHealthMaximum(State.player);
        checkEq(State.player.health, Expected, "五档疗伤恢复真实点数或回满");
        checkNear(Quote.effectValue, Expected - 1, "疗伤预览与实际恢复一致");
    }
    {
        auto State = mfRevisionGame(); const int Before = mfPlayerHealthMaximum(State.player);
        for (const auto* Id : {"5209", "5306", "5417", "5515"}) {
            State.player.arts.potions[Id] = 5;
            for (int Dose = 0; Dose < 5; ++Dose) {
                const auto Quote = artPillUseInfo(State.player, *findRecipe(Id));
                const int Previous = mfPlayerHealthMaximum(State.player);
                check(artUsePill(State, Id).empty(), "四味体魄丹耐药内均可实服");
                checkNear(Quote.effectValue, mfPlayerHealthMaximum(State.player) - Previous, "体魄丹预览是当前真实上限增量");
            }
        }
        checkEq(State.player.arts.BaseHealthBonus, 350, "四味体魄丹吃满累计350基础值");
        checkEq(State.player.arts.hpBonus, 0, "新体魄丹不再写旧倍率字段");
        checkEq(mfPlayerHealthMaximum(State.player) - Before, 840, "350基础值经过2.4乘区成为840气血");
    }
    {
        auto State = mfRevisionGame(); State.player.lifeBonus = 15; State.player.lifespan = 2015;
        for (const auto* Id : {"5119", "5211", "5308", "5404", "5517"}) {
            const auto* Recipe = findRecipe(Id); State.player.arts.potions[Id] = Recipe->tolerance;
            for (int Dose = 0; Dose < Recipe->tolerance; ++Dose) {
                State.player.arts.poison = 0;
                check(artUsePill(State, Id).empty(), "寿元丹耐药内按明确额度结算");
            }
        }
        checkEq(State.player.PillLifeBonus, 500, "丹药独立寿元累计精确封顶500");
        checkEq(State.player.lifeBonus, 515, "长寿天赋不占用丹药500年额度");
        checkEq(State.player.lifespan, 2515, "真实寿元包含境界天赋与500年丹药");
    }
    {
        auto State = mfRevisionGame(); State.player.apt = 200; State.player.insight = 200;
        State.player.arts.aptPills = 10; State.player.arts.potions = {{"5416",1},{"5513",1}};
        check(artUsePill(State, "5416").empty() && State.player.apt > 200, "资质丹超过旧本世10点与总属性200仍有收益");
        check(artUsePill(State, "5513").empty() && State.player.insight > 200, "悟性丹超过旧总属性200仍有收益");
        State.player.xp = 0; State.player.arts.potions["5002"] = 1;
        check(artUsePill(State, "5002").empty() && State.player.xp > 0, "化神零修为需求不再令修为丹成为死值");
    }
    {
        auto State = mfRevisionGame(); State.player.stage = State.player.highestStage = 2;
        State.player.arts.potions["5205"] = 2;
        const auto& Recipe = *findRecipe("5205");
        const auto Catalog = artPillEffect(State.player, Recipe);
        const auto Quote = artPillUseInfo(State.player, Recipe);
        check(Catalog.text.find("1份筑基丹准备") != string::npos && Catalog.text.find("6%") == string::npos,
              "筑基丹目录描述初始资源准备，不展示旧概率加成");
        check(Quote.effect.find("初始随机2气") != string::npos && Quote.effect.find("真气+15") != string::npos &&
            Quote.usage.find("6份上限") != string::npos, "筑基丹预览与用法说明两种正式玩法及准备上限");
        checkNear(Quote.effectValue, 1, "未准备筑基丹时预览提供一份准备");
        check(artUsePill(State, "5205").empty(), "筑基丹真实服用设置对应境界准备");
        checkNear(artPillUseInfo(State.player, Recipe).effectValue, 0, "相同筑基丹准备不重复计算份数");
    }
    {
        auto Person = mfRevisionNpc(49); Person.bag = {{"potion:5119",1},{"potion:5207",8}};
        for (const auto& [Years, Urgency] : std::array<std::pair<int, NpcLifePillUrgency>, 4>{{
                {49,NpcLifePillUrgency::Regular},{24,NpcLifePillUrgency::Priority},
                {9,NpcLifePillUrgency::Urgent},{4,NpcLifePillUrgency::Critical}}})
            check(mfNpcLifePillUrgency(mfRevisionNpc(Years)) == Urgency, "续命49/24/9/4年四档明确区分");
        const int Taken = npcConsumeUsefulPills(Person);
        check(Taken > 3, "NPC日常服丹取消固定三枚上限");
        check(Person.lifespan > 2000, "49年档把续命纳入常规候选");
        auto Urgent = mfRevisionNpc(9); Urgent.health = 10;
        Urgent.bag = {{"potion:5119",1},{"potion:5001",10},{"potion:5207",5}};
        npcConsumeUsefulPills(Urgent);
        checkEq(Urgent.pillState.used["5119"], 1, "剩9年且有伤时先续命");
        auto Threshold = mfRevisionNpc(100); Threshold.pillState.poison = 39;
        Threshold.bag = {{"potion:5002",10}}; Threshold.xp = 0;
        checkEq(npcConsumeUsefulPills(Threshold), 1, "39丹毒允许一次1毒收益后达到40停手");
        checkEq(Threshold.pillState.poison, 40, "NPC日常丹毒不得越过40硬边界");
        Threshold.pillState.poison = 80; Threshold.bag = {{"potion:5006",1},{"potion:5008",1},{"potion:5009",1}};
        checkEq(npcConsumeUsefulPills(Threshold, true), 3, "80丹毒仍能战前服满三枚有效战斗丹");
        checkEq(Threshold.pillState.poison, 92, "战前超载仍真实累计三枚丹毒");
    }
    {
        auto State = mfRevisionGame(); State.player.arts.poison = 90;
        State.player.arts.potions = {{"5006",4}};
        for (int Dose = 0; Dose < 3; ++Dose)
            check(artUsePill(State, "5006").empty(), "高丹毒三枚内同类覆盖仍真实扣药");
        const auto Before = saveToJson(State);
        check(!artUsePill(State, "5006").empty(), "第四枚战斗丹即使同类覆盖也拒绝");
        checkStr(saveToJson(State), Before, "第四枚战斗准备拒绝保持全状态");
    }
    {
        auto State = mfRevisionGame(); auto Person = mfRevisionNpc(4);
        Person.stones = 100000000; Person.pillState.poison = 80;
        const int StonesBefore = Person.stones;
        checkEq(mfAcquireNpcSurvivalPills(State, Person), 2, "高丹毒临寿NPC可真实采购化毒与续命");
        check(Person.stones < StonesBefore, "自救采购确实支付正常报价");
        check(npcConsumeUsefulPills(Person) > 0 && Person.pillState.poison < 80, "无化毒库存的NPC采购后不再永久卡死");
        auto Poor = mfRevisionNpc(4); Poor.stones = 0; Poor.pillState.poison = 80;
        checkEq(mfAcquireNpcSurvivalPills(State, Poor), 0, "无钱采购明确不成交");
        check(Poor.bag.empty() && Poor.stones == 0, "采购拒绝不生成免费丹药或部分扣费");
    }
    {
        auto State = mfRevisionGame(); auto Person = mfRevisionNpc(4);
        Person.location = State.player.location; Person.pillState.poison = 80;
        State.npcs = {Person};
        const auto Before = saveToJson(State);
        check(friendship::npcBag(State, Person.id).empty(), "空NPC行囊查询只展示现有库存");
        (void)friendship::npcBag(State, Person.id);
        checkStr(saveToJson(State), Before, "连续行囊查询不改rawState时钟RNG库存或服丹状态");
        check(friendship::greet(State, Person.id).ok, "真实见礼动作可初始化旧NPC行囊");
        check(!State.npcs.front().bag.empty(), "初始背包生成落在真实动作边界");
    }
    {
        auto State = mfRevisionGame(); auto Person = mfRevisionNpc(100);
        Person.location = State.player.location; Person.bag["potion:5501"] = 1;
        State.npcs = {Person};
        const auto Items = friendship::npcBag(State, Person.id);
        check(Items.size() == 1 && Items.front().value > 0 && Items.front().creditCost > 1,
              "回满药效0哨兵不能变成零价值或一交情采购");
        State.world.market = 2.0; State.player.traits = {"merchant"}; State.player.renown = 1000;
        const auto ChangedMarket = friendship::npcBag(State, Person.id);
        check(Items.size() == 1 && ChangedMarket.size() == 1 && Items.front().value == ChangedMarket.front().value,
              "丹药礼物价值不随行情玩家折扣变化，避免往返套利");
    }
    {
        auto State = mfRevisionGame(); int RegularLifeCount = 0, UrgentLifeCount = 0;
        for (int Seed = 0; Seed < 128; ++Seed) {
            auto Regular = mfRevisionNpc(80); Regular.stage = 0; Regular.lifespan = 100; Regular.age = 20;
            auto Urgent = Regular; Urgent.age = 96;
            State.rng = static_cast<std::uint32_t>(Seed * 7919 + 17);
            friendship::ensureNpcBag(State, Regular);
            State.rng = static_cast<std::uint32_t>(Seed * 7919 + 17);
            friendship::ensureNpcBag(State, Urgent);
            for (const auto& [Key, Count] : Regular.bag)
                if (Key == "potion:5119") RegularLifeCount += Count;
            for (const auto& [Key, Count] : Urgent.bag)
                if (Key == "potion:5119") UrgentLifeCount += Count;
        }
        check(UrgentLifeCount > RegularLifeCount * 2 && UrgentLifeCount > 0,
              "炼气临寿NPC可得二品续命丹，固定种子初始背包实际提高出现概率");
    }
    {
        auto State = mfRevisionGame(); auto Person = mfRevisionNpc(100);
        Person.location = State.player.location;
        Person.bag = {{"potion:5519",2},{"5519",1},{"potion:5420",3},
            {"potion:5318",2},{"potion:5319",2},{"herb:诱妖草",3},{"herb:霸王花",2},
            {"herb:6323",1},{"6324",1},{"herb:幻心草",5}};
        State.npcs = {Person};
        auto Document = json::Value::parse(saveToJson(State));
        Document.set("healthRulesVersion", 1); Document.asObject()["player"].set("health",100);
        Game Loaded; string Error;
        check(loadDetachedFromJson(Document.dump(), Loaded, &Error), "旧NPC带前缀及裸ID退休库存可迁移：" + Error);
        if (!Loaded.npcs.empty()) {
            const auto& Bag = Loaded.npcs.front().bag;
            check(Bag.size() == 2 && Bag.at("potion:5420") == 6 && Bag.at("herb:幻心草") == 5,
                  "NPC退休化毒丹合并到可用前缀，探索物资清退且其它库存保持");
        }
        auto Negative = Document;
        Negative.asObject()["npcs"].asArray().front().asObject()["bag"].set("potion:5519",-1);
        const auto Before = saveToJson(Loaded);
        check(!loadDetachedFromJson(Negative.dump(), Loaded, &Error), "原始负数退休库存先拒绝，不能迁移清洗");
        checkStr(saveToJson(Loaded), Before, "非法退休库存拒绝不覆盖当前角色");
        auto Overflow = Document;
        Overflow.asObject()["npcs"].asArray().front().asObject()["bag"].set("potion:5420",1000000000);
        check(!loadDetachedFromJson(Overflow.dump(), Loaded, &Error), "退休库存合并超存储上限整档拒绝");
        checkStr(saveToJson(Loaded), Before, "合并溢出拒绝不部分迁移当前角色");
    }
    for (const auto* Id : {"5318", "5319", "5519"}) {
        auto State = mfRevisionGame(); State.player.stones = 100; State.player.herbs = 0;
        State.player.arts.herbs.clear(); State.player.arts.furnace = "furnace-5";
        State.player.arts.durability = 37;
        Job Contract; Contract.actionType = Contract.kind = "brew"; Contract.destination = Id;
        Contract.name = "旧在途炼丹"; Contract.total = 120; Contract.remaining = 60;
        Contract.cost = 50; Contract.gift = Contract.batch = 3; Contract.chance = 0.5;
        State.job = Contract;
        auto Document = json::Value::parse(saveToJson(State));
        Document.set("healthRulesVersion",1); Document.asObject()["player"].set("health",100);
        Game Loaded; string Error;
        check(loadDetachedFromJson(Document.dump(), Loaded, &Error), "旧退休丹方在途合同可迁移：" + string(Id) + Error);
        if (string(Id) == "5519") {
            check(Loaded.job && Loaded.job->destination == "5420" && Loaded.job->remaining == 60 &&
                Loaded.job->cost == 50 && Loaded.job->chance == 0.5 && Loaded.job->batch == 3,
                "退休化毒制作映射新丹，保留已付成本时间概率和批次");
        } else {
            check(!Loaded.job, "已删除探索丹方取消在途制作");
            checkEq(Loaded.player.stones, 486, "取消返50已付灵石及三份退休主药112折价");
            checkEq(Loaded.player.herbs, 9, "取消返还三炉仍有效的九份实际药材");
            checkEq(Loaded.player.arts.herbs["幻心草"], 6, "取消返还原配方六份幻心草");
            checkEq(Loaded.player.arts.herbs[string(Id) == "5318" ? "混元果" : "鸡冠草"], 3, "取消返还原配方药引");
            checkEq(Loaded.player.arts.durability, 40, "取消恢复开工扣除的三点炉耐久");
            const auto Once = saveToJson(Loaded); Game Again;
            check(loadDetachedFromJson(Once, Again, &Error), "取消补偿后新档可再次载入");
            checkEq(Again.player.stones, 486, "再次载入不重复补偿");
        }
        auto Bad = Document; Bad.asObject()["job"].set("gift",-3);
        const auto Before = saveToJson(Loaded);
        check(!loadDetachedFromJson(Bad.dump(), Loaded, &Error), "退休制作原始负炉数必须拒绝");
        checkStr(saveToJson(Loaded), Before, "坏合同拒绝不覆盖已加载角色");
        Bad = Document; Bad.asObject()["job"].set("paid", false);
        check(!loadDetachedFromJson(Bad.dump(), Loaded, &Error), "未付款退休合同不能领取退款或映射为已付合同");
        checkStr(saveToJson(Loaded), Before, "未付款合同拒绝不改已有角色");
        Bad = Document; Bad.asObject()["job"].set("batch",2);
        check(!loadDetachedFromJson(Bad.dump(), Loaded, &Error), "退休合同未知批次数先拒绝");
        for (const auto* Field : {"gift", "batch"}) {
            Bad = Document; Bad.asObject()["player"].asObject().erase("build");
            Bad.asObject()["job"].set(Field, Field == string("gift") ? 0 : 2);
            check(!loadDetachedFromJson(Bad.dump(), Loaded, &Error), "缺build旧合同也先拒绝显式非法原始炉数");
            checkStr(saveToJson(Loaded), Before, "旧解码默认修复不得洗白退休合同坏数量");
        }
        if (string(Id) != "5519") {
            Bad = Document; Bad.asObject()["player"].set("stones",1000000000);
            check(!loadDetachedFromJson(Bad.dump(), Loaded, &Error), "退款超储存上限整档拒绝");
            checkStr(saveToJson(Loaded), Before, "退款溢出不部分发布补偿");
        }
    }
}
} // namespace nztest
