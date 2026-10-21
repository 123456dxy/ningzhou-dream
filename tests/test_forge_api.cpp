#include "harness.h"
#include "core/save.h"
#include "core/sim.h"
#include "core/json.h"
#include "core/api.h"
#include "core/battle.h"
#include "core/interface/forge_api.h"
#include "core/interface/views.h"
#include "core/content/forge_content.h"
#include <algorithm>
#include <chrono>
#include <filesystem>

namespace nztest {
namespace {
using namespace nz;
json::Value mfForgeInput() {
    json::Value Input(json::Object{}); Input.set("version", 2); Input.set("shape", "box");
    Input.set("main", "iron"); Input.set("effect", "redraw"); Input.set("inscription", "");
    json::Array Materials; for (int Index = 0; Index < 10; ++Index) Materials.emplace_back("iron");
    Input.set("materials", std::move(Materials)); return Input;
}
void mfExpectRejectedSave(const json::Value& Document, Game& Destination, const string& Label) {
    const string Before = saveToJson(Destination); string Error;
    check(!loadFromJson(Document.dump(), Destination, &Error) && !Error.empty(), Label);
    checkStr(saveToJson(Destination), Before, Label + "不覆盖原状态");
}

struct ForgeStatusApiSession {
    api::Session SessionState;
    string PreviousSaveDirectory = defaultSaveDir();
    std::filesystem::path SaveDirectory = std::filesystem::temp_directory_path() /
        ("ningzhou-status-api-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    ForgeStatusApiSession() {
        std::filesystem::create_directories(SaveDirectory);
        setSaveDirOverride(SaveDirectory.string());
        SessionState.started = true;
        api::bindSession(&SessionState);
    }
    ~ForgeStatusApiSession() {
        api::bindSession(nullptr);
        setSaveDirOverride(PreviousSaveDirectory);
        std::error_code Error;
        std::filesystem::remove_all(SaveDirectory, Error);
    }
};

Game mfStatusApiGame(const string& Effect) {
    CreateOptions Options; Options.name = "定向清除接口隔离角色"; Options.seed = 20261018;
    Game State = createGame(Options);
    State.pending.reset(); State.job.reset(); State.npcs.clear();
    auto& Actor = State.player;
    Actor.arts.gear.clear(); Actor.arts.equipped.clear(); Actor.arts.spells.clear();
    Gear Item;
    Item.id = "status-api-test"; Item.name = "定向清除试器"; Item.slot = "armor";
    Item.shape = "robe"; Item.element = "神"; Item.polarity = "天阳"; Item.grade = 1;
    Item.crafted = true; Item.ForgeVersion = 2; Item.EffectId = Effect; Item.EffectPower = 1;
    Item.SourceMaterials = {{"iron", 10}}; Item.ForgeEnergy = 10;
    Item.dimensions = {100, 100, 100, 100, 100};
    Actor.arts.gear.push_back(Item); Actor.arts.equipped["armor"] = Item.id;
    Npc Opponent; Opponent.id = "status-api-npc"; Opponent.name = "定向清除接口木桩";
    Opponent.stage = 0; Opponent.dao = "神";
    State.npcs.push_back(Opponent);
    EnemySpec Enemy; Enemy.name = Opponent.name; Enemy.id = Opponent.id; Enemy.stage = 0;
    Enemy.speed = 0; Enemy.style = "神";
    battleStart(State, Enemy);
    auto& Field = *State.player.arts.battle;
    Field.mana = 12;
    Field.playerPoison = 18; Field.playerPoisonTurns = 1;
    Field.enemyCards.poison = 9; Field.enemyCards.poisonTurns = 1; Field.enemyCards.poisonDamage = 18;
    Field.enemyCards.burn = 13; Field.enemyCards.burnTurns = 3;
    Field.enemyCards.guard = .145; Field.enemyCards.discardPower = .175;
    Field.enemyCards.buff = .18; Field.enemyCards.reduction = .08;
    return State;
}

json::Value mfStatusAbilityView(const Game& State) {
    const auto View = api::battleView(State);
    for (const auto& Entry : View["artifacts"].asArray())
        if (Entry["id"].asString() == "artifact:status-api-test") return Entry;
    return {};
}

http::Response mfStatusApiAction(const json::Value& Body) {
    return api::handle({"POST", "/api/battle/turn", Body.dump(), ""});
}

void mfRejectStatusApiUnchanged(const json::Value& Body, const string& Label) {
    const string Before = saveToJson(api::game()); const auto Rng = api::game().rng;
    check(mfStatusApiAction(Body).status >= 400, Label + "被接口拒绝");
    check(saveToJson(api::game()) == Before && api::game().rng == Rng,
        Label + "拒绝后状态、费用和随机数完全不变");
}

void mfRunStatusArtifactApiTests() {
    ForgeStatusApiSession Session;
    Session.SessionState.g = mfStatusApiGame("cleanse");
    auto& State = Session.SessionState.g;
    const auto Ability = mfStatusAbilityView(State);
    checkStr(Ability["targetMode"].asString(), "status-one", "清浊接口声明单选状态模式");
    check(!Ability["statusToken"].asString().empty(), "清浊接口提供非空状态报价版本");
    const auto& Targets = Ability["targets"].asArray();
    const auto Poison = std::find_if(Targets.begin(), Targets.end(), [](const auto& Entry) {
        return Entry["id"].asString() == "poison";
    });
    check(Poison != Targets.end(), "清浊接口提供实际可清除的中毒目标");
    if (Poison != Targets.end()) {
        checkNear((*Poison)["layers"].asNumber(), 9, "单选目标下发当前毒层");
        checkNear((*Poison)["removeLayers"].asNumber(), 5, "单选目标预览固定削五层");
        checkNear((*Poison)["remainingLayers"].asNumber(), 4, "单选目标预览实际剩余毒层");
        check(!(*Poison)["unit"].asString().empty() && !(*Poison)["description"].asString().empty(),
            "状态选择提供层单位和效果解释");
    }
    json::Value Body(json::Object{});
    Body.set("id", "artifact:status-api-test"); Body.set("target", "poison");
    Body.set("expectedTurn", State.player.arts.battle->turn);
    Body.set("expectedStatusToken", Ability["statusToken"]);
    auto Missing = Body; Missing.asObject().erase("expectedStatusToken");
    mfRejectStatusApiUnchanged(Missing, "缺少状态报价版本");
    Missing = Body; Missing.asObject().erase("expectedTurn");
    mfRejectStatusApiUnchanged(Missing, "缺少预期回合");
    Missing = Body; Missing.asObject().erase("target");
    mfRejectStatusApiUnchanged(Missing, "缺少单状态目标");
    auto Invalid = Body; Invalid.set("expectedTurn", State.player.arts.battle->turn + 1);
    mfRejectStatusApiUnchanged(Invalid, "跨回合的状态选择");
    Invalid = Body; Invalid.set("expectedTurn", State.player.arts.battle->turn + .5);
    mfRejectStatusApiUnchanged(Invalid, "预期回合不是整数");
    Invalid = Body; Invalid.set("expectedTurn", "1");
    mfRejectStatusApiUnchanged(Invalid, "预期回合字段类型错误");
    Invalid = Body; Invalid.set("expectedStatusToken", "outdated-status-token");
    mfRejectStatusApiUnchanged(Invalid, "过期状态报价");
    Invalid = Body; Invalid.set("expectedStatusToken", 25);
    mfRejectStatusApiUnchanged(Invalid, "状态报价字段类型错误");
    Invalid = Body; Invalid.set("target", "unknown-status");
    mfRejectStatusApiUnchanged(Invalid, "未知状态选择");
    Invalid = Body; Invalid.set("target", 5);
    mfRejectStatusApiUnchanged(Invalid, "单状态目标字段类型错误");
    Invalid = Body; Invalid.set("target", "guard");
    mfRejectStatusApiUnchanged(Invalid, "清浊伪造敌方增益目标");
    Invalid = Body; Invalid.set("targets", json::Array{"poison"});
    mfRejectStatusApiUnchanged(Invalid, "状态选择不能混入手牌多选数组");
    State.player.arts.battle->enemyCards.poison = 8;
    State.player.arts.battle->enemyCards.poisonDamage = 16;
    State.player.arts.battle->playerPoison = 16;
    mfRejectStatusApiUnchanged(Body, "同回合毒层变化后的旧状态报价");
    const auto Updated = mfStatusAbilityView(State);
    Body.set("expectedStatusToken", Updated["statusToken"]);
    const auto Response = mfStatusApiAction(Body);
    checkEq(Response.status, 200, "刷新状态报价后可通过真实API定向清浊");
    const auto Document = json::Value::parse(Response.body);
    check(Document["ok"].asBool(), "定向清浊响应明确成功");
    checkEq(State.player.arts.battle->enemyCards.poison, 3, "API真实结算只扣选定毒的五层");
    checkEq(State.player.arts.battle->playerPoison, 6, "API真实结算同步毒跳伤");
    checkEq(State.player.arts.battle->enemyCards.burn, 13, "API定向清毒保留其他负面状态");
    checkEq(Document["state"]["battle"]["playerPoison"].asInt(), 6, "API响应立即反映清毒后实际跳伤");
    mfRejectStatusApiUnchanged(Body, "定向清浊重复提交");

    Session.SessionState.g = mfStatusApiGame("dispel");
    const auto Dispel = mfStatusAbilityView(State);
    checkStr(Dispel["targetMode"].asString(), "status-one", "破护接口声明单选状态模式");
    const auto& DispelTargets = Dispel["targets"].asArray();
    check(DispelTargets.size() == 2, "破护可选目标仅包括守势和锐势");
    check(std::none_of(DispelTargets.begin(), DispelTargets.end(), [](const auto& Entry) {
        const auto Id = Entry["id"].asString(); return Id == "buff" || Id == "reduction";
    }), "破护不提供本场积累增伤减伤的清除入口");
    Body.set("target", "sharpness"); Body.set("expectedTurn", State.player.arts.battle->turn);
    Body.set("expectedStatusToken", Dispel["statusToken"]);
    checkEq(mfStatusApiAction(Body).status, 200, "破护通过API只削选定锐势");
    checkNear(State.player.arts.battle->enemyCards.discardPower, .165, "一阶破护真实削除一层即一个百分点");
    checkNear(State.player.arts.battle->enemyCards.guard, .145, "API选锐势保留守势");
    checkNear(State.player.arts.battle->enemyCards.buff, .18, "API破护保留本场增伤");
    checkNear(State.player.arts.battle->enemyCards.reduction, .08, "API破护保留本场减伤");
}
}
void mfRunForgeApiTests() {
    mfRunStatusArtifactApiTests();
    CreateOptions Options; Options.name = "炼器契约回归"; Options.seed = 20261017;
    Game GameState = createGame(Options); GameState.pending.reset(); GameState.job.reset();
    GameState.player.stones = 10000; GameState.player.arts.materials["iron"] = 40;
    const string Initial = saveToJson(GameState);
    auto Input = mfForgeInput();
    const auto Catalog = api::mfForgeOptionsView(GameState);
    check(Catalog["version"].asInt() == 2 && Catalog["materials"].asArray().size() >= 575, "新版完整材料目录");
    const auto Quote = api::mfForgePreviewView(GameState, Input);
    check(Quote["ok"].asBool() && Quote["info"]["enabled"].asBool(), "十份现有材料可预览并开工");
    check(Quote["info"]["ability"].isObject() && Quote["info"]["ability"]["cost"].asInt(-1) == 1,
          "成器前可查看新版主动的真实费用");
    checkStr(saveToJson(GameState), Initial, "目录与预览不改变时钟RNG或库存");
    for (const auto& Definition : content::mfForgeMaterials()) {
        if (Definition.Source != content::ForgeMaterialSource::BeastDrop) continue;
        Game CatalogFixture = GameState; CatalogFixture.player.arts.materials[Definition.Material.id] = 1;
        json::Value CatalogState(json::Object{}); api::view::appendEconomyState(CatalogFixture, CatalogState);
        const auto& Entries = CatalogState["materialCatalog"].asArray();
        const auto Found = std::find_if(Entries.begin(), Entries.end(), [&](const json::Value& Entry) {
            return Entry["id"].asString() == Definition.Material.id && Entry["name"].asString() == Definition.Material.name;
        });
        check(Found != Entries.end(), "持有的新素材在行囊有后端名称而非待加载");
        check(Found != Entries.end() && !(*Found)["acquisitionPending"].asBool(), "妖兽部位目录展示真实已开放掉落来源");
        break;
    }
    Input.set("quoteToken", Quote["info"]["quoteToken"]);
    Game Stale = GameState; Stale.player.stones -= 1; const string StaleBefore = saveToJson(Stale); int DurationDays = 0;
    check(!api::mfBeginForgeRequest(Stale, Input, DurationDays).empty(), "旧报价在库存资金变化后拒绝");
    checkStr(saveToJson(Stale), StaleBefore, "旧报价拒绝不扣资源");
    auto Invalid = Input; Invalid.set("materials", "iron");
    check(!api::mfForgePreviewView(GameState, Invalid)["ok"].asBool(), "拒绝错误材料数组类型");
    Invalid = Input; Invalid.set("version", 2.5);
    check(!api::mfForgePreviewView(GameState, Invalid)["ok"].asBool(), "拒绝非整数规则版本");
    check(api::mfBeginForgeRequest(GameState, Input, DurationDays).empty() && GameState.job && GameState.job->ForgeProduct,
          "真实开工冻结新版成品合同");
    Game Restored; string Error; const string ContractText = saveToJson(GameState);
    check(loadFromJson(ContractText, Restored, &Error), "在途新版炼器往返：" + Error);
    check(Restored.job && Restored.job->ForgeProduct && Restored.job->ForgeProduct->EffectId == "redraw", "合同保留主效果与材料");
    if (Restored.job && Restored.job->ForgeProduct) check(Restored.job->ForgeProduct->SourceMaterials == GameState.job->ForgeProduct->SourceMaterials, "投料证据完整往返");
    Gear Item = *GameState.job->ForgeProduct; Item.id = "forge-contract-fixture";
    GameState.job.reset(); GameState.player.arts.gear.push_back(Item);
    const auto Document = json::Value::parse(saveToJson(GameState));
    checkEq(Document["player"]["arts"]["gear"].asArray().back()["forgeVersion"].asInt(), 2, "新版法器编码规则版本");
    checkStr(Document["player"]["arts"]["gear"].asArray().back()["effectId"].asString(), "redraw", "新版法器编码主效果");
    check(loadFromJson(Document.dump(), Restored, &Error), "新器型与法器字段可重载：" + Error);
    check(Restored.player.arts.gear.back().SourceMaterials == Item.SourceMaterials, "新法器原料与效果无损恢复");
    auto Bad = Document; auto Player = Bad["player"], Arts = Player["arts"], GearList = Arts["gear"];
    GearList.asArray().back().set("effectPower", "1.2"); Arts.set("gear", GearList); Player.set("arts", Arts); Bad.set("player", Player);
    mfExpectRejectedSave(Bad, Restored, "坏效果数值类型被拒绝");
    Bad = Document; Player = Bad["player"]; Arts = Player["arts"]; GearList = Arts["gear"];
    GearList.asArray().back().set("effectId", "nonexistent-effect"); Arts.set("gear", GearList); Player.set("arts", Arts); Bad.set("player", Player);
    mfExpectRejectedSave(Bad, Restored, "未知主效果被拒绝");
    if (!GameState.npcs.empty()) {
        GameState.npcs.front().gear.push_back(Item);
        check(loadFromJson(saveToJson(GameState), Restored, &Error) && Restored.npcs.front().gear.back().ForgeVersion == 2,
              "NPC赠装的新法器字段往返：" + Error);
    }
    GameState.player.arts.equipped["weapon"] = Item.id;
    EnemySpec Enemy; Enemy.name = "炼器回归靶"; Enemy.stage = 0; Enemy.style = "剑";
    battleStart(GameState, Enemy);
    if (GameState.player.arts.battle) {
        auto& State = *GameState.player.arts.battle;
        State.ArtifactState.Version = 2; State.ArtifactState.SwordQiCap = 9; State.swordQi = 9;
        State.ArtifactState.RoundShield = 400; State.ArtifactState.RoundBurn = 8;
        State.ArtifactState.RoundPoisonDamagePerLayer = 5;
        State.enemyCards.ArtifactState.Version = 2;
        State.enemyCards.ArtifactState.RoundShield = 3; State.enemyCards.ArtifactState.RoundBurn = 1;
        State.enemyCards.ArtifactState.RoundPoisonDamagePerLayer = 1;
        const string BattleText = saveToJson(GameState);
        check(loadFromJson(BattleText, Restored, &Error), "突破基础资源上限的在途法器快照恢复：" + Error);
        if (Restored.player.arts.battle) {
            const auto& PlayerSnapshot = Restored.player.arts.battle->ArtifactState;
            const auto& EnemySnapshot = Restored.player.arts.battle->enemyCards.ArtifactState;
            checkEq(PlayerSnapshot.SwordQiCap, 9, "入战上限冻结恢复");
            checkEq(PlayerSnapshot.RoundShield, 400, "玩家法器固定每轮护盾点数往返");
            checkEq(PlayerSnapshot.RoundBurn, 8, "玩家法器固定每轮灼烧点数往返");
            checkEq(PlayerSnapshot.RoundPoisonDamagePerLayer, 5, "玩家法器固定每层毒伤往返");
            checkEq(EnemySnapshot.RoundShield, 3, "敌方法器固定每轮护盾点数往返");
            checkEq(EnemySnapshot.RoundBurn, 1, "敌方法器固定每轮灼烧点数往返");
            checkEq(EnemySnapshot.RoundPoisonDamagePerLayer, 1, "敌方法器固定每层毒伤往返");
        }
        const auto SnapshotDocument = json::Value::parse(BattleText);
        for (const bool EnemySide : {false, true}) {
            const string Side = EnemySide ? "敌方" : "玩家";
            const auto& SnapshotJson = EnemySide
                ? SnapshotDocument["player"]["arts"]["battle"]["enemyCards"]["artifactState"]
                : SnapshotDocument["player"]["arts"]["battle"]["artifactState"];
            checkEq(SnapshotJson.asObject().size(), 14, Side + "法器快照只编码十四个当前字段");
            check(!SnapshotJson.asObject().contains("roundShieldRatio") &&
                !SnapshotJson.asObject().contains("roundBurnScale"), Side + "法器快照不再编码旧比率字段");
            const auto ReplaceSnapshot = [&](const json::Value& Replacement) {
                auto Modified = SnapshotDocument;
                auto ModifiedPlayer = Modified["player"], ModifiedArts = ModifiedPlayer["arts"];
                auto ModifiedBattle = ModifiedArts["battle"];
                if (EnemySide) {
                    auto ModifiedEnemy = ModifiedBattle["enemyCards"];
                    ModifiedEnemy.set("artifactState", Replacement); ModifiedBattle.set("enemyCards", ModifiedEnemy);
                } else ModifiedBattle.set("artifactState", Replacement);
                ModifiedArts.set("battle", ModifiedBattle); ModifiedPlayer.set("arts", ModifiedArts);
                Modified.set("player", ModifiedPlayer); return Modified;
            };
            for (const auto* Key : {"roundShield", "roundBurn", "roundPoisonDamagePerLayer"}) {
                for (const int Boundary : {0, 1000000}) {
                    auto BoundarySnapshot = SnapshotJson; BoundarySnapshot.set(Key, Boundary);
                    Game BoundaryRestored;
                    check(loadFromJson(ReplaceSnapshot(BoundarySnapshot).dump(), BoundaryRestored, &Error),
                        Side + "固定点数字段" + Key + "接受合法边界" + std::to_string(Boundary) + "：" + Error);
                }
                for (const auto& BadValue : {json::Value(-1), json::Value(.5), json::Value(1000001), json::Value("5")}) {
                    auto InvalidSnapshot = SnapshotJson; InvalidSnapshot.set(Key, BadValue);
                    mfExpectRejectedSave(ReplaceSnapshot(InvalidSnapshot), Restored, Side + "固定点数字段" + Key + "拒绝非法数值或类型");
                }
                auto MissingSnapshot = SnapshotJson; MissingSnapshot.asObject().erase(Key);
                mfExpectRejectedSave(ReplaceSnapshot(MissingSnapshot), Restored, Side + "固定点数快照拒绝缺少字段" + Key);
            }
            for (const auto* OldKey : {"roundShieldRatio", "roundBurnScale"}) {
                auto OldSnapshot = SnapshotJson; OldSnapshot.set(OldKey, .1);
                mfExpectRejectedSave(ReplaceSnapshot(OldSnapshot), Restored, Side + "固定点数快照拒绝旧比例字段" + OldKey);
            }
        }
        json::Value Action(json::Object{}); json::Array Hand;
        for (const auto& Uid : State.hand) Hand.emplace_back(Uid);
        Action.set("expectedHand", Hand); Action.set("expectedTurn", State.turn); Action.set("targets", json::Array{});
        vector<string> Targets;
        check(api::mfValidateArtifactTargets(State, Action, Targets).empty(), "当前整组手牌可作为选择版本");
        Action.set("expectedTurn", State.turn + 1);
        check(!api::mfValidateArtifactTargets(State, Action, Targets).empty(), "跨回合换牌选择拒绝");
        Action.set("expectedTurn", State.turn);
        if (!State.hand.empty()) {
            Action.set("targets", json::Array{State.hand.front(), State.hand.front()});
            check(!api::mfValidateArtifactTargets(State, Action, Targets).empty(), "重复UID选择拒绝");
        }
    }
}
} // namespace nztest
