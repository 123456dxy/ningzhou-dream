#include "harness.h"
#include "core/api.h"
#include "core/api_exploration.h"
#include "core/battle.h"
#include "core/content/game_ids.h"
#include "core/crafting/green_bottle.h"
#include "core/dungeon.h"
#include "core/exploration/dungeon_card_preferences.h"
#include "core/exploration/dungeon_encounters.h"
#include "core/exploration/dungeon_events.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <optional>

namespace nztest {
using namespace nz;
namespace {
// 用途：创建不读写玩家文件的秘境接口夹具；流程：偏好定向测试；用法：独立Session调用；返回：具备真实入口的游戏快照。
Game mfCardPreferenceFixture(int Seed = 20261016) {
    CreateOptions Options; Options.seed = Seed; Options.name = "秘境发卡验收";
    Options.traitsExplicit = true;
    Game State = createGame(Options);
    State.npcs.clear(); State.pending.reset(); State.job.reset(); State.checkpoint = AutoCheckpoint{};
    State.player.stage = State.player.highestStage = 6;
    State.player.lifespan = 500; State.player.health = 100; State.player.stones = 100000;
    State.player.arts.spells = {"gold", "water", "gather"};
    State.player.arts.knownSpells = State.player.arts.spells;
    State.dungeons = DungeonWorld{}; ensureDungeons(State);
    State.player.location = State.dungeons.sites.front().location;
    return State;
}
// 类型：测试会话借用范围；不拥有服务或文件，退出恢复默认绑定。
struct CardPreferenceSessionBinding {
    // 用途：绑定独立测试角色；流程：真实API分派；用法：传作用域内Session；返回：无。
    explicit CardPreferenceSessionBinding(api::Session& Session) { api::bindSession(&Session); }
    // 用途：恢复默认会话；流程：测试清理；用法：作用域退出自动调用；返回：无。
    ~CardPreferenceSessionBinding() { api::bindSession(nullptr); }
};
// 用途：提交真实接口正文；流程：偏好入口测试；用法：传路径及JSON；返回：响应对象，无网络服务。
json::Value mfCardPreferenceRequest(const string& Path, const json::Value& Body) {
    http::Request Request; Request.method = "POST"; Request.path = "/api/" + Path; Request.body = Body.dump();
    return json::Value::parse(api::handle(Request).body);
}
// 用途：生成协议偏好而不借用新增产品类型；流程：新旧库均可编译的契约测试；用法：传模式与稳定道系；返回：JSON对象。
json::Value mfPreference(const string& Mode, const vector<string>& Daos = {}) {
    json::Value Preference(json::Object{}); Preference.set("mode", Mode);
    json::Array Choices; for (const auto& Dao : Daos) Choices.emplace_back(Dao);
    Preference.set("daos", std::move(Choices)); return Preference;
}
// 用途：通过真实API进入普通秘境；流程：偏好冻结；用法：传独立Game及可选协议对象；返回：是否成功并提交会话快照。
bool mfEnterWithPreference(Game& State, const std::optional<json::Value>& Preference = std::nullopt) {
    api::Session Session; Session.g = State; Session.started = true;
    CardPreferenceSessionBinding Binding(Session);
    json::Value Body(json::Object{}); Body.set("id", State.dungeons.sites.front().id);
    if (Preference) Body.set("cardRewardPreference", *Preference);
    const auto Response = mfCardPreferenceRequest("dungeon/enter", Body);
    State = std::move(Session.g);
    return Response["ok"].asBool() && State.dungeons.run.has_value();
}
// 用途：读取冻结探索协议；流程：持久化断言；用法：传活动快照；返回：独立JSON，不改变状态。
json::Value mfStoredRun(const Game& State) {
    return json::Value::parse(saveToJson(State))["dungeons"]["run"];
}
// 用途：确认完整存档往返；流程：新偏好和旧在途兼容；用法：传有效快照与名称；返回：是否加载成功，失败不继续访问run。
bool mfPreferenceRoundtrip(const Game& State, const string& Label) {
    const string Snapshot = saveToJson(State); Game Restored; string Error;
    const bool Loaded = loadFromJson(Snapshot, Restored, &Error);
    check(Loaded, Label + "可加载：" + Error);
    if (Loaded) checkStr(saveToJson(Restored), Snapshot, Label + "往返保持冻结选择和RNG");
    return Loaded;
}
// 用途：选择真实房间并以显式胜利夹具收口；流程：仅核验掉落，不测自然胜率；用法：传独立Game；返回：是否仍在探索中。
bool mfFinishPreferenceBattle(Game& State) {
    if (!State.player.arts.battle) return State.dungeons.run.has_value();
    State.player.arts.battle->hp = State.player.arts.battle->max;
    State.player.arts.battle->enemyHp = 0; battleFinish(State, true);
    return State.dungeons.run.has_value() && !State.player.arts.battle;
}
// 用途：沿实际连线寻找目标房间；流程：商人/事件场景覆盖；用法：传探索与只读匹配器；返回：目标之前的完整节点路径。
vector<string> mfPreferencePath(const DungeonRun& Run, const std::function<bool(const DungeonNode&)>& Matches) {
    std::deque<vector<string>> Pending;
    for (const auto& Node : Run.nodes) if (dungeonNodeAvailable(Run, Node.id)) Pending.push_back({Node.id});
    while (!Pending.empty()) {
        auto Path = std::move(Pending.front()); Pending.pop_front();
        const auto Found = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const auto& Node) { return Node.id == Path.back(); });
        if (Found == Run.nodes.end()) continue;
        if (Matches(*Found)) return Path;
        for (const auto& Next : Found->next) { auto Longer = Path; Longer.push_back(Next); Pending.push_back(std::move(Longer)); }
    }
    return {};
}
// 用途：逐层走到目标，途中真实跳过奖励；流程：场景夹具；用法：传合法路线；返回：成功停在目标房间，未合成节点或奖励。
bool mfWalkPreferencePath(Game& State, const vector<string>& Path) {
    if (Path.empty()) return false;
    for (const auto& NodeId : Path) {
        if (!dungeonChoose(State, NodeId).empty() || !State.dungeons.run) return false;
        if (NodeId == Path.back()) return true;
        if (!mfFinishPreferenceBattle(State)) return false;
        if (State.dungeons.run->phase == "reward" && !dungeonClaim(State, "skip").empty()) return false;
        if (State.dungeons.run->phase != "route") return false;
    }
    return false;
}
// 用途：找到真实可达的指定事件或商人；流程：少量稳定种子覆盖三种发卡事件；用法：传模式、房间和可选主题；返回：成功的独立快照。
bool mfPreferenceRoomFixture(Game& Output, const string& Mode, const string& RoomType,
                             std::optional<DungeonEventKind> EventKind = std::nullopt) {
    for (int SeedOffset = 0; SeedOffset < 12; ++SeedOffset) {
        Game Candidate = mfCardPreferenceFixture(20261016 + SeedOffset);
        if (!mfEnterWithPreference(Candidate, mfPreference(Mode, Mode == "selected" ? vector<string>{"metal"} : vector<string>{}))) continue;
        Candidate.dungeons.run->stones = 10000;
        const auto Path = mfPreferencePath(*Candidate.dungeons.run, [&](const auto& Node) {
            return Node.type == RoomType && (!EventKind || mfDescribeDungeonEvent(*Candidate.dungeons.run, Node).Kind == *EventKind);
        });
        if (!Path.empty() && mfWalkPreferencePath(Candidate, Path)) { Output = std::move(Candidate); return true; }
    }
    return false;
}
// 用途：以稳定道系解释单系测试选择；流程：新奖励池和真实入牌断言；用法：仅对新增神通调用；返回：是否满足单选金系，不约束入场已有牌。
bool mfSelectedCardAllowed(const string& SpellId) {
    const auto* Spell = battleSpell(SpellId);
    if (!Spell) return false;
    const auto Dao = content::parseDaoId(Spell->dao);
    return Dao && content::stableId(*Dao) == "metal";
}
// 用途：核验当前发卡选项及实际领取；流程：普通战/商人/三类事件；用法：传冻结房间与策略；返回：无，每个可用选项在独立副本上兑现。
void mfCheckPreferenceOffers(const Game& State, const string& Mode, const string& Label) {
    const auto& Run = *State.dungeons.run; int CardOfferCount = 0;
    for (const auto& Reward : Run.rewards) {
        const bool IsCard = Reward.kind == "card" || Reward.kind == "buyCard" ||
            (Reward.kind == "eventChoice" && battleSpell(Reward.spell));
        if (!IsCard) continue;
        ++CardOfferCount;
        if (Mode == "selected") check(mfSelectedCardAllowed(Reward.spell), Label + "冻结选项只来自单选金系");
        Game Trial = State; const auto DeckSizeBefore = Trial.dungeons.run->deck.size();
        const string Error = dungeonClaim(Trial, Reward.id);
        check(Error.empty(), Label + "合法临时发卡选项实际兑现：" + Error);
        if (!Error.empty() || !Trial.dungeons.run) continue;
        const auto& DeckAfter = Trial.dungeons.run->deck;
        check(DeckAfter.size() > DeckSizeBefore, Label + "领取真实增加临时实例");
        for (size_t CardIndex = DeckSizeBefore; CardIndex < DeckAfter.size(); ++CardIndex)
            if (Mode == "selected") check(mfSelectedCardAllowed(DeckAfter[CardIndex].spell), Label + "新增实例符合偏好");
        check(mfPreferenceRoundtrip(Trial, Label + "领取后"), Label + "领取后完整状态合法");
    }
    if (Mode == "none") checkEq(CardOfferCount, 0, Label + "不生成任何临时发卡选项");
    else if (Label == "普通战" || Label == "游商") check(CardOfferCount > 0, Label + "有可用选中系候选");
    Game Skipped = State; const size_t DeckSize = Run.deck.size();
    check(dungeonClaim(Skipped, "skip").empty() && Skipped.dungeons.run && Skipped.dungeons.run->phase == "route", Label + "无卡或不想领仍可继续");
    if (Skipped.dungeons.run) checkEq(Skipped.dungeons.run->deck.size(), DeckSize, Label + "跳过不污染临时牌组");
}
// 用途：构造只缺新增偏好字段的合法旧在途JSON；流程：规则0兼容；用法：传all冻结快照；返回：独立旧档，保留原有奖励和路线。
json::Value mfLegacyPreferenceSave(const Game& State) {
    auto Saved = json::Value::parse(saveToJson(State));
    auto& Run = Saved.asObject().at("dungeons").asObject().at("run");
    Run.asObject().erase("cardRewardRulesVersion"); Run.asObject().erase("cardRewardPreference");
    return Saved;
}
}
// 用途：验证秘境临时卡牌偏好的真实接口与持久状态；流程：独立/常规回归；用法：harness调用；返回：无，记录断言。
void mfRunDungeonCardPreferenceTests() {
    std::printf("\n--- 秘境临时卡偏好、冻结与隐藏遭遇 ---\n");
    {
    api::Session Session; Session.g = mfCardPreferenceFixture(); Session.started = true;
    CardPreferenceSessionBinding Binding(Session);
    json::Value Preference(json::Object{}); Preference.set("mode", "none"); Preference.set("daos", json::Array{});
    json::Value Body(json::Object{}); Body.set("id", Session.g.dungeons.sites.front().id);
    Body.set("cardRewardPreference", Preference);
    const auto Response = mfCardPreferenceRequest("dungeon/enter", Body);
    check(Response["ok"].asBool() && Session.g.dungeons.run.has_value(), "真实入口接受不出临时牌偏好");
    const auto Saved = json::Value::parse(saveToJson(Session.g));
    checkStr(Saved["dungeons"]["run"]["cardRewardPreference"]["mode"].asString(), "none", "入口偏好不能被API忽略且必须冻结到run");
    checkEq(Saved["dungeons"]["run"]["cardRewardRulesVersion"].asInt(), 1, "新探索冻结临时奖励规则版本1");
    }
    {
        Game Default = mfCardPreferenceFixture();
        check(mfEnterWithPreference(Default), "未指定偏好的新入口保持默认全部");
        if (Default.dungeons.run) {
            const auto Run = mfStoredRun(Default);
            checkStr(Run["cardRewardPreference"]["mode"].asString(), "all", "新入场默认all");
            checkEq(Run["cardRewardRulesVersion"].asInt(), 1, "默认入场也冻结规则1");
            check(Run["cardRewardPreference"]["daos"].isArray() && Run["cardRewardPreference"]["daos"].asArray().empty(), "默认all没有暗中选中道系");
            mfPreferenceRoundtrip(Default, "默认all");
        }
        for (const string Mode : {"all", "none", "selected"}) {
            Game State = mfCardPreferenceFixture();
            check(mfEnterWithPreference(State, mfPreference(Mode, Mode == "selected" ? vector<string>{"metal"} : vector<string>{})), "真实入口接受" + Mode);
            if (!State.dungeons.run || !Default.dungeons.run) continue;
            const auto Stored = mfStoredRun(State);
            checkStr(Stored["cardRewardPreference"]["mode"].asString(), Mode, "入场策略冻结为" + Mode);
            checkStr(Stored["deck"].dump(), mfStoredRun(Default)["deck"].dump(), Mode + "不删除初始带入的中性牌和装备神通");
            checkStr(Stored["bossRewards"].dump(), mfStoredRun(Default)["bossRewards"].dump(), Mode + "不改变永久终奖三选一");
            mfPreferenceRoundtrip(State, Mode + "新探索");
        }
    }
    {
        vector<json::Value> InvalidPreferences = {
            json::Value(), json::Value(true), json::Value(1), json::Value("none"), json::Value(json::Array{}),
            json::Value(json::Object{}), mfPreference("unknown"), mfPreference("selected"),
            mfPreference("selected", {"unknown"}), mfPreference("selected", {"metal", "metal"}),
            mfPreference("selected", {"metal", "water"}),
            mfPreference("all", {"metal"}), mfPreference("none", {"water"})
        };
        auto WrongMode = mfPreference("all"); WrongMode.set("mode", 1); InvalidPreferences.push_back(WrongMode);
        for (const auto& WrongDaos : {json::Value(), json::Value("metal"), json::Value(true), json::Value(2), json::Value(json::Object{}), json::Value(json::Array{json::Value(1)})}) {
            auto Preference = mfPreference("selected"); Preference.set("daos", WrongDaos); InvalidPreferences.push_back(std::move(Preference));
        }
        for (size_t CaseIndex = 0; CaseIndex < InvalidPreferences.size(); ++CaseIndex) {
            api::Session Session; Session.g = mfCardPreferenceFixture(); Session.started = true;
            CardPreferenceSessionBinding Binding(Session); const string Before = saveToJson(Session.g);
            json::Value Body(json::Object{}); Body.set("id", Session.g.dungeons.sites.front().id);
            Body.set("cardRewardPreference", InvalidPreferences[CaseIndex]);
            const auto Response = mfCardPreferenceRequest("dungeon/enter", Body);
            check(!Response["error"].asString().empty(), "非法偏好明确拒绝案例" + std::to_string(CaseIndex));
            checkStr(saveToJson(Session.g), Before, "非法入口不变更资源/在途/RNG案例" + std::to_string(CaseIndex));
        }
        api::Session Session; Session.g = mfCardPreferenceFixture(); Session.started = true;
        CardPreferenceSessionBinding Binding(Session); const string Before = saveToJson(Session.g);
        http::Request Request; Request.method = "POST"; Request.path = "/api/dungeon/enter";
        Request.body = "{\"id\":\"" + Session.g.dungeons.sites.front().id + "\",\"cardRewardPreference\":{\"mode\":\"selected\",\"daos\":[NaN]}}";
        const auto Response = json::Value::parse(api::handle(Request).body);
        check(!Response["error"].asString().empty(), "NaN协议输入明确拒绝");
        checkStr(saveToJson(Session.g), Before, "NaN拒绝不留下探索或随机数变化");
    }
    {
        DungeonCardRewardPreference Multiple;
        Multiple.Mode = DungeonCardRewardMode::Selected;
        Multiple.Daos = {content::DaoId::Metal, content::DaoId::Water};
        Game Ordinary = mfCardPreferenceFixture(); const string OrdinaryBefore = saveToJson(Ordinary);
        check(!mfEnterDungeonWithCardRewards(Ordinary, Ordinary.dungeons.sites.front().id, Multiple).empty(), "普通typed新入场拒绝多系");
        checkStr(saveToJson(Ordinary), OrdinaryBefore, "普通typed多系拒绝不改入口/资源/RNG");
        Game Bottle = mfCardPreferenceFixture(); Bottle.player.traits = {"greenBottle"};
        Bottle.bonds.origins.emplace_back("greenBottle", ""); Bottle.player.arts.GreenBottleGrade = 3;
        Bottle.player.cave = Bottle.player.location = "wuling"; Bottle.player.caveTier = 2;
        const string BottleBefore = saveToJson(Bottle);
        check(!crafting::mfEnterGreenBottleDungeon(Bottle, 4, 3, Multiple).empty(), "绿瓶typed新入场拒绝多系");
        checkStr(saveToJson(Bottle), BottleBefore, "绿瓶typed多系拒绝不生成入口或扣资源");
        check(!mfOpenBottleMaterialDungeon(Bottle, 4, Multiple).empty(), "底层开绿瓶秘境typed入口同样拒绝多系");
        checkStr(saveToJson(Bottle), BottleBefore, "底层typed多系拒绝保持完整状态");
        DungeonCardRewardPreference Single; Single.Mode = DungeonCardRewardMode::Selected;
        Single.Daos = {content::DaoId::Metal};
        check(crafting::mfEnterGreenBottleDungeon(Bottle, 4, 3, Single).empty(), "绿瓶typed恰选单金可正常进入");
        if (Bottle.dungeons.run) {
            const auto Stored = mfStoredRun(Bottle);
            checkStr(Stored["cardRewardPreference"]["daos"].dump(), json::Value(json::Array{json::Value("metal")}).dump(), "绿瓶typed冻结恰好一个金系");
            mfPreferenceRoundtrip(Bottle, "单系绿瓶typed");
        }
    }
    {
        for (const string Mode : {"selected", "none"}) {
            Game Ordinary = mfCardPreferenceFixture();
            check(mfEnterWithPreference(Ordinary, mfPreference(Mode, Mode == "selected" ? vector<string>{"metal"} : vector<string>{})), Mode + "可进入普通战测试");
            if (Ordinary.dungeons.run) {
                Ordinary.dungeons.run->stones = 10000;
                const auto Path = mfPreferencePath(*Ordinary.dungeons.run, [](const auto& Node) { return Node.type == "battle"; });
                const bool Arrived = mfWalkPreferencePath(Ordinary, Path) && mfFinishPreferenceBattle(Ordinary);
                check(Arrived, "普通战实际胜利产生待选阶段");
                if (Arrived) mfCheckPreferenceOffers(Ordinary, Mode, "普通战");
            }
            Game Merchant; const bool MerchantFound = mfPreferenceRoomFixture(Merchant, Mode, "merchant");
            check(MerchantFound, Mode + "真实连线可到游商");
            if (MerchantFound) mfCheckPreferenceOffers(Merchant, Mode, "游商");
            for (const auto Kind : {DungeonEventKind::Reflection, DungeonEventKind::Tablet, DungeonEventKind::Remains}) {
                Game Event; const bool EventFound = mfPreferenceRoomFixture(Event, Mode, "event", Kind);
                const string Label = Kind == DungeonEventKind::Reflection ? "幻镜" : Kind == DungeonEventKind::Tablet ? "道碑" : "遗蜕";
                check(EventFound, Mode + "真实连线可到" + Label);
                if (!EventFound) continue;
                check(mfDungeonEventStateError(Event).empty(), Label + "过滤后仍符合完整冻结校验");
                mfPreferenceRoundtrip(Event, Label + "领取前");
                mfCheckPreferenceOffers(Event, Mode, Label);
            }
        }
    }
    {
        Game State = mfCardPreferenceFixture();
        check(mfEnterWithPreference(State, mfPreference("none")), "无临时卡策略可完整走通路线");
        if (State.dungeons.run) {
            const string InitialDeck = mfStoredRun(State)["deck"].dump();
            const string InitialBossRewards = mfStoredRun(State)["bossRewards"].dump();
            int Guard = 0;
            while (State.dungeons.run && !State.dungeons.run->cleared && ++Guard < 70) {
                if (State.player.arts.battle) { if (!mfFinishPreferenceBattle(State)) break; }
                else if (State.dungeons.run->phase == "reward") { if (!dungeonClaim(State, "skip").empty()) break; }
                else {
                    const auto& Run = *State.dungeons.run;
                    const auto Node = std::find_if(Run.nodes.begin(), Run.nodes.end(), [&](const auto& Entry) { return dungeonNodeAvailable(Run, Entry.id); });
                    if (Node == Run.nodes.end() || !dungeonChoose(State, Node->id).empty()) break;
                }
            }
            check(State.dungeons.run && State.dungeons.run->cleared, "无临时卡策略可越过空奖励房间抵达终点");
            if (State.dungeons.run && State.dungeons.run->cleared) {
                checkStr(mfStoredRun(State)["deck"].dump(), InitialDeck, "整次none探索没有添加任何沿途卡牌");
                checkStr(mfStoredRun(State)["rewards"].dump(), InitialBossRewards, "none仍展示原永久终奖三选一");
                const auto Reward = std::find_if(State.dungeons.run->bossRewards.begin(), State.dungeons.run->bossRewards.end(), [](const auto& Entry) { return Entry.kind == "learnSpell"; });
                check(Reward != State.dungeons.run->bossRewards.end(), "永久神通未被不出临时卡设置移除");
                if (Reward != State.dungeons.run->bossRewards.end()) {
                    const string RewardId = Reward->id, SpellId = Reward->spell;
                    check(dungeonClaim(State, RewardId).empty(), "none策略实际领取永久神通");
                    check(std::find(State.player.arts.knownSpells.begin(), State.player.arts.knownSpells.end(), SpellId) != State.player.arts.knownSpells.end(), "永久奖励进入真实已学神通");
                    checkStr(mfStoredRun(State)["deck"].dump(), InitialDeck, "永久领悟不会强制塞入本次临时卡组");
                    mfPreferenceRoundtrip(State, "none完整通关");
                }
            }
        }
    }
    {
        for (const bool IsEvent : {false, true}) {
            Game Frozen; bool Prepared = false;
            if (IsEvent) Prepared = mfPreferenceRoomFixture(Frozen, "all", "event", DungeonEventKind::Tablet);
            else {
                Frozen = mfCardPreferenceFixture(); Prepared = mfEnterWithPreference(Frozen, mfPreference("all"));
                if (Prepared) {
                    const auto Path = mfPreferencePath(*Frozen.dungeons.run, [](const auto& Node) { return Node.type == "battle"; });
                    Prepared = mfWalkPreferencePath(Frozen, Path) && mfFinishPreferenceBattle(Frozen);
                }
            }
            check(Prepared, "构造旧在途冻结奖励夹具");
            if (!Prepared) continue;
            const string Rewards = mfStoredRun(Frozen)["rewards"].dump(), Deck = mfStoredRun(Frozen)["deck"].dump();
            Game Legacy; string Error; const bool Loaded = loadFromJson(mfLegacyPreferenceSave(Frozen).dump(), Legacy, &Error);
            check(Loaded, "旧缺偏好字段在途奖励可加载：" + Error);
            if (!Loaded) continue;
            const auto Stored = mfStoredRun(Legacy);
            checkEq(Stored["cardRewardRulesVersion"].asInt(-1), 0, "旧缺字段探索保留规则0");
            checkStr(Stored["cardRewardPreference"]["mode"].asString(), "all", "旧缺字段策略默认all");
            checkStr(Stored["rewards"].dump(), Rewards, "旧当前奖励不被过滤/刷新/重抽");
            checkStr(Stored["deck"].dump(), Deck, "旧已得卡牌不追溯清除");
            mfPreferenceRoundtrip(Legacy, "旧在途奖励");
            const auto FirstCard = std::find_if(Legacy.dungeons.run->rewards.begin(), Legacy.dungeons.run->rewards.end(), [](const auto& Reward) {
                return Reward.kind == "card" || (Reward.kind == "eventChoice" && battleSpell(Reward.spell));
            });
            check(FirstCard != Legacy.dungeons.run->rewards.end(), "旧全池卡牌选项仍存在");
            if (FirstCard != Legacy.dungeons.run->rewards.end()) {
                const string RewardId = FirstCard->id;
                check(dungeonClaim(Legacy, RewardId).empty(), "旧规则当前冻结卡牌仍可真实领取");
            }
        }
    }
    {
        bool CheckedWaterReward = false, CheckedEvent = false;
        for (int SeedOffset = 0; SeedOffset < 12 && (!CheckedWaterReward || !CheckedEvent); ++SeedOffset) {
            Game Single = mfCardPreferenceFixture(20261016 + SeedOffset);
            if (!mfEnterWithPreference(Single, mfPreference("selected", {"metal"}))) continue;
            auto Saved = json::Value::parse(saveToJson(Single));
            Saved.asObject().at("dungeons").asObject().at("run").set("cardRewardPreference", mfPreference("selected", {"metal", "water"}));
            Game Archived; string Error; const bool Loaded = loadFromJson(Saved.dump(), Archived, &Error);
            check(Loaded, "此前已冻结多系探索仍可读档：" + Error);
            if (!Loaded) break;
            Archived.dungeons.run->stones = 10000;
            checkEq(mfStoredRun(Archived)["cardRewardRulesVersion"].asInt(), 1, "旧多系在途继续使用原规则1，不额外迁移版本");
            checkEq(mfStoredRun(Archived)["cardRewardPreference"]["daos"].asArray().size(), 2, "旧多系在途不截断成单系");
            if (!CheckedWaterReward) {
                Game Ordinary = Archived;
                const auto Path = mfPreferencePath(*Ordinary.dungeons.run, [](const auto& Node) { return Node.type == "battle"; });
                if (mfWalkPreferencePath(Ordinary, Path) && mfFinishPreferenceBattle(Ordinary)) {
                    const auto Reward = std::find_if(Ordinary.dungeons.run->rewards.begin(), Ordinary.dungeons.run->rewards.end(), [](const auto& Entry) {
                        const auto* Spell = battleSpell(Entry.spell);
                        return Entry.kind == "card" && Spell && content::parseDaoId(Spell->dao) == content::DaoId::Water;
                    });
                    if (Reward != Ordinary.dungeons.run->rewards.end()) {
                        const string RewardId = Reward->id, SpellId = Reward->spell;
                        const string FrozenRewards = mfStoredRun(Ordinary)["rewards"].dump();
                        Game Restored; const bool RestoredOk = loadFromJson(saveToJson(Ordinary), Restored, &Error);
                        check(RestoredOk, "旧多系已生成水系奖励可往返：" + Error);
                        if (RestoredOk) {
                            checkStr(mfStoredRun(Restored)["rewards"].dump(), FrozenRewards, "旧多系当前冻结奖励不重抽或删减");
                            const string BeforeQuery = saveToJson(Restored); (void)api::dungeonView(Restored);
                            checkStr(saveToJson(Restored), BeforeQuery, "旧多系查询不会隐式缩窄奖励池");
                            const size_t BeforeCount = Restored.dungeons.run->deck.size();
                            check(dungeonClaim(Restored, RewardId).empty(), "旧多系当前水系奖励仍可真实领取");
                            check(Restored.dungeons.run && Restored.dungeons.run->deck.size() == BeforeCount + 1 && Restored.dungeons.run->deck.back().spell == SpellId, "兼容领取增加原水系实例，不替换为金系");
                            mfPreferenceRoundtrip(Restored, "旧多系普通奖励领取后");
                            CheckedWaterReward = true;
                        }
                    }
                }
            }
            if (!CheckedEvent) {
                Game Event = Archived;
                const auto Path = mfPreferencePath(*Event.dungeons.run, [&](const auto& Node) {
                    return Node.type == "event" && mfDescribeDungeonEvent(*Event.dungeons.run, Node).Kind == DungeonEventKind::Tablet;
                });
                if (mfWalkPreferencePath(Event, Path)) {
                    const auto Reward = std::find_if(Event.dungeons.run->rewards.begin(), Event.dungeons.run->rewards.end(), [](const auto& Entry) {
                        return Entry.kind == "eventChoice" && battleSpell(Entry.spell);
                    });
                    if (Reward != Event.dungeons.run->rewards.end()) {
                        const string RewardId = Reward->id, FrozenRewards = mfStoredRun(Event)["rewards"].dump();
                        Game Restored; const bool RestoredOk = loadFromJson(saveToJson(Event), Restored, &Error);
                        check(RestoredOk, "旧多系事件的完整冻结集合仍可读档：" + Error);
                        if (RestoredOk) {
                            checkStr(mfStoredRun(Restored)["rewards"].dump(), FrozenRewards, "旧多系事件不按新入场单系要求重写");
                            check(mfDungeonEventStateError(Restored).empty(), "旧多系事件仍通过完整集合校验");
                            check(dungeonClaim(Restored, RewardId).empty(), "旧多系事件发卡实际兑现");
                            mfPreferenceRoundtrip(Restored, "旧多系事件领取后");
                            CheckedEvent = true;
                        }
                    }
                }
            }
        }
        check(CheckedWaterReward, "已覆盖旧多系水牌冻结奖励的真实兑现");
        check(CheckedEvent, "已覆盖旧多系完整事件冻结和兑现");
    }
    {
        Game State = mfCardPreferenceFixture(); check(mfEnterWithPreference(State, mfPreference("selected", {"metal"})), "建立偏好保存校验基线");
        if (State.dungeons.run) {
            const auto Base = json::Value::parse(saveToJson(State));
            vector<json::Value> InvalidSaves;
            for (const auto& BadVersion : {json::Value(-1), json::Value(2), json::Value(0.5), json::Value("1"), json::Value(true), json::Value()}) {
                auto Saved = Base; Saved.asObject().at("dungeons").asObject().at("run").set("cardRewardRulesVersion", BadVersion); InvalidSaves.push_back(std::move(Saved));
            }
            for (const auto& BadPreference : {mfPreference("selected"), mfPreference("selected", {"metal", "metal"}), mfPreference("selected", {"not-a-dao"}), mfPreference("all", {"metal"}), json::Value()}) {
                auto Saved = Base; Saved.asObject().at("dungeons").asObject().at("run").set("cardRewardPreference", BadPreference); InvalidSaves.push_back(std::move(Saved));
            }
            auto MissingPreference = Base; MissingPreference.asObject().at("dungeons").asObject().at("run").asObject().erase("cardRewardPreference"); InvalidSaves.push_back(std::move(MissingPreference));
            auto MissingVersion = Base; MissingVersion.asObject().at("dungeons").asObject().at("run").asObject().erase("cardRewardRulesVersion"); InvalidSaves.push_back(std::move(MissingVersion));
            auto Downgraded = Base; Downgraded.asObject().at("dungeons").asObject().at("run").set("cardRewardRulesVersion", 0); InvalidSaves.push_back(std::move(Downgraded));
            for (size_t CaseIndex = 0; CaseIndex < InvalidSaves.size(); ++CaseIndex) {
                Game Target = State; const string Before = saveToJson(Target); string Error;
                check(!loadFromJson(InvalidSaves[CaseIndex].dump(), Target, &Error), "非法持久偏好或规则拒绝案例" + std::to_string(CaseIndex));
                checkStr(saveToJson(Target), Before, "非法导入不覆盖当前探索案例" + std::to_string(CaseIndex));
            }
            const string Before = saveToJson(State);
            for (int QueryIndex = 0; QueryIndex < 3; ++QueryIndex) (void)api::dungeonView(State);
            checkStr(saveToJson(State), Before, "偏好/奖励查询不改RNG、世界时间、NPC或冻结状态");
            const auto View = api::dungeonView(State);
            for (const auto& NodeView : View["run"]["nodes"].asArray()) {
                if (NodeView["visited"].asBool()) continue;
                const auto Node = std::find_if(State.dungeons.run->nodes.begin(), State.dungeons.run->nodes.end(), [&](const auto& Entry) { return Entry.id == NodeView["id"].asString(); });
                if (Node == State.dungeons.run->nodes.end()) continue;
                const auto Encounter = mfDescribeDungeonEncounter(State, *State.dungeons.run, *Node);
                if (!Encounter.Enabled) continue;
                check(NodeView["encounterName"].asString().empty() && NodeView["encounterHint"].asString().empty(), "未触发战斗不提前返回怪物名称与机制");
                check(NodeView["name"].asString() != Encounter.Name && NodeView["text"].asString().find(Encounter.Name) == string::npos, "通用name/text也不泄漏具体怪物");
            }
            const auto Path = mfPreferencePath(*State.dungeons.run, [](const auto& Node) { return Node.type == "battle"; });
            check(mfWalkPreferencePath(State, Path), "选择房间后触发真实遭遇");
            if (State.dungeons.run && State.player.arts.battle) {
                const auto Triggered = api::dungeonView(State);
                const auto Current = std::find_if(Triggered["run"]["nodes"].asArray().begin(), Triggered["run"]["nodes"].asArray().end(), [&](const auto& Node) { return Node["id"].asString() == State.dungeons.run->current; });
                check(Current != Triggered["run"]["nodes"].asArray().end() && !(*Current)["encounterName"].asString().empty() && !(*Current)["encounterHint"].asString().empty(), "已触发当前战斗可显示真实名称与机制");
            }
        }
    }
    {
        api::Session Session; Session.g = mfCardPreferenceFixture(); Session.started = true;
        Session.g.player.traits = {"greenBottle"}; Session.g.player.arts.GreenBottleGrade = 3;
        Session.g.bonds.origins.emplace_back("greenBottle", "");
        Session.g.player.cave = Session.g.player.location = "wuling"; Session.g.player.caveTier = 2;
        CardPreferenceSessionBinding Binding(Session);
        json::Value Body(json::Object{}); Body.set("grade", 4); Body.set("expectedGrade", 3); Body.set("cardRewardPreference", mfPreference("none"));
        const string Before = saveToJson(Session.g);
        Body.set("cardRewardPreference", mfPreference("selected", {"metal", "water"}));
        const auto Rejected = mfCardPreferenceRequest("cave/green-bottle/dungeon", Body);
        check(!Rejected["error"].asString().empty(), "绿瓶API新入场明确拒绝多系");
        checkStr(saveToJson(Session.g), Before, "绿瓶API多系拒绝不生成入口/推进序号/改变RNG");
        Body.set("cardRewardPreference", mfPreference("none"));
        const auto Response = mfCardPreferenceRequest("cave/green-bottle/dungeon", Body);
        check(Response["ok"].asBool() && Session.g.dungeons.run && Session.g.dungeons.run->type == "bottle", "绿瓶外层none偏好实际进入专属秘境");
        if (Session.g.dungeons.run) {
            const auto Stored = mfStoredRun(Session.g);
            checkStr(Stored["cardRewardPreference"]["mode"].asString(), "none", "绿瓶入口不会丢弃外层none设置");
            checkEq(Stored["cardRewardRulesVersion"].asInt(), 1, "绿瓶新探索同样冻结规则1");
            const auto Path = mfPreferencePath(*Session.g.dungeons.run, [](const auto& Node) { return Node.type == "battle"; });
            if (mfWalkPreferencePath(Session.g, Path) && mfFinishPreferenceBattle(Session.g)) mfCheckPreferenceOffers(Session.g, "none", "绿瓶普通战");
            mfPreferenceRoundtrip(Session.g, "绿瓶none探索");
        }
    }
}
}
