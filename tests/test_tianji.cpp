#include "harness.h"
#include "core/tianji.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
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
std::string readStoreText(const std::filesystem::path& dir) {
    std::ifstream file(dir / "store.json");
    return std::string((std::istreambuf_iterator<char>(file)), {});
}
Value readStore(const std::filesystem::path& dir) { return Value::parse(readStoreText(dir)); }
Value actionPayload(const std::string& requestId, const Value& encounter, const std::string& action) {
    auto body = payload(requestId); body.set("encounterId", encounter["id"]);
    body.set("expectedRevision", encounter["revision"]); body.set("id", action); return body;
}
// Persist through a fresh, unrelated entry so assertions inspect the restored in-memory
// state, not merely the old file. Remove precisely that entry and its ID allocation
// before comparing every original snapshot, attempt, encounter, RNG and receipt.
void checkPersistedRollback(tianji::Store& store, const std::filesystem::path& dir,
                            const Value& before, const std::string& probeOwner,
                            const Game& game, const std::string& label) {
    const auto probe = call(store, probeOwner, &game, "join", payload("probe-" + probeOwner));
    checkEq(probe.status, 200, label + "：独立报名可保存回滚后的内存");
    const auto probeId = "shadow-" + std::to_string(before["nextId"].asInt());
    checkStr(Value::parse(probe.body)["entry"]["id"].asString(), probeId, label + "：失败未消耗全局ID");
    auto after = readStore(dir);
    checkEq(after["nextId"].asInt(), before["nextId"].asInt() + 1, label + "：仅独立报名消耗一个ID");
    auto& entries = after.asObject().at("entries").asArray();
    const auto oldSize = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Value& e) {
        return e["id"].asString() == probeId && e["owner"].asString() == probeOwner;
    }), entries.end());
    checkEq(oldSize - entries.size(), 1, label + "：只排除用于检查的新留影");
    after.set("nextId", before["nextId"]);
    check(after.dump() == before.dump(), label + "：完整留影、尝试、战斗、RNG和回执全部恢复");
}
Value retryAndReload(tianji::Store& store, const std::filesystem::path& dir, const Game& game,
                     const std::string& route, const Value& body, const std::string& label) {
    const auto retry = call(store, "61", &game, route, body);
    checkEq(retry.status, 200, label + "：相同请求ID重试成功");
    const auto state = store.status("61", &game).dump(), persisted = readStore(dir).dump();
    checkEq(call(store, "61", &game, route, body).status, 200, label + "：成功后重复请求幂等");
    check(store.status("61", &game).dump() == state && readStore(dir).dump() == persisted,
          label + "：重复请求不改变内存或已存数据");
    tianji::Store restarted(dir.string());
    check(restarted.healthy(), label + "：成功后重启记录健康");
    check(restarted.status("61", &game).dump() == state, label + "：重启完整恢复成功状态");
    checkEq(call(restarted, "61", &game, route, body).status, 200, label + "：重启后仍识别原请求ID");
    check(restarted.status("61", &game).dump() == state && readStore(dir).dump() == persisted,
          label + "：重启后重放不重复行动或计分");
    return Value::parse(retry.body);
}
void runJoinIdCollisionTest(const std::filesystem::path& dir) {
    Game game = fixture("ID冲突测试");
    tianji::Store initial(dir.string());
    checkEq(call(initial, "61", &game, "join", payload("collision-a")).status, 200, "准备冲突留影甲");
    checkEq(call(initial, "62", &game, "join", payload("collision-b")).status, 200, "准备冲突留影乙");
    auto challenge = payload("collision-challenge");
    challenge.set("targetId", initial.status("62", &game)["entry"]["id"]);
    checkEq(call(initial, "61", &game, "challenge", challenge).status, 200, "准备ID冲突时需保留的遭遇");
    auto before = readStore(dir); before.set("nextId", 1);
    { std::ofstream file(dir / "store.json", std::ios::trunc); file << before.dump(2); }
    const auto originalBytes = readStoreText(dir);
    tianji::Store collision(dir.string());
    check(collision.healthy(), "当前读取契约允许指向已有留影的nextId");
    const auto stateA = collision.status("61", &game).dump(), stateB = collision.status("62", &game).dump();
    // Cover both an available destination and the failed-write path that must never
    // erase an existing entry when emplace did not insert a new one.
    for (const bool blocked : {false, true}) {
        if (blocked) {
            std::filesystem::rename(dir / "store.json", dir / "store.backup");
            std::filesystem::create_directory(dir / "store.json");
        }
        const auto rejected = call(collision, "63", &game, "join", payload("collision-new-owner"));
        if (blocked) {
            std::filesystem::remove(dir / "store.json");
            std::filesystem::rename(dir / "store.backup", dir / "store.json");
        }
        checkEq(rejected.status, 503, "报名ID冲突拒绝提交");
        checkStr(Value::parse(rejected.body)["code"].asString(), "store_unavailable", "ID冲突在持久化前失败关闭");
        check(collision.status("63", &game)["entry"].isNull(), "ID冲突未创建新账号留影");
        check(collision.active("61") && collision.status("61", &game).dump() == stateA &&
              collision.status("62", &game).dump() == stateB, "ID冲突不删除已有留影或改变原遭遇");
        check(readStoreText(dir) == originalBytes, "ID冲突保持存储文件逐字节不变");
    }
    // Persist via an unrelated monotone closure to verify hidden in-memory state
    // and nextId also survived; only that explicitly changed closed flag may differ.
    const auto expired = fixture("ID冲突测试", 60);
    check(collision.observe("62", &expired), "冲突后无关账号仍可正常关闭窗口并保存");
    for (auto& entry : before.asObject().at("entries").asArray())
        if (entry["owner"].asString() == "62") entry.set("closed", true);
    check(readStore(dir).dump() == before.dump(), "ID冲突完整保留nextId、原留影、尝试、遭遇、RNG和回执");
}
void runTransactionRollbackTests(const std::filesystem::path& dir) {
    std::printf("\n--- 天机事务定向回滚回归 ---\n");
    Game challenger = fixture("回滚挑战者"), opponent = challenger;
    opponent.player.name = "回滚对手";
    // Keep end-turn probes nonterminal independently of the initial random deck.
    challenger.player.arts.hpBonus = opponent.player.arts.hpBonus = 5000;
    tianji::Store store(dir.string());
    for (const std::string owner : {"62", "63", "64"})
        checkEq(call(store, owner, &opponent, "join", payload("setup-" + owner)).status, 200, "准备回滚测试真人留影");
    const auto targetB = store.status("62", &opponent)["entry"]["id"].asString();
    const auto targetC = store.status("63", &opponent)["entry"]["id"].asString();
    auto otherChallenge = payload("unrelated-start"); otherChallenge.set("targetId", targetB);
    checkEq(call(store, "64", &opponent, "challenge", otherChallenge).status, 200, "准备另一账号的进行中遭遇");
    const auto unrelatedEncounter = store.status("64", &opponent)["encounter"].dump();

    auto failedWrite = [&](tianji::Store& current, const std::string& route, const Value& body,
                           const std::string& probeOwner, const std::string& label) {
        const auto before = readStore(dir);
        const auto ownState = current.status("61", &challenger).dump();
        const auto otherState = current.status("64", &opponent).dump();
        // Only replace this temporary fixture's destination; atomic rename must fail.
        std::filesystem::rename(dir / "store.json", dir / "store.backup");
        std::filesystem::create_directory(dir / "store.json");
        const auto failed = call(current, "61", &challenger, route, body);
        std::filesystem::remove(dir / "store.json");
        std::filesystem::rename(dir / "store.backup", dir / "store.json");
        checkEq(failed.status, 503, label + "：落盘失败返回503");
        checkStr(Value::parse(failed.body)["code"].asString(), "storage", label + "：明确返回存储错误");
        check(current.status("61", &challenger).dump() == ownState, label + "：本人可见状态未变化");
        check(current.active("64") && current.status("64", &opponent).dump() == otherState,
              label + "：另一账号的进行中遭遇未受影响");
        check(readStore(dir).dump() == before.dump(), label + "：原持久化文件未被改写");
        checkPersistedRollback(current, dir, before, probeOwner, opponent, label);
    };

    const auto join = payload("failed-join");
    failedWrite(store, "join", join, "71", "首次报名回滚");
    check(store.status("61", &challenger)["entry"].isNull(), "失败报名没有残留留影");
    const auto joinId = "shadow-" + std::to_string(readStore(dir)["nextId"].asInt());
    auto joined = retryAndReload(store, dir, challenger, "join", join, "报名重试");
    checkStr(joined["entry"]["id"].asString(), joinId, "报名重试使用回滚后的下一ID");

    auto firstChallenge = payload("failed-first-challenge"); firstChallenge.set("targetId", targetB);
    failedWrite(store, "challenge", firstChallenge, "72", "首次挑战回滚");
    check(store.status("61", &challenger)["encounter"].isNull() && !store.active("61"), "失败首次挑战删除新遭遇并释放锁");
    checkEq(store.status("61", &challenger)["entry"]["attempts"].asInt(), 0, "失败首次挑战删除新尝试记录");
    const auto firstId = "duel-" + std::to_string(readStore(dir)["nextId"].asInt());
    const auto started = retryAndReload(store, dir, challenger, "challenge", firstChallenge, "首次挑战重试");
    checkStr(started["encounter"]["id"].asString(), firstId, "挑战重试没有跳过遭遇ID");
    checkEq(started["entry"]["pending"].asInt(), 1, "挑战成功仅有一次待结算尝试");

    auto invalid = actionPayload("retry-action", started["encounter"], "missing-card");
    const auto beforeInvalid = readStore(dir);
    const auto invalidState = store.status("61", &challenger).dump();
    const auto rejected = call(store, "61", &challenger, "action", invalid);
    checkEq(rejected.status, 400, "非法卡牌行动被拒绝");
    checkStr(Value::parse(rejected.body)["code"].asString(), "illegal_action", "非法行动确实经过斗法规则验证");
    check(store.status("61", &challenger).dump() == invalidState, "非法行动不改变完整可见状态");
    checkPersistedRollback(store, dir, beforeInvalid, "73", opponent, "非法行动回滚");

    // Reuse the rejected action's request ID: no failed path may reserve a receipt.
    auto turn = invalid; turn.set("id", "end");
    failedWrite(store, "action", turn, "74", "非终局行动回滚");
    const auto advanced = retryAndReload(store, dir, challenger, "action", turn, "非终局行动重试");
    check(store.active("61") && advanced["encounter"]["result"].isNull(), "非终局行动重试后战斗仍进行中");
    checkEq(advanced["encounter"]["revision"].asInt(), 1, "非终局行动只推进一次版本");
    checkEq(advanced["entry"]["pending"].asInt(), 1, "非终局行动不提前结算尝试");

    // Move only this fixture to the draw boundary, making the terminal operation
    // award a nonzero score and exercise settlement rollback as well as the game.
    auto terminalData = readStore(dir);
    for (auto& encounter : terminalData.asObject().at("encounters").asArray()) {
        if (encounter["owner"].asString() != "61") continue;
        auto& battle = encounter.asObject().at("game").asObject().at("player").asObject().at("arts").asObject().at("battle");
        battle.set("turn", 23); battle.set("hp", battle["max"]); battle.set("enemyHp", battle["enemyMax"]);
        battle.asObject().at("enemyCards").set("turns", 23);
    }
    { std::ofstream file(dir / "store.json", std::ios::trunc); file << terminalData.dump(); }
    tianji::Store terminal(dir.string());
    check(terminal.healthy() && terminal.active("61"), "平局边界测试存档可恢复且仍在进行中");
    auto finish = actionPayload("failed-terminal", terminal.status("61", &challenger)["encounter"], "end");
    failedWrite(terminal, "action", finish, "75", "有积分终局行动回滚");
    check(terminal.active("61"), "失败终局仍保持原进行中遭遇");
    checkEq(terminal.status("61", &challenger)["entry"]["score"].asInt(), 0, "失败终局撤销新增积分");
    checkEq(terminal.status("61", &challenger)["entry"]["pending"].asInt(), 1, "失败终局恢复待结算尝试");
    const auto settled = retryAndReload(terminal, dir, challenger, "action", finish, "有积分终局重试");
    checkStr(settled["encounter"]["result"].asString(), "draw", "终局重试正确结算平局");
    checkEq(settled["encounter"]["scoreDelta"].asInt(), 1, "终局重试恢复正确积分增量");
    checkEq(settled["entry"]["score"].asInt(), 1, "终局重试只增加一次积分");
    checkEq(settled["entry"]["draws"].asInt(), 1, "终局重试只增加一场平局");
    check(!terminal.active("61"), "成功终局释放世界操作锁");

    auto replacement = payload("failed-replacement"); replacement.set("targetId", targetC);
    const auto settledEncounter = terminal.status("61", &challenger)["encounter"].dump();
    failedWrite(terminal, "challenge", replacement, "76", "替换已结算遭遇回滚");
    check(terminal.status("61", &challenger)["encounter"].dump() == settledEncounter,
          "失败替换恢复完整旧终局而非删除遭遇");
    checkEq(terminal.status("61", &challenger)["entry"]["attempts"].asInt(), 1, "失败替换仅保留旧目标的已结算尝试");
    const auto replacementId = "duel-" + std::to_string(readStore(dir)["nextId"].asInt());
    const auto replaced = retryAndReload(terminal, dir, challenger, "challenge", replacement, "替换遭遇重试");
    checkStr(replaced["encounter"]["id"].asString(), replacementId, "替换重试使用回滚后的遭遇ID");
    checkEq(replaced["entry"]["attempts"].asInt(), 2, "成功替换保留旧尝试并新增一次尝试");
    checkEq(replaced["entry"]["score"].asInt(), 1, "成功替换保留原平局积分");
    check(terminal.active("64") && terminal.status("64", &opponent)["encounter"].dump() == unrelatedEncounter,
          "所有失败、重试和重启均保留无关账号的进行中遭遇");
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
    runTransactionRollbackTests(dir / "rollback");
    runJoinIdCollisionTest(dir / "id-collision");
    std::filesystem::remove_all(dir); std::filesystem::remove_all(corruptDir);
}
}
