#include "harness.h"
#include "core/progression/zhuji_rules.h"
#include "core/progression/zhuji_board.h"
#include "core/persistence/zhuji_codec.h"
#include "core/json.h"
#include "core/model.h"
#include "core/sim.h"
#include "core/interface/zhuji_api.h"
#include "core/calendar.h"
#include "core/save.h"
#include "core/api.h"

namespace nztest {
void mfRunZhujiTests() {
    using namespace nz;
    std::printf("\n-- 正式双路线筑基 --\n");
    checkEq(static_cast<long long>(mfZhujiNodes().size()), 36, "周天保留36穴");
    checkEq(static_cast<long long>(mfZhujiEdges().size()), 65, "周天保留65边");
    auto Elements = mfCreateZhujiRun(ZhujiRoute::Elements, 2, true, {}, 11);
    checkEq(Elements.Round, 1, "五行只抽首轮");
    checkEq(Elements.PillDraws.size(), 4, "每丹独立抽两点五行");
    check(Elements.Resources == std::array<int, 5>{0, 3, 1, 3, 3}, "五行seed11真实抽气与确认原型逐项一致");
    check(Elements.PillDraws == std::vector<int>{1, 4, 4, 1}, "丹药随机顺序与原型一致");
    auto Before = persistence::mfZhujiToJson(Elements).dump();
    ZhujiAction NextRound{"next"};
    NextRound.Keep = std::vector<int>{};
    auto Quote = mfQuoteZhujiAction(Elements, NextRound);
    check(Quote.Enabled, "可进入下一轮");
    checkStr(persistence::mfZhujiToJson(Elements).dump(), Before, "报价不推进局部随机流或状态");
    auto Invalid = mfApplyZhujiAction(Elements, ZhujiAction{"five", {0,0,0,0,0}});
    check(!Invalid.empty(), "非法五行组合拒绝");
    checkStr(persistence::mfZhujiToJson(Elements).dump(), Before, "拒绝不部分扣气");
    checkEq(mfZhujiHealthReward(100), 0, "100分无奖励");
    checkEq(mfZhujiHealthReward(101), 1, "101分增加1基础气血");
    checkEq(mfZhujiHealthReward(130), 20, "基础气血奖励120分封顶");

    auto Meridian = mfCreateZhujiRun(ZhujiRoute::Meridians, 1, true, {}, 25);
    checkEq(Meridian.QiInitial, 115, "一丹提供15真气");
    auto North = mfQuoteZhujiAction(Meridian, ZhujiAction{"strike", {}, "zhiyang"});
    check(North.Enabled, "丹田北向至阳可以直接冲穴");
    checkEq(North.QiCost, 4, "北向普通穴费用不变");
    auto NorthRun = Meridian;
    check(mfApplyZhujiAction(NorthRun, ZhujiAction{"strike", {}, "zhiyang"}).empty(), "北路真实冲击提交");
    checkEq(NorthRun.Qi, 111, "北路成功耗4气");
    checkEq(NorthRun.Score, 4, "北路首次成功4分");
    checkStr(NorthRun.PositionId, "zhiyang", "北路成功移动至至阳");
    checkEq(NorthRun.RandomStates[0], 1055517348, "周天随机序列与原型逐值一致");
    check(mfApplyZhujiAction(NorthRun, ZhujiAction{"move", {}, "start"}).empty(), "北路双向回丹田");
    checkEq(NorthRun.Qi, 110, "已通回走只耗1气");
    checkEq(NorthRun.Score, 4, "回走不重复得分");
    check(!mfQuoteZhujiAction(Meridian, ZhujiAction{"seal"}).Enabled, "三关未通不能归元");
    auto DaoBody = mfCreateZhujiRun(ZhujiRoute::Meridians, 0, false, {"daoBody"}, 25);
    checkEq(mfQuoteZhujiAction(DaoBody, ZhujiAction{"strike", {}, "zhiyang"}).QiCost, 3,
            "真实道体冲穴少耗1气");
    auto Encoded = persistence::mfZhujiToJson(Meridian);
    ZhujiRun Restored;
    std::string Error;
    check(persistence::mfZhujiFromJson(Encoded, Restored, Error), "在途局严格读回");
    checkStr(persistence::mfZhujiToJson(Restored).dump(), Encoded.dump(), "私有随机流完整往返");
    Encoded.set("revision", -1);
    check(!persistence::mfZhujiFromJson(Encoded, Restored, Error), "负修订号明确拒绝");
    checkStr(persistence::mfZhujiToJson(Restored).dump(), persistence::mfZhujiToJson(Meridian).dump(),
             "错误导入不覆盖原局");

    Game State;
    State.player.stage = 2; State.player.xp = realmOf(2).need; State.player.stones = 1000;
    State.player.pills = 4; State.player.cores = 2; State.player.health = 100; State.player.arts.knownSpells = {"fire"};
    json::Value Start(json::Object{}); Start.set("kind", "start"); Start.set("route", "elements"); Start.set("pills", 2);
    Start.set("core", true); Start.set("revision", 0); Start.set("requestId", "isolated-zhuji-start");
    json::Value Response(json::Object{});
    check(api::mfExecuteZhujiCommand(State, Start, Response).empty(), "真实筑基按所选准备入关");
    checkEq(State.player.stones, 900, "入关扣旧报价100灵石");
    checkEq(State.player.pills, 2, "入关扣真实选择2颗丹");
    checkEq(State.player.cores, 1, "携核只扣1枚");
    check(State.job && State.job->remaining == 90 && State.player.Zhuji->Status == ZhujiStatus::Preparing, "先进入90天准备安排");
    check(api::mfExecuteZhujiCommand(State, Start, Response).empty() && Response["replayed"].asBool(), "同入关请求重放成功但不复扣");
    checkEq(State.player.stones, 900, "重复入关未扣第二次");
    mfCompleteZhujiPreparation(State); State.job.reset();
    State.player.Zhuji->Score = State.player.Zhuji->RawScore = 107;
    check(mfApplyZhujiAction(*State.player.Zhuji, ZhujiAction{"seal"}).empty(), "五行107分永久定型");
    mfSettleZhujiRun(State);
    checkEq(State.player.stage, 3, "小游戏功成真实升至筑基，无二次概率抽签");
    checkEq(State.player.arts.FoundationHealthBonus, 7, "107分实际落永久基础气血7");
    mfSettleZhujiRun(State);
    checkEq(State.player.arts.FoundationHealthBonus, 7, "重复结算不发第二次奖励");
    check(!mfStartZhuji(State, ZhujiRoute::Meridians, 0, false).empty(), "成功后不能换路线或补修");
    json::Value Bind(json::Object{}); Bind.set("kind", "bind"); Bind.set("revision", State.player.Zhuji->Revision);
    Bind.set("requestId", "isolated-zhuji-bind"); Bind.set("spell", "fire");
    check(api::mfExecuteZhujiCommand(State, Bind, Response).empty(), "已领悟未携带神通可选作本命");
    checkStr(State.player.arts.LifeboundKind, "innate", "五行路线关联固有");
    checkStr(State.player.arts.LifeboundSpell, "fire", "本命槽记录所选神通");
    const auto BoundLogCount = State.logs.size();
    const auto BoundRevision = State.player.Zhuji->Revision;
    Bind.set("revision", State.player.Zhuji->Revision); Bind.set("requestId", "isolated-zhuji-bind-again");
    check(api::mfExecuteZhujiCommand(State, Bind, Response).empty(), "重复设置同一本命成功且不重复结算");
    check(State.logs.size() == BoundLogCount && State.player.Zhuji->Revision == BoundRevision,
        "同一本命重复设置不产生新日志或修订");
    bool HasCurrent = false, HasStrike = false, HasDefend = false;
    const auto BoundSnapshot = api::view::mfZhujiView(State);
    for (const auto& Choice : BoundSnapshot["lifeboundChoices"].asArray()) {
        HasCurrent |= Choice["id"].asString() == "fire" && Choice["selected"].asBool();
        HasStrike |= Choice["id"].asString() == "strike" && Choice["enabled"].asBool();
        HasDefend |= Choice["id"].asString() == "defend" && Choice["enabled"].asBool();
    }
    check(HasCurrent && HasStrike && HasDefend, "已有本命的筑基快照仍提供普通攻防与可更换候选");
    const auto FoundationBeforeRebind = State.player.arts.FoundationHealthBonus;
    for (const std::string SpellId : {"strike", "fire"}) {
        Bind.set("revision", State.player.Zhuji->Revision);
        Bind.set("requestId", "isolated-zhuji-rebind-" + SpellId); Bind.set("spell", SpellId);
        check(api::mfExecuteZhujiCommand(State, Bind, Response).empty() && State.player.arts.LifeboundSpell == SpellId,
            "已结算筑基本命允许焰火到普攻再回焰火");
    }
    checkEq(State.player.arts.FoundationHealthBonus, FoundationBeforeRebind, "更换本命不补发筑基根基奖励");
    checkStr(State.player.arts.LifeboundKind, "innate", "更换本命保持五行筑基的固有类型");

    Game Retry;
    Retry.player.stage = 2; Retry.player.xp = realmOf(2).need; Retry.player.stones = 1000;
    Retry.player.pills = 4; Retry.player.cores = 2; Retry.player.health = 100;
    check(api::mfExecuteZhujiCommand(Retry, Start, Response).empty(), "隔离失败重试角色入关");
    mfCompleteZhujiPreparation(Retry); Retry.job.reset();
    json::Value Withdraw(json::Object{}); Withdraw.set("kind", "action"); Withdraw.set("revision", Retry.player.Zhuji->Revision);
    Withdraw.set("requestId", "isolated-zhuji-withdraw");
    json::Value WithdrawAction(json::Object{}); WithdrawAction.set("kind", "withdraw"); Withdraw.set("action", WithdrawAction);
    check(api::mfExecuteZhujiCommand(Retry, Withdraw, Response).empty(), "主动收功真实结算");
    check(Retry.player.Zhuji->Settled && Retry.player.stage == 2 && Retry.player.xp < realmOf(2).need,
          "收功扣修为而不升境，准备不退");
    Retry.player.xp = realmOf(2).need;
    auto Restart = Start; Restart.set("revision", Retry.player.Zhuji->Revision); Restart.set("requestId", "isolated-zhuji-restart");
    check(api::mfExecuteZhujiCommand(Retry, Restart, Response).empty(), "失败或收功后可以重新准备");
    checkEq(Retry.player.stones, 800, "新尝试真实扣第二份准备");
    check(api::mfExecuteZhujiCommand(Retry, Start, Response).empty(), "跨尝试重放旧入关收据");
    checkEq(Retry.player.stones, 800, "跨尝试旧入关重放不扣第三次");
    Withdraw.set("requestId", "isolated-zhuji-stale-new-id");
    check(!api::mfExecuteZhujiCommand(Retry, Withdraw, Response).empty(), "旧尝试修订号拒绝新动作ID");

    auto CalendarState = createGame(CreateOptions{});
    CalendarState.npcs.clear(); CalendarState.pending.reset(); CalendarState.player.stage = 2;
    CalendarState.player.xp = realmOf(2).need; CalendarState.player.stones = 1000;
    CalendarState.player.health = 100; CalendarState.checkpoint.enabled = false; CalendarState.checkpoint.snapshot.clear();
    check(mfStartZhuji(CalendarState, ZhujiRoute::Meridians, 0, false).empty(), "真实日历筑基准备可启动");
    for (int Month = 0; Month < 3 && CalendarState.job; ++Month) {
        const auto Result = calendarRun(CalendarState, 30);
        check(Result.ok, "真实日历推进准备月份");
    }
    checkEq(CalendarState.clock.days, 90, "真实日历恰好扣90天");
    check(CalendarState.player.Zhuji && CalendarState.player.Zhuji->Status == ZhujiStatus::Active &&
          !CalendarState.job && !CalendarState.pending && !CalendarState.player.arts.battleActive(),
          "90天完成后仅激活小游戏，不生成阻塞抉择或第二次随机突破");
    checkEq(CalendarState.player.stage, 2, "小游戏定型前保持炼气后期");

    for (const int RequestedDays : {30, 360}) {
        api::Session Session;
        CreateOptions Options; Options.traitsExplicit = true; Options.seed = 71377;
        Session.g = createGame(Options); Session.started = true;
        auto& PreparedState = Session.g;
        PreparedState.npcs.clear(); PreparedState.pending.reset(); PreparedState.player.stage = PreparedState.player.highestStage = 2;
        PreparedState.player.xp = realmOf(2).need; PreparedState.player.stones = 1000; PreparedState.player.health = 100;
        PreparedState.checkpoint.enabled = false; PreparedState.checkpoint.snapshot.clear();
        check(mfStartZhuji(PreparedState, ZhujiRoute::Meridians, 0, false).empty(), "推进边界夹具真实开启九十天准备");
        while (PreparedState.job && PreparedState.job->remaining > 1) {
            const auto Progress = calendarRun(PreparedState, PreparedState.job->remaining - 1);
            if (!Progress.ok) break;
        }
        check(PreparedState.job && PreparedState.job->remaining == 1 && PreparedState.clock.days == 89,
              "真实准备推进到剩余一天");
        api::bindSession(&Session);
        json::Value AdvanceInput(json::Object{}); AdvanceInput.set("days", RequestedDays);
        const auto Advanced = api::handle({"POST", "/api/advance", AdvanceInput.dump()});
        checkEq(Advanced.status, 200, "准备剩一天时接受长推进请求");
        checkEq(PreparedState.clock.days, 90, "筑基激活立即停止长推进，不额外履行日常计划");
        check(PreparedState.player.Zhuji->Status == ZhujiStatus::Active && !PreparedState.job && !PreparedState.pending && !PreparedState.player.arts.battleActive(),
              "准备完成后只保留筑基运转，不生成普通安排");
        const auto ActivatedSave = saveToJson(PreparedState);
        checkEq(api::handle({"GET", "/api/zhuji"}).status, 200, "激活后筑基只读重取成功");
        checkEq(api::handle({"GET", "/api/state"}).status, 200, "激活后完整只读重取成功");
        checkStr(saveToJson(PreparedState), ActivatedSave, "激活后的只读重取不续推时间、计划或随机流");
        Game LoadedActive;
        check(loadFromJson(ActivatedSave, LoadedActive, &Error), "长推进停止后的激活存档合法：" + Error);
        json::Value StrikeInput(json::Object{}), StrikeAction(json::Object{});
        StrikeInput.set("kind", "action"); StrikeInput.set("revision", PreparedState.player.Zhuji->Revision);
        StrikeInput.set("requestId", "isolated-advance-stop-" + std::to_string(RequestedDays));
        StrikeAction.set("kind", "strike"); StrikeAction.set("targetId", "zhiyang"); StrikeInput.set("action", StrikeAction);
        checkEq(api::handle({"POST", "/api/zhuji", StrikeInput.dump()}).status, 200, "长推进完成后可以正常提交冲穴");
        checkEq(PreparedState.player.Zhuji->Qi, 96, "后续冲穴按真实费用只消耗一次真气");
        api::bindSession(nullptr);
    }

    const auto PublicBefore = persistence::mfZhujiToJson(*CalendarState.player.Zhuji).dump();
    const auto PublicView = api::view::mfZhujiView(CalendarState);
    check(PublicView.dump().find("\"rng\"") == std::string::npos, "公开局面不发送私有随机状态");
    const int HiddenIndex = mfZhujiNodeIndex("yintang");
    check(!PublicView["nodes"][HiddenIndex]["revealed"].asBool() && PublicView["nodes"][HiddenIndex]["kind"].isNull() &&
          PublicView["nodes"][HiddenIndex]["points"].isNull(), "未揭示穴不泄露名称以外的真实种类与分数");
    checkStr(persistence::mfZhujiToJson(*CalendarState.player.Zhuji).dump(), PublicBefore, "公开查询不改变局部随机流或进度");

    Game Prepared;
    Prepared.player.stage = 2; Prepared.player.xp = realmOf(2).need; Prepared.player.stones = 1000;
    Prepared.player.health = 100; Prepared.player.pills = 6;
    Prepared.player.arts.breakBuff = "stage"; Prepared.player.arts.BreakthroughPillStage = 2;
    check(!mfStartZhuji(Prepared, ZhujiRoute::Meridians, 6, false).empty(), "已服准备也计入6份上限，拒绝再选6丹");
    checkEq(Prepared.player.pills, 6, "拒绝超量准备不部分扣丹");
    check(mfStartZhuji(Prepared, ZhujiRoute::Meridians, 5, false).empty(), "已服准备加库存5丹可入关");
    checkEq(Prepared.player.Zhuji->PillCount, 6, "记录实际生效6份准备");
    checkEq(Prepared.player.Zhuji->QiInitial, 190, "已服突破丹也真实提供15真气");
    checkEq(Prepared.player.pills, 1, "已服准备不重复扣库存");
    check(Prepared.player.arts.breakBuff.empty() && Prepared.player.arts.BreakthroughPillStage == -1, "只消费匹配当前境界的突破准备");

    for (const auto Status : {ZhujiStatus::Preparing, ZhujiStatus::Active}) {
        auto Death = createGame(CreateOptions{});
        Death.npcs.clear(); Death.pending.reset(); Death.player.stage = 2; Death.player.xp = realmOf(2).need;
        Death.player.highestStage = 2; Death.player.stones = 1000; Death.player.health = 100;
        Death.checkpoint.enabled = false; Death.checkpoint.snapshot.clear();
        check(mfStartZhuji(Death, ZhujiRoute::Meridians, 0, false).empty(), "死亡边界测试真实入关");
        if (Status == ZhujiStatus::Active) { mfCompleteZhujiPreparation(Death); Death.job.reset(); }
        const int ExperienceBeforeDeath = Death.player.xp;
        Death.player.health = 0;
        finish(Death, "隔离筑基死亡测试");
        check(Death.player.Zhuji->Status == ZhujiStatus::Withdrawn && Death.player.Zhuji->Settled && !Death.job,
              "准备或施术中死亡终止筑基，清除在途安排");
        checkEq(Death.player.xp, ExperienceBeforeDeath, "死亡终止不重复套失败修为惩罚");
        Game LoadedDeath;
        std::string LoadError;
        check(loadFromJson(saveToJson(Death), LoadedDeath, &LoadError), "死亡筑基存档可完整读回：" + LoadError);
        check(LoadedDeath.ended && LoadedDeath.player.Zhuji && LoadedDeath.player.Zhuji->Settled, "死亡结束和已终止筑基往返一致");
    }
}
}
