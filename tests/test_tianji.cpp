#include "harness.h"
#include "core/tianji.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/save.h"
#include "core/sim.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace nztest {
namespace {
using namespace nz;
using json::Value;
Value payload(const std::string& requestId) { Value v(json::Object{}); v.set("requestId", requestId); return v; }
Game fixture(const std::string& name, int year = 50) {
    Game g = createGame(); g.player.name = name; g.player.stage = 3; g.player.lifespan = 200;
    g.year = year - 1; g.clock.days = g.year * 360; g.player.age = g.clock.ageBase + g.year;
    g.pending.reset(); g.job.reset(); g.player.arts.battle.reset(); g.dungeons.run.reset();
    return g;
}
http::Response call(tianji::Store& store, const std::string& owner, const Game* game,
                    const std::string& route, const Value& body = Value(json::Object{}), const std::string& method = "POST", long long now = 1000) {
    http::Request req; req.path = "/api/tianji/" + route; req.method = method; req.body = body.dump();
    return store.handle(req, owner, owner == "61" ? "甲" : "乙", game, now);
}
}
void runTianjiTests() {
    using namespace nz;
    std::printf("\n--- 天机大比服务端真人留影与持久化 ---\n");
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto dir = std::filesystem::temp_directory_path() / ("nz-tianji-core-" + std::to_string(unique));
    const auto corruptDir = dir.string() + "-corrupt";
    Game a = fixture("真人甲"), b = fixture("真人乙");
    for (int year : {49,50,59,60,99,100,109,110}) {
        auto g = fixture("窗口", year); const auto w = tianji::windowFor(g);
        checkEq(w.year, year, "按界面显示的入道年计算");
        check(w.open == (year == 50 || year == 59 || year == 100 || year == 109), "十年窗口边界准确");
    }
    tianji::Store store(dir.string());
    check(store.healthy(), "新目录创建空天机大比");
    checkEq(store.board(1000)["total"].asInt(), 0, "空榜没有伪装NPC");
    checkEq(call(store, "", &a, "join", payload("anon")).status, 401, "禁止未认证报名");
    checkEq(call(store, "../", &a, "join", payload("path")).status, 401, "拒绝路径式账号归属");
    checkEq(call(store, "61", &a, "join", payload("get"), "GET").status, 405, "GET不能报名");
    auto malicious = payload("spoof"); malicious.set("score", 999); malicious.set("owner", "62");
    checkEq(call(store, "61", &a, "join", malicious).status, 400, "客户端不能提供成绩或归属");
    const auto joinedA = Value::parse(call(store, "61", &a, "join", payload("join-a")).body);
    check(joinedA["ok"].asBool(), "真人甲报名");
    checkStr(joinedA["phase"].asString(), "waiting", "一人报名诚实等待");
    checkEq(joinedA["opponents"].asArray().size(), 0, "不给单人填充NPC");
    const auto entryA = joinedA["entry"]["id"].asString();
    check(!entryA.empty(), "报名返回不可变留影标识");
    const auto frozen = joinedA["entry"]["build"].dump();
    a.player.body += 7; a.lifeId = "another-life";
    const auto repeated = Value::parse(call(store, "61", &a, "join", payload("join-a-again")).body);
    checkStr(repeated["entry"]["id"].asString(), entryA, "换人生或重复报名仍是账号本届原留影");
    checkStr(repeated["entry"]["build"].dump(), frozen, "报名后本体变化不更新留影");
    auto joinedB = Value::parse(call(store, "62", &b, "join", payload("join-b")).body);
    const auto entryB = joinedB["entry"]["id"].asString(); check(!entryB.empty(), "真人乙报名");
    auto challenge = payload("challenge-a"); challenge.set("targetId", entryA);
    checkEq(call(store, "61", &a, "challenge", challenge).status, 409, "禁止自我挑战");
    challenge.set("targetId", entryB);
    const auto started = Value::parse(call(store, "61", &a, "challenge", challenge).body);
    check(started["ok"].asBool() && store.active("61"), "玩家手动开始挑战其他真人留影");
    const auto encounter = started["encounter"]; const auto encounterId = encounter["id"].asString();
    check(!encounterId.empty(), "遭遇有服务端ID");
    check(encounter["battle"]["enemyCards"].isNull() && encounter["battle"]["rng"].isNull(), "客户端不见敌方隐藏牌堆与随机数");
    check(!encounter["battle"]["hand"].asArray().empty(), "真人手牌可手动操作");
    checkEq(call(store, "61", &a, "challenge", challenge).status, 200, "重复开始请求幂等");
    checkStr(Value::parse(call(store, "61", &a, "challenge", challenge).body)["encounter"]["id"].asString(), encounterId, "重复请求不另开战斗");
    b.player.body = 999; b.player.arts.spells.clear();
    checkStr(store.status("61", &a)["encounter"]["target"]["build"].dump(), joinedB["entry"]["build"].dump(), "离线对手本体变化不影响已存留影");
    const auto stateBeforeReload = store.status("61", &a)["encounter"].dump();
    tianji::Store reloaded(dir.string());
    check(reloaded.healthy(), "进行中挑战可从磁盘恢复");
    checkStr(reloaded.status("61", &a)["encounter"].dump(), stateBeforeReload, "恢复不改战斗随机状态、先手计划或手牌视图");
    auto turn = payload("turn-a"); turn.set("encounterId", encounterId); turn.set("expectedRevision", 0); turn.set("id", "end");
    checkEq(call(reloaded, "62", &b, "action", turn).status, 409, "他人不能操作挑战");
    // Replace only this test's store path with a directory to simulate a failed atomic rename.
    std::filesystem::rename(dir / "store.json", dir / "store.backup"); std::filesystem::create_directory(dir / "store.json");
    checkEq(call(reloaded, "61", &a, "action", turn).status, 503, "落盘失败不能回报成功");
    checkEq(reloaded.status("61", &a)["encounter"]["revision"].asInt(), 0, "落盘失败回滚内存与行动序号");
    std::filesystem::remove(dir / "store.json"); std::filesystem::rename(dir / "store.backup", dir / "store.json");
    checkEq(call(reloaded, "61", &a, "action", turn).status, 200, "修复存储后用相同请求重试成功");
    const auto afterTurn = reloaded.status("61", &a)["encounter"].dump();
    checkEq(call(reloaded, "61", &a, "action", turn).status, 200, "重复行动请求成功但不再结算");
    checkStr(reloaded.status("61", &a)["encounter"].dump(), afterTurn, "重复行动没有额外敌方回合");
    auto stale = turn; stale.set("requestId", "stale");
    checkEq(call(reloaded, "61", &a, "action", stale).status, 409, "多标签页旧版本行动被拒绝");
    auto conflict = turn; conflict.set("id", "forfeit");
    checkEq(call(reloaded, "61", &a, "action", conflict).status, 409, "同请求ID不同内容被拒绝");
    auto forfeit = payload("forfeit"); forfeit.set("encounterId", encounterId); forfeit.set("expectedRevision", 1); forfeit.set("id", "forfeit");
    const auto finished = Value::parse(call(reloaded, "61", &a, "action", forfeit).body);
    checkStr(finished["encounter"]["result"].asString(), "forfeit", "认输正常结算");
    check(!reloaded.active("61"), "结算释放本人世界操作锁");
    checkEq(finished["entry"]["attempts"].asInt(), 1, "每个对手仅一条挑战记录");
    checkEq(finished["entry"]["losses"].asInt(), 1, "认输只结算一次败场");
    checkEq(call(reloaded, "61", &a, "action", forfeit).status, 200, "重复终局请求不会重复计分");
    challenge.set("requestId", "farm"); checkEq(call(reloaded, "61", &a, "challenge", challenge).status, 409, "同对手不能刷荣誉");
    tianji::Store settledReload(dir.string()); check(settledReload.healthy(), "终局包含气血和日志可恢复");
    checkEq(settledReload.status("61", &a)["entry"]["losses"].asInt(), 1, "重启不重复加败场");
    checkEq(settledReload.board(1031)["total"].asInt(), 2, "全服榜只有两位真人报名者");
    auto a60 = fixture("真人甲", 60); check(settledReload.observe("61", &a60), "越过截止年持久关闭本届");
    check(!settledReload.status("61", &a)["canJoin"].asBool(), "回退到窗口不能重新报名");
    check(settledReload.status("61", &a)["entry"]["closed"].asBool(), "关闭状态不因回档撤销");
    auto a100 = fixture("真人甲", 100);
    checkEq(call(settledReload, "61", &a100, "join", payload("next-event")).status, 200, "下个五十年新届可报名");
    checkEq(settledReload.status("61", &a100)["entry"]["eventId"].asInt(), 2, "下届和历史届分开");
    // Compare unmodified source games separately: store must not mutate either source object.
    Game untouchedA = fixture("隔离甲"), untouchedB = fixture("隔离乙");
    const auto beforeA = saveToJson(untouchedA), beforeB = saveToJson(untouchedB);
    tianji::Store isolated((dir / "isolated").string());
    call(isolated, "63", &untouchedA, "join", payload("iso-a")); call(isolated, "64", &untouchedB, "join", payload("iso-b"));
    checkStr(saveToJson(untouchedA), beforeA, "生成留影不改挑战者世界"); checkStr(saveToJson(untouchedB), beforeB, "生成留影不改防守者世界");
    auto capacityChallenge = payload("capacity-start");
    capacityChallenge.set("targetId", isolated.status("63", &untouchedA)["opponents"][0]["id"].asString());
    const auto capacityStart = Value::parse(call(isolated, "63", &untouchedA, "challenge", capacityChallenge).body);
    const auto isolatedFile = dir / "isolated" / "store.json";
    {
        std::ifstream file(isolatedFile); std::string text((std::istreambuf_iterator<char>(file)), {});
        auto data = Value::parse(text); auto& duels = data.asObject().at("encounters").asArray();
        auto& battle = duels[0].asObject().at("game").asObject().at("player").asObject().at("arts").asObject().at("battle");
        Value oversizedLog(json::Array{}); for (int i = 0; i < 850; ++i) oversizedLog.push(std::string(200, 'x'));
        battle.set("log", oversizedLog);
        std::ofstream write(isolatedFile, std::ios::trunc); write << data.dump();
    }
    tianji::Store nearCapacity((dir / "isolated").string());
    check(nearCapacity.healthy(), "安全上限内的长战报可以恢复");
    auto capacityForfeit = payload("capacity-forfeit"); capacityForfeit.set("encounterId", capacityStart["encounter"]["id"]);
    capacityForfeit.set("expectedRevision", 0); capacityForfeit.set("id", "forfeit");
    const auto capacityEnd = Value::parse(call(nearCapacity, "63", &untouchedA, "action", capacityForfeit).body);
    check(capacityEnd["ok"].asBool() && !nearCapacity.active("63"), "接近大小上限仍可认输解锁人物");
    check(capacityEnd["encounter"]["log"].asArray().size() <= 64, "终局压缩最近战报为保存留出空间");
    std::filesystem::create_directories(corruptDir); { std::ofstream f(std::filesystem::path(corruptDir) / "store.json"); f << "broken"; }
    tianji::Store corrupt(corruptDir); check(!corrupt.healthy(), "损坏记录失败关闭而不是重置空榜");
    checkEq(call(corrupt, "61", &a, "join", payload("reset")).status, 503, "损坏时禁止重复报名");
    std::filesystem::remove_all(dir); std::filesystem::remove_all(corruptDir);
}
}
