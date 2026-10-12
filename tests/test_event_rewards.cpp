#include "harness.h"

#include <algorithm>

#include "core/adventure.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/reward_receipt.h"
#include "core/save.h"
#include "core/sect_trial.h"
#include "core/sim.h"
#include "core/society.h"
#include "data/reference_tables.h"

namespace nztest {
using namespace nz;
namespace {

Game eventGame() {
    CreateOptions options;
    options.seed = 20261012;
    options.name = "事件收益核验";
    Game game = createGame(options);
    ecoSyncBooks(game);
    game.pending.reset(); game.job.reset(); game.logs.clear();
    game.player.traits.clear();
    game.player.stones = 500;
    game.player.health = 100;
    return game;
}

bool hasBook(const vector<string>& books, const string& id) {
    return std::find(books.begin(), books.end(), id) != books.end();
}

bool logged(const Game& game, const string& text) {
    return std::any_of(game.logs.begin(), game.logs.end(), [&](const auto& log) { return log.text.find(text) != string::npos; });
}

const data::StoryDef& story(const string& id) {
    for (const auto& entry : data::StoryDef_TABLE()) if (entry.id == id) return entry;
    throw std::runtime_error("missing story " + id);
}

bool validBooks(const Game& game) {
    for (const auto& id : game.economy.books) {
        ManualInfo info;
        if (!findManualInfo(id, info) || !hasBook(game.player.arts.bookList, id)) return false;
    }
    return true;
}

struct EventApiRestore {
    Game original = api::game();
    ~EventApiRestore() { api::game() = std::move(original); }
};

http::Response chooseEvent(const string& id) {
    json::Value body(json::Object{});
    body.set("id", id);
    return api::handle({"POST", "/api/choose", body.dump(), ""});
}

} // namespace

void runEventRewardTests() {
    std::printf("\n-- 事件真实收益、抄本与师门差事回归 --\n");
    {
        auto game = eventGame();
        adventureEvent(game, "heritage");
        game.pending->adventure->book = "wheel";
        check(adventureChoose(game, "book"), "残碑抄录选择完成");
        checkEq(game.player.stones, 488, "残碑按承诺消耗12灵石");
        check(hasBook(game.economy.books, "wheel") && hasBook(game.player.arts.bookList, "wheel"), "重元转轮诀抄本同步两套书单，功法页可见");
        check(!game.player.arts.manuals.count("wheel"), "抄本并非免费修成的功法");
        check(logged(game, "重元转轮诀") && logged(game, "需修习") && logged(game, "灵石−12"), "结算明确抄本、学习入口与真实代价");
        check(!logged(game, "所选报酬与代价已结算"), "结算不再用虚假的通用奖励说明");
        check(ecoActionError(game, "learn", "wheel").empty(), "所得抄本可以开始修习");
        const auto info = ecoActionInfo(game, "learn", "wheel");
        ecoBegin(game, "learn", "wheel");
        check(game.job.has_value(), "已有抄本修习确实生成工作");
        if (game.job) {
            const auto job = *game.job; game.job.reset(); ecoComplete(game, job);
            checkEq(game.player.arts.manuals["wheel"], 1, "完成修习后才学会第一层");
            checkEq(game.player.stones, 488 - info.cost, "学习照常支付学费");
        }
    }
    {
        auto game = eventGame();
        game.player.arts.bookList.push_back("wheel");
        adventureEvent(game, "heritage");
        check(hasBook(game.economy.books, "wheel"), "旧档只在bookList的抄本先同步再抽取奖励");
        check(game.pending->adventure->book != "wheel", "已拥有抄本不再进入新传承候选池");
        game.pending->adventure->book = "wheel";
        const int exp = game.player.arts.exp["剑"];
        adventureChoose(game, "book");
        checkEq(game.player.arts.exp["剑"], exp + 12, "残碑重复传承真实转为剑道经验12");
        checkEq(std::count(game.economy.books.begin(), game.economy.books.end(), "wheel"), 1, "重复抄本不堆积");
        check(logged(game, "剑道经验+12") && logged(game, "未重复发放"), "重复传承明示实际转换");
    }
    {
        auto game = eventGame(); game.player.arts.focus = "剑";
        const int before = game.player.arts.exp["剑"];
        adventureEvent(game, "heritage");
        check(game.pending->options[1].desc.find("剑意") == string::npos, "参悟选项不再承诺已经删除的剑意");
        adventureChoose(game, "study");
        checkEq(game.player.arts.exp["剑"], before + 9, "剑道主悟合并8加1经验");
        check(logged(game, "剑道经验+9"), "结算合并同一道的实际增量");
    }
    {
        auto game = eventGame();
        const int before = game.player.arts.exp["器"];
        adventureEvent(game, "caravan"); adventureChoose(game, "help");
        checkEq(game.player.stones, 508, "商旅工钱16扣修阵费8，净赚8");
        checkEq(game.player.arts.exp["器"], before + 4, "修车阵器道实际加4");
        check(logged(game, "灵石+8") && logged(game, "器道经验+4"), "商旅结算显示净收入和真实经验");
    }
    {
        auto game = eventGame(); game.player.insight = INSIGHT_MAX;
        adventureEvent(game, "caravan"); adventureChoose(game, "leave");
        check(logged(game, "未新增物品或属性收益"), "已封顶悟性不再假报加1");
    }
    {
        auto game = eventGame();
        check(!ecoAcquireBook(game, "nonexistent-manual", "无效传承测试"), "无效奖励ID不会假发功法");
        check(!hasBook(game.economy.books, "nonexistent-manual") && !hasBook(game.player.arts.bookList, "nonexistent-manual"), "两套书单没有未知ID");
    }
    {
        auto game = eventGame(); game.player.stones = 39;
        const string before = saveToJson(game);
        check(!storyChoiceError(game, story("rootecho"), 0).empty(), "残碑支线不足40灵石有准确原因");
        storyComplete(game, story("rootecho"), 0);
        checkStr(saveToJson(game), before, "余额不足不能免费提升资质或消耗支线");
        check(!storyChoiceError(game, story("rootecho"), 2).empty(), "非法支线选项被拒绝");
        game.player.stones = 40; const int apt = game.player.apt;
        storyComplete(game, story("rootecho"), 0);
        checkEq(game.player.stones, 0, "承接残碑固定扣40灵石");
        checkEq(game.player.apt, std::min(APT_MAX, apt + 3), "资质奖励与上限规则一致");
        const string completed = saveToJson(game);
        storyComplete(game, story("rootecho"), 0);
        checkStr(saveToJson(game), completed, "已完支线不能再次领奖");
    }
    for (const auto& entry : data::StoryDef_TABLE()) {
        for (int option = 0; option < 2; ++option) {
            auto game = eventGame(); const RewardReceipt receipt(game);
            storyComplete(game, entry, option);
            check(game.bonds.stories.count(entry.id), entry.name + "选项" + std::to_string(option) + "真实完成");
            check(validBooks(game), entry.name + "只发真实且两套可见的抄本");
            check(logged(game, receipt.changes(game)), entry.name + "结算包含全部实际收益");
        }
    }
    for (const auto& entry : data::QuestDef_TABLE()) {
        auto game = eventGame();
        game.player.traits = {"blessing"};
        const RewardReceipt receipt(game);
        artRewardQuest(game, entry);
        check(game.player.arts.quests.count(entry.id), entry.name + "机缘记录完成");
        check(validBooks(game), entry.name + "机缘抄本ID有效并同步可见");
        check(logged(game, receipt.changes(game)), entry.name + "日志包括天赋倍率后的实际奖励");
        const string completed = saveToJson(game);
        artRewardQuest(game, entry);
        checkStr(saveToJson(game), completed, entry.name + "完成机缘不能重复结算奖励");
    }
    {
        auto game = eventGame(); game.player.luck = 150;
        storyComplete(game, story("letter"), 0);
        check(!logged(game, "福缘+2") && logged(game, "心境+4"), "封顶属性不假报，未封顶奖励仍显示");
    }
    {
        auto game = eventGame();
        game.player.sect = game.player.location = "zhushan";
        game.player.stage = 3;
        game.world.sects["zhushan"].treasury = 10000;
        for (const auto& def : sectTaskDefs()) {
            game.economy.taskCounts.clear();
            const auto info = ecoActionInfo(game, "task", def.id);
            check(info.valid && info.days == def.days, def.name + "由同一任务表提供用时");
            const int treasury = game.world.sects["zhushan"].treasury;
            const int stones = game.player.stones, contribution = game.economy.contribution;
            const int exp = game.player.arts.exp["木"];
            ecoBegin(game, "task", def.id);
            check(game.job.has_value(), def.name + "在所属山门可接");
            if (!game.job) continue;
            const Job job = *game.job;
            checkStr(job.sect, "zhushan", def.name + "冻结接单宗门");
            checkStr(job.origin, "zhushan", def.name + "冻结接单地点");
            checkEq(game.world.sects["zhushan"].treasury, treasury - info.fee, def.name + "从宗门府库预留工钱");
            game.job.reset(); ecoComplete(game, job);
            checkEq(game.player.stones, stones + info.fee, def.name + "真实发放冻结工钱");
            checkEq(game.economy.contribution, contribution + def.contribution, def.name + "贡献与任务预览一致");
            checkEq(game.player.arts.exp["木"], exp + def.daoExp, def.name + "只增本宗大道经验");
        }
        game.economy.taskCounts = {{"sect", 1}, {"sect-patrol", 1}, {"sect-records", 1}};
        const string before = saveToJson(game);
        check(!ecoActionError(game, "task", "sect-array").empty(), "新旧四种宗门差事共用月三次额度");
        ecoBegin(game, "task", "sect-array");
        checkStr(saveToJson(game), before, "切换差事不能突破共用额度或扣府库");
        game.clock.days += 30;
        check(ecoActionError(game, "task", "sect-patrol").empty(), "跨月陈旧任务计数不误拦新任务");
    }
    {
        auto game = eventGame();
        game.player.sect = game.player.location = "zhushan";
        game.world.sects["zhushan"].treasury = 10000;
        ecoBegin(game, "task", "sect-records");
        check(game.job.has_value(), "防跨宗结算测试差事创建");
        if (game.job) {
            const Job job = *game.job; game.job.reset();
            game.player.sect = game.player.location = "lihuo";
            const int stones = game.player.stones, contribution = game.economy.contribution;
            ecoComplete(game, job);
            checkEq(game.player.stones, stones, "改变所属宗门不能领取旧山门工钱");
            checkEq(game.economy.contribution, contribution, "改变所属宗门不能套取新门贡献");
            check(logged(game, "未发放工钱"), "未交付差事给出真实拒绝原因");
        }
    }
    {
        EventApiRestore restore;
        api::game() = eventGame();
        auto& game = api::game();
        game.world.herbs = 0; game.player.luck = 150; game.rng = 0;
        check(!game.npcs.empty(), "药谷API回归有真实同行道友");
        if (!game.npcs.empty()) {
            const string npcId = game.npcs.front().id;
            game.npcs.front().alive = true;
            game.player.relations[npcId] = 100;
            Pending pending; pending.type = "valley"; pending.title = "药谷中的求援";
            pending.npcId = npcId;
            pending.options = {{"help", "合力救援", "灵药最多+3，好感最多+20", false}};
            game.pending = pending;
            const int herbs = game.player.herbs, health = game.player.health;
            checkEq(chooseEvent("help").status, 200, "药谷选择通过真实API结算");
            check(!game.pending.has_value(), "药谷选择成功关闭待处理抉择");
            checkEq(game.player.herbs, herbs, "药谷存量为0时没有新增灵药");
            checkEq(game.world.herbs, 0, "药谷无灵药时不会扣成负数");
            checkEq(game.player.relations[npcId], 100, "已满好感不再提高");
            checkEq(game.player.health, health, "固定随机数下药谷救援平安");
            check(logged(game, "实际结算：未新增物品或属性收益"), "药谷无资源且好感封顶时明确没有新增收益");
            check(!logged(game, "灵药+3") && !logged(game, "交情+20") && !logged(game, "好感+20"), "药谷不虚报三份灵药或二十交情");
        }
    }
    {
        EventApiRestore restore;
        api::game() = eventGame();
        auto& game = api::game();
        game.world.herbs = 2; game.player.luck = 150; game.rng = 0;
        if (!game.npcs.empty()) {
            const string npcId = game.npcs.front().id;
            game.npcs.front().alive = true; game.player.relations[npcId] = 99;
            Pending pending; pending.type = "valley"; pending.title = "药谷中的求援"; pending.npcId = npcId;
            pending.options = {{"help", "合力救援", "灵药最多+3，好感最多+20", false}};
            game.pending = pending;
            const int herbs = game.player.herbs;
            const auto herbInventory = game.player.arts.herbs;
            checkEq(chooseEvent("help").status, 200, "药谷有限资源通过真实API结算");
            checkEq(game.player.herbs, herbs + 2, "药谷仅取得实际余下的两份灵药");
            checkEq(game.player.relations[npcId], 100, "九十九交情实际仅加一");
            int received = 0;
            bool loggedMaterials = true;
            for (const auto& [name, count] : game.player.arts.herbs) {
                const auto old = herbInventory.find(name);
                const int delta = count - (old == herbInventory.end() ? 0 : old->second);
                if (delta > 0) { received += delta; loggedMaterials &= logged(game, name + "+" + std::to_string(delta)); }
            }
            check(received == 2 && loggedMaterials && logged(game, "交情+1"), "药谷日志显示有限具体草药和交情真实增量");
            check(!logged(game, "灵药+3") && !logged(game, "交情+20"), "药谷有限资源不再按名义奖励假报");
        }
    }
    {
        EventApiRestore restore;
        api::game() = eventGame();
        auto& game = api::game(); game.player.insight = INSIGHT_MAX;
        Pending pending; pending.type = "sectInvitation"; pending.title = "山门来信";
        pending.options = {{"study", "先访各宗，再作打算", "悟性最多+8", false}};
        game.pending = pending;
        checkEq(chooseEvent("study").status, 200, "山门来信通过真实API选择请教");
        checkEq(game.player.insight, INSIGHT_MAX, "封顶悟性保持上限");
        check(!game.pending.has_value(), "来信选择正常关闭");
        check(logged(game, "实际结算：未新增物品或属性收益"), "封顶悟性结算明示没有属性收益");
        check(!logged(game, "悟性+8") && !logged(game, "悟道+8"), "山门来信不假报封顶悟性或悟道奖励");
    }
}

} // namespace nztest
