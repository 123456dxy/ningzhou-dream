#include "harness.h"
#include "core/sim.h"
#include "core/tide.h"
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/save.h"
#include "core/calendar.h"
#include "core/interface/views.h"

namespace nztest {
using namespace nz;
namespace {
// 用途：建立隔离的60天讨伐场景；流程：回归测试；传境界，返回无NPC/事件/工作干扰的游戏。
Game tideFixture(int stage=6) {
    CreateOptions options; options.seed=20261013; options.traitsExplicit=true;
    Game game=createGame(options); game.npcs.clear(); game.pending.reset(); game.job.reset();
    game.checkpoint.enabled=false; game.player.stage=stage; game.player.highestStage=stage;
    game.player.health=mfPlayerHealthMaximum(game.player);
    game.tide.active=true; game.tide.monthsLeft=12; game.tide.rulesVersion=0;
    tide::ensureTide(game);
    return game;
}
// 用途：固定只承受守势的妖兽，消除计日测试的随机死亡；流程：测试；无返回值。
void safeEnemy(Game& game) {
    auto& battle=*game.player.arts.battle;
    battle.BeastActions.clear(); battle.enemyPower=0; battle.intentKind="guard";
    battle.intentRaw=0; battle.intentScale=0; battle.enemyHp=battle.enemyMax=100000;
}
// 用途：找到本场固定普通攻击；流程：快速击杀测试；返回手牌UID，保证不伪造战场卡牌。
string attackCard(Game& game) {
    auto& battle=*game.player.arts.battle;
    for (const auto& card : battle.deck) if (card.spell=="strike") {
        battle.hand={card.uid}; battle.draw.clear(); battle.discard.clear(); battle.exhausted.clear(); battle.mana=10;
        for (const auto& Other : battle.deck) if (Other.uid != card.uid) battle.draw.push_back(Other.uid);
        return card.uid;
    }
    return "";
}
}
// 用途：验证回合日历、期末战果和存档边界；由测试入口调用；返回无，输出有效断言。
void runTideIntegrationTests() {
    std::printf("\n--- 兽潮日历、结榜与存档 ---\n");
    {
        auto game=tideFixture();
        checkEq(game.tide.endDay-game.tide.startDay,60,"旧活跃兽潮迁移为60天，不依赖月界");
        check(tide::fight(game,0).ok,"低战场允许较高境界参加"); safeEnemy(game);
        const int before=game.clock.days;
        check(battleTurn(game,"end").ok,"兽潮完整回合成功");
        checkEq(game.clock.days,before+1,"兽潮完整回合恰好一天");
        const string snapshot=saveToJson(game);
        check(!battleTurn(game,"missing-card").ok,"无效卡牌被拒绝");
        checkStr(saveToJson(game),snapshot,"无效卡牌不耗时不改账本");
    }
    {
        auto game=tideFixture(); tide::fight(game,0);
        const auto card=attackCard(game); game.player.arts.battle->enemyHp=1;
        const int before=game.clock.days;
        check(!card.empty() && battleTurn(game,card).ok,"首轮普通攻击击杀");
        checkEq(game.clock.days,before+1,"首轮斩杀也耗一天，不能零天刷分");
        checkEq(game.tide.playerScore,1,"炼气战场击杀只记一分");
    }
    {
        auto game=tideFixture(); tide::fight(game,0); safeEnemy(game);
        auto& battle=*game.player.arts.battle; battle.enemyHp=1;
        battle.poison=1; battle.poisonDamage=1; battle.poisonTurns=1;
        battleTurn(game,"end");
        checkEq(game.clock.days,game.tide.startDay+1,"持续伤害提前结束回合也耗一天");
        checkEq(game.tide.playerScore,1,"持续伤害击殺正常记分");
    }
    {
        auto game=tideFixture(); game.clock.days=game.tide.endDay-1; game.tide.lastProcessedDay=game.clock.days;
        tide::fight(game,0); auto card=attackCard(game); game.player.arts.battle->enemyHp=1;
        battleTurn(game,card);
        check(game.tide.settled && !game.tide.active,"第60天结榜");
        checkEq(game.tide.playerScore,1,"最后一天击杀先计分再结榜");
        check(game.tide.rewardPaid && game.tide.rewardStones>0,"正分玩家得到一次排名奖励");
        const int stones=game.player.stones; tide::settleIfDue(game); tide::dayTick(game);
        checkEq(game.player.stones,stones,"重复结算不重复发奖");
        const string save=saveToJson(game); Game loaded; string error;
        check(loadFromJson(save,loaded,&error),"金榜奖励和市场库存可保存重载："+error);
        checkEq(loaded.tide.playerScore,game.tide.playerScore,"重载保留末日积分");
        checkEq(loaded.player.stones,stones,"重载不重复付款");
        checkEq(loaded.rng,game.rng,"现代存档重载不重新抽签");
        checkEq(loaded.tide.marketStock.size(),game.tide.marketStock.size(),"重载保留有限高阶市场库存");
        const auto view=api::view::tideState(game); const auto state=api::view::state(game);
        checkStr(view.dump(),state["tide"].dump(),"独立兽潮查询和完整state使用相同权威视图");
        auto corrupt=json::Value::parse(save); auto data=corrupt["tide"]; data.set("playerScore",json::Value(1.5));
        corrupt.set("tide",data); check(!loadFromJson(corrupt.dump(),loaded,&error),"拒绝小数积分而不截断为整数");
        corrupt=json::Value::parse(save); data=corrupt["tide"]; data.set("eventId",json::Value(""));
        corrupt.set("tide",data); check(!loadFromJson(corrupt.dump(),loaded,&error),"现代兽潮不能以空ID绕过期限和库存检查");
        corrupt=json::Value::parse(save); data=corrupt["tide"];
        if (!data["marketStock"].asArray().empty()) {
            auto stocks=data["marketStock"].asArray(); auto row=stocks.front(); row.set("stock",json::Value(61));
            stocks.front()=row; data.set("marketStock",json::Value(stocks)); corrupt.set("tide",data);
            check(!loadFromJson(corrupt.dump(),loaded,&error),"导入库存超过统一上限60被拒绝");
        }
    }
    {
        auto game=tideFixture(); game.clock.days=game.tide.endDay-1; game.tide.lastProcessedDay=game.clock.days;
        tide::fight(game,0); safeEnemy(game); battleTurn(game,"end");
        check(!game.player.arts.battle && game.tide.settled,"期限到时未击杀对局安全撤出");
        checkEq(game.tide.playerScore,0,"到期撤出不虚构击杀积分");
        check(!game.tide.rewardPaid,"零积分不能仅入场领取保底");
    }
    {
        auto game=tideFixture(); tide::fight(game,0); safeEnemy(game);
        auto saved=json::Value::parse(saveToJson(game)); auto player=saved["player"]; auto arts=player["arts"];
        auto battle=arts["battle"]; battle.set("tideBattlefield",json::Value("0"));
        arts.set("battle",battle); player.set("arts",arts); saved.set("player",player);
        Game loaded; string error;
        check(!loadFromJson(saved.dump(),loaded,&error),"错误类型战场不能被asInt默默降级");
        saved=json::Value::parse(saveToJson(game)); auto oldTide=json::Value(json::Object{});
        oldTide.set("active",json::Value(true)); oldTide.set("monthsLeft",json::Value(12)); oldTide.set("severity",json::Value(1));
        oldTide.set("wavesFought",json::Value(1)); saved.set("tide",oldTide);
        player=saved["player"]; arts=player["arts"]; battle=arts["battle"];
        battle.set("kind",json::Value("hunt")); battle.set("enemy",json::Value("兽潮先锋"));
        battle.asObject().erase("huntFee"); battle.asObject().erase("tideEventId"); battle.asObject().erase("tideBattlefield");
        arts.set("battle",battle); player.set("arts",arts); saved.set("player",player);
        const bool LegacyLoaded=loadFromJson(saved.dump(),loaded,&error);
        check(LegacyLoaded,"旧hunt兽潮在途战斗可迁移："+error);
        check(loaded.player.arts.battle && loaded.player.arts.battle->kind=="tide" &&
            loaded.player.arts.battle->tideEventId==loaded.tide.eventId,"先恢复旧battle来源，再补本期归属");
    }
    {
        auto game=tideFixture(); game.clock.days=359; game.tide.startDay=300; game.tide.endDay=360;
        game.tide.lastProcessedDay=359; tide::fight(game,0);
        auto& battle=*game.player.arts.battle; battle.hp=1; battle.BeastActions.clear(); battle.intentKind="attack"; battle.intentScale=100;
        battle.enemyPower=100000; battleTurn(game,"end");
        check(game.ended && game.ending,"年末致命兽潮回合正常结束人生");
        checkEq(game.clock.days,360,"致命回合只耗一天，不重复扣天");
        checkEq(game.player.age,17,"致命回合跨年同步年龄");
        check(game.ending && game.ending->age==game.player.age && game.ending->year==game.year,"致命回合纪事与结局使用同一年龄/年份");
    }
    {
        auto game=tideFixture(); EnemySpec enemy; enemy.kind="spar"; enemy.name="普通切磋";
        battleStart(game,enemy); safeEnemy(game); const int before=game.clock.days; battleTurn(game,"end");
        checkEq(game.clock.days,before,"普通切磋不使用兽潮日历规则");
    }
}
} // namespace nztest
