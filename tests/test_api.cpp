#include "harness.h"

#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/legacy.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"

namespace nztest {
namespace {
nz::http::Response request(const std::string& route, const nz::json::Value& body = nz::json::Object{},
                           const std::string& method = "POST") {
    return nz::api::handle({method, "/api/" + route, body.dump(), ""});
}
nz::json::Value input(const char* key, const std::string& value) {
    nz::json::Value out(nz::json::Object{});
    out.set(key, value);
    return out;
}
void unchangedReject(const std::string& route, const nz::json::Value& body, const std::string& label) {
    const std::string before = nz::saveToJson(nz::api::game());
    check(request(route, body).status >= 400, label + "被拒绝");
    check(nz::saveToJson(nz::api::game()) == before, label + "不改资源、时间和任务");
}
}

void runApiTests() {
    using namespace nz;
    std::printf("\n--- Web 接口流程回归 ---\n");
    const auto cold = json::Value::parse(request("state").body);
    check(!cold["hasGame"].asBool(), "冷启动明确没有角色");
    unchangedReject("travel", input("to", "fanghu"), "未建号无法赶路");
    unchangedReject("save", {}, "未建号无法保存空角色");
    check(request("build-options").status == 200, "冷启动可以获取建号选项");

    CreateOptions opts;
    opts.name = "接口回归";
    Game imported = createGame(opts);
    const std::string saved = saveToJson(imported);
    check(request("import", input("game", saved)).status == 200, "冷启动可以导入有效存档");
    check(json::Value::parse(request("state").body)["hasGame"].asBool(), "导入后刷新仍识别为已开局");
    checkStr(api::game().player.name, opts.name, "导入保留角色名");
    bool archiveOk = true;
    legacy::fromJson("not JSON", &archiveOk);
    check(!archiveOk, "损坏传承档案不能被当作空白有效档");
    legacy::Archive archive = legacy::create();
    archive.claimed = {"回归\"旧世\\记录"};
    const auto archiveBack = legacy::fromJson(legacy::toJson(archive), &archiveOk);
    check(archiveOk && archiveBack.claimed == archive.claimed, "传承记录正确往返转义字符");
    legacy::fromJson("{\"version\":1,\"earned\":10000000000000000000,\"runs\":0,\"claimed\":[],\"milestones\":[]}", &archiveOk);
    check(!archiveOk, "传承档案拒绝整数溢出");
    json::Value badPack(json::Object{});
    badPack.set("game", saved);
    badPack.set("legacy", "broken archive");
    unchangedReject("import", badPack, "传承损坏时整包导入");

    const std::string beforeMalformed = saveToJson(api::game());
    check(api::handle({"POST", "/api/new", "{broken", ""}).status == 400, "损坏 JSON 不会默认重新开局");
    check(api::handle({"POST", "/api/new", "[]", ""}).status == 400, "请求数组不能当作默认建号参数");
    checkStr(saveToJson(api::game()), beforeMalformed, "非法请求保留当前角色");
    check(request("travel", input("to", "fanghu"), "GET").status == 405, "GET 不会执行赶路扣费");

    unchangedReject("plan", input("plan", "missing"), "未知计划");
    json::Value huge(json::Object{});
    huge.set("days", 1e30);
    unchangedReject("advance", huge, "推进天数溢出");
    huge.set("days", -1);
    unchangedReject("advance", huge, "负数推进");
    json::Value fractional(json::Object{});
    fractional.set("count", 1.5);
    unchangedReject("buy", fractional, "小数购买数量");

    Game& g = api::game();
    g.pending.reset();
    g.job.reset();
    Pending pending;
    pending.type = "sectInvitation";
    pending.options.push_back({"free", "暂不入宗", "", false});
    g.pending = pending;
    unchangedReject("travel", input("to", "fanghu"), "抉择期间不能替换任务");
    unchangedReject("arts/focus", input("dao", "剑"), "抉择期间不能调整道法");
    unchangedReject("choose", input("id", "fake"), "伪造抉择选项");
    check(request("export").status == 200, "待处理抉择仍可导出存档");
    check(request("choose", input("id", "free")).status == 200 && !g.pending,
          "合法抉择正常结算");

    EnemySpec enemy;
    enemy.name = "试招石人";
    battleStart(g, enemy);
    unchangedReject("use-pill", input("id", "5001"), "斗法期间不能服用场外丹药");
    unchangedReject("travel", input("to", "fanghu"), "斗法期间不能赶路逃课");
    unchangedReject("arts/spell", input("id", "sword"), "斗法期间不能换牌组");
    check(request("export").status == 200, "斗法中可以导出完整存档");
    check(request("battle/turn", input("id", "escape")).status == 200, "斗法操作仍可执行");

    g = imported;
    g.pending.reset();
    Job job;
    job.actionType = "plan";
    job.plan = "rest";
    job.name = "休养";
    job.total = 10;
    job.remaining = 10;
    job.started = g.clock.days;
    g.job = job;
    unchangedReject("travel", input("to", "fanghu"), "已有工作不能被赶路覆盖");
    const int dayBefore = g.clock.days;
    json::Value oneDay(json::Object{});
    oneDay.set("days", 1);
    oneDay.set("autoStop", false);
    check(request("advance", oneDay).status == 200, "可以继续已有工作一天");
    checkEq(g.clock.days - dayBefore, 1, "过一天严格只过一天");
    check(g.job && g.job->remaining == 9, "短推进保留剩余工作");
    check(request("cancel-job").status == 200 && !g.job, "未完成安排可以正常中止");

    g = imported;
    g.pending.reset();
    g.job.reset();
    g.plan = "cultivate";
    g.player.stoneSupply = g.player.qiSupply = 0;
    const int stoneBefore = g.player.stones;
    const int annualCost = cultivationCost(g.player);
    for (int i = 0; i < 60; ++i) request("advance", oneDay);
    checkEq(g.clock.days, 60, "连续点击一天可正常跨月");
    checkEq(stoneBefore - g.player.stones, jsFloor(annualCost / 6.0 + 1e-9),
            "逐天闭关按累计费用收费，不能拆单免年费");
    check(stoneBefore > g.player.stones, "短闭关累计足够时确实扣费");

    g.player.arts.points = 20;
    g.player.arts.exp["剑"] = 1000;
    unchangedReject("arts/node", input("node", "8-1fake"), "节点编号必须来自真实表项");
    g.economy.books.clear();
    g.player.arts.bookList.clear();
    g.player.arts.manuals.erase("bull");
    unchangedReject("arts/learn", input("id", "bull"), "修习功法需要真实抄本");

    g = imported;
    g.pending.reset();
    g.job.reset();
    const std::string npc = g.npcs.front().id;
    g.npcs.front().location = g.player.location;
    const int socialDay = g.clock.days;
    json::Value social(json::Object{});
    social.set("npc", npc);
    social.set("action", "chat");
    check(request("social", social).status == 200, "当面闲谈正常执行");
    checkEq(g.clock.days - socialDay, 1, "闲谈经过真实日历一天");

    g.player.health = 100;
    g.npcs.front().stage = g.player.stage;
    g.npcs.front().dao = "剑"; g.npcs.front().sect.clear();
    g.npcs.front().gear.clear(); g.npcs.front().equipped.clear();
    g.npcs.front().spells = {"sword"};
    social.set("action", "spar");
    const int sparDay = g.clock.days;
    check(request("social", social).status == 200 && g.player.arts.battleActive(), "切磋建立真实NPC斗法");
    checkEq(g.clock.days - sparDay, 0, "切磋未分胜负前不偷推进日历");
    check(g.job && g.job->actionType == "socialElapsed" && g.job->remaining == 3,
          "切磋约定三天耗时保存为斗法后的任务");
    unchangedReject("advance", {}, "切磋进行中不能绕过斗法直接推进");
    if (g.player.arts.battle) {
        Battle& spar = *g.player.arts.battle;
        spar.enemyHp = 1; spar.mana = 5;
        spar.hand.clear(); spar.draw.clear(); spar.discard.clear(); spar.exhausted.clear();
        bool selected = false;
        for (const auto& card : spar.deck) {
            if (!selected && card.spell == "strike") { spar.hand.push_back(card.uid); selected = true; }
            else spar.draw.push_back(card.uid);
        }
        check(selected && request("battle/turn", input("id", "strike")).status == 200 &&
              !g.player.arts.battle, "通过真实出牌API完成切磋胜利");
        checkEq(g.clock.days - sparDay, 3, "切磋结束后实际消耗约定三天");
        check(!g.job, "切磋结束耗时完成后不残留虚假任务");
    }
    Job elapsed;
    elapsed.actionType = "socialElapsed";
    elapsed.name = "道友往来";
    elapsed.total = 5;
    elapsed.remaining = 2;
    elapsed.started = g.clock.days;
    g.job = elapsed;
    unchangedReject("cancel-job", {}, "往来收益结算后不能取消剩余耗时");
    g.job.reset();

    g.player.apt = APT_MAX;
    g.player.arts.potions["5208"] = 1;
    unchangedReject("use-pill", input("id", "5208"), "资质已满不会白扣资质丹");

    finish(g, "回归结局");
    unchangedReject("travel", input("to", "fanghu"), "结束人生后不能继续赶路");
    unchangedReject("plan", input("plan", "work"), "结束人生后不能切计划");
    check(request("export").status == 200, "结局档可以导出");

    json::Value newGame(json::Object{});
    newGame.set("name", "无符号种子");
    newGame.set("traits", json::Array{});
    newGame.set("seed", 4294967295.0);
    check(request("new", newGame).status == 200, "支持 32 位无符号种子上界");
    checkEq(g.seed, 4294967295LL, "建号与先天气运使用同一无符号种子");
    json::Value draft(json::Object{});
    draft.set("seed", 4294967295.0);
    draft.set("roll", 2);
    const auto drawn = json::Value::parse(request("legacy/draft", draft).body);
    const auto expected = legacy::draft(4294967295u, 2);
    checkEq(drawn["talents"].asArray().size(), expected.size(), "无符号种子抽出完整天赋候选");
    checkStr(drawn["talents"][size_t(0)]["id"].asString(), expected.front(), "抽天赋沿用真实无符号种子");
    draft.set("roll", 2147483647);
    unchangedReject("legacy/draft", draft, "重抽次数超限");
    const auto state = json::Value::parse(request("state").body)["state"];
    check(!state["plans"][size_t(0)]["preview"].asString().empty(), "计划带投入收益预览");
    check(state["logs"][size_t(0)]["age"].isNumber(), "纪事下发发生时年龄");
    check(state["manualSources"][size_t(0)]["owned"].isBool(), "功法来源下发真实抄本状态");
    checkEq(state["bondActions"].asArray().size(), 5, "五项师门与道侣日常均有界面入口数据");
    check(state["world"]["coreStock"].isNumber() && state["world"]["pillStock"].isNumber(),
          "坊市库存下发真实灵核和凝元丹数量");
    const auto options = json::Value::parse(request("build-options").body);
    json::Value scheme(json::Object{});
    scheme.set("budget", 1000);
    scheme.set("traits", json::Array{});
    json::Value allocation(json::Object{});
    allocation.set("apt", 8);
    allocation.set("insight", 8);
    scheme.set("allocation", allocation);
    const auto build = json::Value::parse(request("build-check", scheme).body);
    checkEq(build["budget"].asInt(), options["budget"].asInt(), "建号校验使用真实传承预算，不能伪造客户端点数");

    g = createGame(opts);
    g.pending.reset();
    for (int stage : {0, 3, 6, 9, 12}) {
        g.player.stage = g.player.highestStage = stage;
        const auto spells = json::Value::parse(request("arts/spells").body);
        checkEq(spells["max"].asInt(), 4 + 2 * (stage / 3), "神通目录下发当前境界的额外槽位");
        int fixed = 0;
        for (const auto& sp : spells["spells"].asArray()) {
            if (sp["mandatory"].asBool()) ++fixed;
        }
        const auto& spellRows = spells["spells"].asArray();
        check(std::all_of(spellRows.begin(), spellRows.end(), [](const json::Value& sp) {
            return !sp["stageName"].asString().empty() && !sp["description"].asString().empty();
        }), "所有神通均下发具体携带境界及权威效果描述");
        checkEq(fixed, 2, "普通攻击防御作为固定神通展示");
    }
    unchangedReject("arts/spell", input("id", "strike"), "固定普通攻击不能卸载");
    g.player.arts.spells = {"purge", "swordFocus", "aegis", "skim", "qiFlow"};
    EnemySpec buffEnemy; buffEnemy.name = "界面状态回归靶子";
    battleStart(g, buffEnemy);
    Battle& buffBattle = *g.player.arts.battle;
    buffBattle.mana = 12;
    auto forceHand = [&](const std::string& spell) {
        buffBattle.hand.clear(); buffBattle.draw.clear(); buffBattle.discard.clear();
        bool selected = false;
        for (const auto& card : buffBattle.deck) {
            if (std::find(buffBattle.exhausted.begin(), buffBattle.exhausted.end(), card.uid) !=
                buffBattle.exhausted.end()) continue;
            if (!selected && card.spell == spell) {
                buffBattle.hand.push_back(card.uid); selected = true;
            } else buffBattle.draw.push_back(card.uid);
        }
    };
    forceHand("swordFocus");
    check(request("battle/turn", input("id", "swordFocus")).status == 200, "增伤牌正常施放");
    auto battleState = json::Value::parse(request("state").body)["state"]["battle"];
    checkNear(battleState["buff"].asNumber(), 0.10, "施放后立即下发实际增伤");
    forceHand("qiFlow");
    check(request("battle/turn", input("id", "qiFlow")).status == 200, "持续聚气正常施放");
    battleState = json::Value::parse(request("state").body)["state"]["battle"];
    checkEq(battleState["manaBonus"].asInt(), 1, "聚气后的本场灵力增益可见");
    checkEq(battleState["nextMana"].asInt(), battleNextMana(g.player, buffBattle), "下回合回复值与战斗真实规则一致");
    forceHand("purge");
    unchangedReject("battle/turn", input("id", "purge"), "移除神通未选择目标");
    json::Value removal(json::Object{});
    removal.set("id", "purge"); removal.set("target", "purge");
    unchangedReject("battle/turn", removal, "移除牌不能选择自己");
    removal.set("target", "strike");
    check(request("battle/turn", removal).status == 200, "接口可以选择另一神通本场移除");
    battleState = json::Value::parse(request("state").body)["state"]["battle"];
    checkEq(battleState["exhausted"].asInt(), 5, "界面显示移除全部五张普攻");
    check(battleState["exhaustedSpells"].asArray().size() == 1 &&
          battleState["exhaustedSpells"][size_t(0)]["id"].asString() == "strike", "界面显示本场已移除神通名称");
    const auto withBuilds = json::Value::parse(request("state").body)["state"];
    checkEq(withBuilds["artBuilds"].asArray().size(), 8, "八种本地功法神通悟道组合接入状态");
    checkEq(withBuilds["arts"]["mandatorySpells"].asArray().size(), 2, "固定普通招式独立展示");
    for (const auto& card : withBuilds["arts"]["mandatorySpells"].asArray())
        checkEq(card["copies"].asInt(), 5, "面板显示普通攻击与防御各五张，与真实牌组一致");

    // 旧档聚气迁移到金丹神通后，炼气玩家仍必须能卸下其占用的槽位。
    g = createGame(opts);
    g.pending.reset();
    g.player.arts.spells = {"gather", "sword", "water", "wood"};
    auto oldSpells = json::Value::parse(saveToJson(g));
    oldSpells.set("combatRulesVersion", 1);
    check(request("import", input("game", oldSpells.dump())).status == 200,
          "炼气旧档聚气可以导入并迁移");
    auto spellRow = [](const json::Value& catalogue, const std::string& id) {
        for (const auto& row : catalogue["spells"].asArray())
            if (row["id"].asString() == id) return row;
        return json::Value{};
    };
    auto legacyFlow = spellRow(json::Value::parse(request("arts/spells").body), "qiFlow");
    check(legacyFlow["carried"].asBool() && !legacyFlow["unlocked"].asBool() &&
              legacyFlow["enabled"].asBool() && legacyFlow["reason"].asString().empty(),
          "低境界已携带神通目录允许卸下但不伪装为已解锁");
    check(request("arts/spell", input("id", "qiFlow")).status == 200,
          "炼气玩家可以卸下旧档迁移的金丹神通");
    checkEq(g.player.arts.spells.size(), 3, "卸下旧档高境神通释放额外槽位");
    legacyFlow = spellRow(json::Value::parse(request("arts/spells").body), "qiFlow");
    check(!legacyFlow["carried"].asBool() && !legacyFlow["enabled"].asBool() &&
              !legacyFlow["reason"].asString().empty(),
          "卸下后高境神通目录禁止重新携带并给出原因");
    unchangedReject("arts/spell", input("id", "qiFlow"), "炼气不能重新携带金丹神通");

    // 保留旧档在途牌组、手牌UID及冷却，再检查界面和执行端一致拒绝。
    g.player.arts.spells = {"gather"};
    battleStart(g, buffEnemy);
    Battle& oldBattle = *g.player.arts.battle;
    oldBattle.hand.clear(); oldBattle.draw.clear(); oldBattle.discard.clear();
    bool gathered = false;
    for (const auto& card : oldBattle.deck) {
        if (!gathered && card.spell == "gather") {
            oldBattle.hand.push_back(card.uid); gathered = true;
        } else oldBattle.draw.push_back(card.uid);
    }
    oldBattle.cooldowns["gather"] = 2;
    auto oldInFlight = json::Value::parse(saveToJson(g));
    oldInFlight.set("combatRulesVersion", 1);
    check(request("import", input("game", oldInFlight.dump())).status == 200,
          "旧档在途聚气牌组和冷却可以完整迁移");
    const Battle& migratedBattle = *g.player.arts.battle;
    check(migratedBattle.cooldowns.count("qiFlow") &&
              migratedBattle.cooldowns.at("qiFlow") == 2 &&
              !migratedBattle.cooldowns.count("gather"),
          "聚气旧冷却迁移至持续聚气ID");
    const auto inFlightState = json::Value::parse(request("state").body)["state"]["battle"];
    const auto& lockedHand = inFlightState["hand"][size_t(0)];
    check(lockedHand["id"].asString() == "qiFlow" && !lockedHand["playable"].asBool() &&
              lockedHand["reason"].asString().find("境界") != std::string::npos,
          "低境界在途高境手牌不可施放并显示境界原因");
    checkEq(lockedHand["cost"].asInt(), battleCost(g.player, *battleSpell("qiFlow")),
            "锁定手牌仍显示真实费用，不显示为零费");
    unchangedReject("battle/turn", input("id", migratedBattle.hand.front()),
                    "低境界在途高境手牌施放");

    g = createGame(opts);
    g.pending.reset(); g.job.reset();
    g.player.location = "wuling"; g.player.stage = 6;
    g.player.lifespan = realmOf(6).life;
    g.player.renown = 0; g.world.market = 1;
    g.player.arts.main.clear(); g.player.arts.aux.clear(); g.player.arts.nodes.clear();
    g.player.arts.equipped.clear();
    Gear crafted; crafted.id = "forge-api-sale"; crafted.name = "自炼售价回归";
    crafted.slot = "weapon"; crafted.grade = 2; crafted.craftCost = 100; crafted.crafted = true;
    Gear purchased = crafted; purchased.id = "buy-api-sale"; purchased.crafted = false;
    g.player.arts.gear = {crafted, purchased};
    const int baseCrafted = shop::salePrice(g, "gear", crafted.id);
    const int basePurchased = shop::salePrice(g, "gear", purchased.id);
    g.player.arts.main = "forge"; g.player.arts.manuals["forge"] = 5;
    g.player.arts.nodes = {"11-4", "11-5"};
    const int enhancedPrice = jsRound(baseCrafted * 1.5);
    checkEq(shop::salePrice(g, "gear", crafted.id), enhancedPrice,
            "实际出售路径自炼法器被动增值只乘一次50%加成");
    checkEq(ecoSalePrice(g, "gear", crafted.id), enhancedPrice,
            "旧经济报价与实际出售路径使用相同自炼增值");
    checkEq(shop::salePrice(g, "gear", purchased.id), basePurchased,
            "实际出售路径购入法器不享自炼增值");
    g.economy.marketCash = 10000;
    const int stones = g.player.stones, cash = g.economy.marketCash;
    json::Value sell(json::Object{}); sell.set("kind", "gear"); sell.set("id", crafted.id);
    check(request("sell", sell).status == 200, "真正通过出售API售出自炼法器");
    checkEq(g.player.stones - stones, enhancedPrice, "出售API实际支付一次自炼被动加成后的价款");
    checkEq(cash - g.economy.marketCash, enhancedPrice, "坊市收购资金按相同真实价款扣除");
    check(g.player.arts.gear.size() == 1 && g.player.arts.gear.front().id == purchased.id,
          "出售API真实移除一件自炼法器并保留购入法器");
    const int beforePurchased = g.player.stones;
    sell.set("id", purchased.id);
    check(request("sell", sell).status == 200, "真正通过出售API售出购入法器");
    checkEq(g.player.stones - beforePurchased, basePurchased,
            "购入法器实际成交不受生活悟道加价影响");
}
} // namespace nztest
