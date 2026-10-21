#include "harness.h"
#include "core/simulation/tide_rules.h"
#include "core/tide.h"
#include "core/save.h"
#include "core/json.h"
#include "core/sim.h"
#include "core/interface/views.h"
#include "core/content/npc_combat_profiles.h"
#include <array>
#include <limits>

namespace nztest {
// 用途：验证兽潮奖励与阶梯库存的真实规则；参与定向回归和统一自测。
// 使用：仅构造独立游戏，不读取玩家存档；返回：无，失败记录到测试计数。
void mfRunTideStockRewardTests() {
    using namespace nz;
    std::printf("\n--- 兽潮阶梯库存与排名奖励 ---\n");
    checkEq(tide::rules::rewardStones(9, 1), 45 * 12 * 50, "榜首奖励为入场境界五十年基础俸禄");
    checkEq(tide::rules::rewardStones(3, 3), 16 * 12 * 25, "前三奖励为二十五年基础俸禄");
    checkEq(tide::rules::rewardStones(6, 10), 28 * 12 * 15, "前十奖励为十五年基础俸禄");
    checkEq(tide::rules::rewardStones(0, 11), 8 * 12 * 5, "正积分参战者保底为五年基础俸禄");
    struct StockSample { int Score, Grade, ExpectedCount; };
    for (const auto& Sample : std::array{
        StockSample{0,3,0}, StockSample{299,3,0}, StockSample{300,3,1},
        StockSample{2999,3,9}, StockSample{2999,4,0}, StockSample{3000,3,10},
        StockSample{3000,4,1}, StockSample{3300,3,11}, StockSample{3300,4,1},
        StockSample{3899,4,1}, StockSample{3900,4,2},
        StockSample{5699,4,3}, StockSample{5699,5,0}, StockSample{5700,3,19},
        StockSample{5700,4,4}, StockSample{5700,5,1}, StockSample{6000,5,1},
        StockSample{8399,5,1}, StockSample{8400,5,2},
        StockSample{11400,3,38}, StockSample{11400,4,10}, StockSample{11400,5,3}}) {
        checkEq(tide::rules::stockPerItem(Sample.Score, Sample.Grade), Sample.ExpectedCount,
            std::to_string(Sample.Score) + "分的" + std::to_string(Sample.Grade) + "阶库存符合阶梯且低阶继续累积");
    }
    checkEq(tide::rules::stockPerItem(-1, 3), 0, "负积分不生成免费库存");
    checkEq(tide::rules::stockPerItem(6000, 2), 0, "低阶普通坊市物品不进入战后库存规则");
    checkEq(tide::rules::stockPerItem(6000, 6), 0, "未开放品阶不生成虚构库存");
    checkEq(tide::rules::stockPerItem(std::numeric_limits<int>::max(), 5), 60,
        "积分极值下库存仍有有限上限且不溢出");
    checkEq(tide::rules::NormalNpcBattlePace.BattleChanceDenominator, 6, "普通修士日出战频率提高至六分之一");
    checkEq(tide::rules::EliteNpcBattlePace.BattleChanceDenominator, 4, "精英修士日出战频率提高至四分之一");
    checkEq(tide::rules::mfStockUnlockScore(3), 300, "三阶首次库存门槛三百分");
    checkEq(tide::rules::mfStockUnlockScore(4), 3000, "三阶十份时首次解锁四阶");
    checkEq(tide::rules::mfStockUnlockScore(5), 5700, "五阶保留原解锁门槛，后续供给增速独立计算");
    checkEq(tide::rules::mfRewardRenown(0), 0, "无有效排名不具有声望奖资格");
    checkEq(tide::rules::mfRewardRenown(1), 50, "榜首声望五十");
    checkEq(tide::rules::mfRewardRenown(3), 20, "前三声望二十");
    checkEq(tide::rules::mfRewardRenown(10), 10, "前十声望十");
    checkEq(tide::rules::mfRewardRenown(11), 1, "其余正积分者声望一");
    Game State;
    State.tide.settled = true;
    State.tide.totalScore = 3000;
    tide::initializeMarketStock(State);
    check(!State.tide.marketStock.empty(), "真实期末初始化建立有限库存记录");
    const auto FrozenStock = State.tide.marketStock;
    State.tide.totalScore = 100000;
    tide::initializeMarketStock(State);
    bool StockUnchanged = State.tide.marketStock.size() == FrozenStock.size();
    for (std::size_t StockIndex = 0; StockUnchanged && StockIndex < FrozenStock.size(); ++StockIndex)
        StockUnchanged = State.tide.marketStock[StockIndex].stock == FrozenStock[StockIndex].stock;
    check(StockUnchanged, "已冻结库存不因重复初始化或积分变化重新补货");
    CreateOptions Options;
    Options.seed = 20261014;
    Options.traitsExplicit = true;
    auto SavedState = createGame(Options);
    SavedState.pending.reset();
    SavedState.npcs.clear();
    SavedState.tide.active = true;
    tide::ensureTide(SavedState);
    SavedState.tide.playerJoined = true;
    SavedState.tide.playerScore = 1;
    SavedState.clock.days = SavedState.tide.endDay;
    tide::dayTick(SavedState);
    const auto SavedStones = SavedState.player.stones;
    const auto SavedRenown = SavedState.player.renown;
    Game Restored;
    string Error;
    auto OldSave = json::Value::parse(saveToJson(SavedState));
    OldSave.asObject().at("tide").asObject().erase("rewardRenown");
    check(loadFromJson(OldSave.dump(), Restored, &Error), "旧已结算档缺新声望回执可以正常载入：" + Error);
    checkEq(Restored.tide.RewardRenown, 0, "旧已结算档缺字段不会虚构声望到账");
    tide::settleIfDue(Restored);
    checkEq(Restored.player.renown, SavedRenown, "旧已支付奖励不因新规则重发声望");
    checkEq(Restored.player.stones, SavedStones, "旧已支付奖励不因新规则重发灵石");
    auto InvalidSave = json::Value::parse(saveToJson(SavedState));
    InvalidSave.asObject().at("tide").set("rewardRenown", json::Value(51));
    check(!loadFromJson(InvalidSave.dump(), Restored, &Error), "拒绝超出最高奖档的声望回执");
    checkEq(Restored.player.renown, SavedRenown, "非法导入不部分改变当前声望");

    Game ViewState = SavedState;
    Npc Champion;
    Champion.id = "supply-view-champion";
    Champion.name = "高手展示";
    Champion.sect = "jinhong";
    Champion.dao = "金";
    Champion.combatProfileId = "jinhong-complete";
    Champion.location = ViewState.player.location;
    Npc Ordinary = Champion;
    Ordinary.id = "supply-view-ordinary";
    Ordinary.name = "普通展示";
    Ordinary.combatProfileId.clear();
    ViewState.npcs = {Champion, Ordinary};
    TideParticipant ChampionEntry;
    ChampionEntry.actorId = Champion.id;
    ChampionEntry.name = Champion.name;
    ChampionEntry.sect = Champion.sect;
    ChampionEntry.combatProfileId = Champion.combatProfileId;
    ChampionEntry.elite = true;
    ChampionEntry.score = 27;
    ChampionEntry.entryStage = 9;
    ViewState.tide.participants = {ChampionEntry};
    ViewState.tide.totalScore = 11400;
    const auto ViewStateBefore = saveToJson(ViewState);
    const auto TideView = api::view::tideState(ViewState);
    checkEq(TideView["market"]["stockRules"][1]["incrementScore"].asInt(), 900, "四阶展示读取真实900分增量门槛");
    checkEq(TideView["market"]["stockRules"][2]["incrementScore"].asInt(), 2700, "五阶展示读取真实2700分增量门槛");
    bool ShowsChampion = false;
    for (const auto& Row : TideView["leaderboard"].asArray())
        if (Row["actorId"].asString() == Champion.id)
            ShowsChampion = Row["elite"].asBool() && Row["buildName"].asString() == "金虹弃牌锐势";
    check(ShowsChampion, "金榜显示真实流派高手及中文构筑名");
    json::Value SocialView(json::Object{});
    api::view::appendSocialState(ViewState, SocialView);
    bool NearbyChampion = false, NearbyOrdinary = false;
    for (const auto& Row : SocialView["nearby"].asArray()) {
        if (Row["id"].asString() == Champion.id) NearbyChampion = Row["isChampion"].asBool();
        if (Row["id"].asString() == Ordinary.id) NearbyOrdinary = !Row["isChampion"].asBool();
    }
    check(NearbyChampion && NearbyOrdinary, "当地仙友仅给合法完整构筑高手身份，普通弟子不冒充");
    checkStr(saveToJson(ViewState), ViewStateBefore, "高阶库存及高手展示查询不修改存档状态或随机数");
}
} // namespace nztest
