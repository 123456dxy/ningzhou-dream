#include "harness.h"
#include "core/api.h"
#include "core/arts.h"
#include "core/legacy.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace nztest {
namespace {
using namespace nz;
using json::Value;

// 用途：隔离测试账号的传承和角色；流程：本组 API 回归；用法：栈上创建后自动恢复绑定；返回：无，管理临时目录与会话。
struct PlayerFixScope {
    std::string previous = defaultSaveDir();
    std::filesystem::path directory = std::filesystem::current_path() / "build-player-fix-tests" /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    api::Session session;
    PlayerFixScope() {
        std::filesystem::create_directories(directory);
        const auto bytes = directory.u8string();
        setSaveDirOverride(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        api::bindSession(&session);
        session.started = true;
        CreateOptions options; options.seed = 20261012; options.name = "回归旧人生"; options.traitsExplicit = true;
        session.g = createGame(options);
        session.g.npcs.clear(); session.g.job.reset(); session.g.pending.reset();
        session.g.player.arts.progress = {}; session.g.player.stones = 100;
        session.g.player.arts.herbs.clear(); session.g.player.herbs = 0;
    }
    ~PlayerFixScope() { api::bindSession(nullptr); setSaveDirOverride(previous); }
};

// 用途：提交当前独立账号请求；流程：真实 core API 回归；用法：传路由与 JSON 参数；返回：HTTP 响应，不模拟领域实现。
http::Response call(const std::string& route, const Value& payload = Value(json::Object{})) {
    http::Request request; request.method = "POST"; request.path = "/api/" + route; request.body = payload.dump();
    return api::handle(request);
}

// 用途：生成明确空天赋的新世方案；流程：重开原子性与预算回归；用法：传名字/属性预算；返回：建号请求 JSON。
Value newLife(const std::string& name, int body = 0) {
    Value input(json::Object{}); input.set("name", name); input.set("traits", Value(json::Array{}));
    Value allocation(json::Object{});
    if (body) { allocation.set("apt", std::min(10, body)); allocation.set("body", std::max(0, body - 10)); }
    input.set("allocation", allocation); input.set("seed", 20261012); return input;
}

// 用途：读取测试传承文件；流程：验证重开持久化；用法：在 PlayerFixScope 内调用；返回：校验后的 Archive。
legacy::Archive archiveAt(const PlayerFixScope& scope) {
    std::ifstream file(scope.directory / "legacy.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), {}); return legacy::fromJson(text);
}

// 用途：合计可实际使用的药材；流程：药谷奖励同步核验；用法：传当前玩家；返回：具体库存份数。
int typedHerbs(const Player& player) {
    int total = 0; for (const auto& [name, count] : player.arts.herbs) total += count; return total;
}
}

// 用途：覆盖已领取成就、重开与药谷奖励；流程：完整自测；用法：由 test_main 调用；返回：无，失败累计到 harness。
void runPlayerFixTests() {
    std::printf("\n--- 重开传承、具体药材和制作预览回归 ---\n");
    {
        PlayerFixScope scope;
        auto& game = scope.session.g; game.player.arts.progress.brew = 3;
        const std::string old = saveToJson(game), oldId = game.lifeId;
        const auto options = Value::parse(call("build-options").body);
        checkEq(options["budget"].asInt(), BASE_POINTS + 1, "重开面板预览计入本世初试丹火一点传承");
        checkEq(options["pendingLegacyPoints"].asInt(), 1, "展示尚未结算的真实成就增量");
        check(saveToJson(game) == old && !std::filesystem::exists(scope.directory / "legacy.json"),
              "打开或取消建号面板不结束旧人生也不写传承");
        const auto checked = Value::parse(call("build-check", newLife("下一世", BASE_POINTS + 1)).body);
        check(checked["valid"].asBool(), "构筑校验与重开预览使用同一预算");
        check(call("new", newLife("非法分配", MAX_POINTS)).status == 400, "非法构筑拒绝重开");
        check(saveToJson(game) == old && !std::filesystem::exists(scope.directory / "legacy.json"),
              "失败建号不提前消耗成就、不替换旧人生");
        check(call("new", newLife("下一世", BASE_POINTS + 1)).status == 200, "使用新成就预算真实重开成功");
        const auto archive = archiveAt(scope);
        checkEq(archive.earned, 1, "重开确实持久化成就传承");
        checkEq(archive.runs, 1, "旧人生恰好结算一次");
        check(std::find(archive.claimed.begin(), archive.claimed.end(), oldId) != archive.claimed.end(), "保存已结算的旧人生ID");
        checkEq(Value::parse(call("build-options").body)["budget"].asInt(), BASE_POINTS + 1, "下一世预算保留传承点");
        game.player.arts.progress.brew = 3;
        check(call("new", newLife("再下一世")).status == 200 && archiveAt(scope).earned == 1,
              "相同成就跨人生不可重复刷天赋点");
    }
    {
        PlayerFixScope scope;
        auto& game = scope.session.g; game.player.stage = game.player.highestStage = 3;
        finish(game, "寿终正寝");
        checkEq(Value::parse(call("build-options").body)["budget"].asInt(), BASE_POINTS + 2, "寿尽未手动结算的筑基成就不会漏入预算");
        check(call("new", newLife("寿尽后重开")).status == 200 && archiveAt(scope).earned == 2, "寿尽直接重开仍发传承点");
    }
    {
        PlayerFixScope scope;
        auto& game = scope.session.g; game.player.arts.progress.brew = 3;
        std::filesystem::create_directory(scope.directory / "legacy.json");
        const auto before = saveToJson(game);
        checkEq(call("new", newLife("写入失败不能覆盖")).status, 500, "传承不能落盘时拒绝丢弃旧人生");
        check(saveToJson(game) == before, "传承写入失败保留旧角色、RNG与成就");
    }
    for (const auto& choice : {std::string("help"), std::string("take")}) {
        PlayerFixScope scope; auto& game = scope.session.g;
        Pending event; event.type = "valley"; event.title = "药谷中的求援";
        event.options = {{choice, "采得灵药", "", false}}; game.pending = event; game.world.herbs = 10;
        Value payload(json::Object{}); payload.set("id", choice);
        check(call("choose", payload).status == 200, "药谷选择实际结算" + choice);
        const int expected = choice == "help" ? 3 : 5;
        checkEq(typedHerbs(game.player), expected, "灵药奖励进入可炼丹的具体药材库存");
        checkEq(game.player.herbs, expected, "具体库存与汇总份数同步");
        checkEq(game.world.herbs, 10 - expected, "只消耗真实获得的野外药材份数");
        Game loaded; std::string error;
        check(loadFromJson(saveToJson(game), loaded, &error) && typedHerbs(loaded.player) == expected, "事件药材旧schema读回后可用");
    }
    {
        PlayerFixScope scope; auto& game = scope.session.g;
        game.year = 9; game.plan = "cultivate"; Npc nearby; nearby.id = "context-npc"; nearby.name = "测试道友";
        nearby.location = game.player.location; nearby.alive = true; game.npcs = {nearby};
        maybeChoice(game);
        check(game.pending && game.pending->text.find("游历途中") == std::string::npos, "闭关触发求援不再假称正在游历");
    }
    {
        PlayerFixScope scope; const auto before = saveToJson(scope.session.g);
        const auto state = Value::parse(call("state").body)["state"];
        bool ingredientDetails = false;
        for (const auto& item : state["brewOptions"].asArray()) if (item["id"].asString() == "5001") {
            ingredientDetails = item["ingredients"].isArray() && item["ingredients"].asArray().size() == 3;
            checkEq(item["durabilityCost"].asInt(-1), 1, "制作预览公开既有每炉耐久消耗");
            check(item["effect"].isString() && !item["effect"].asString().empty(), "丹方展示真实服用效果");
        }
        check(ingredientDetails, "丹方公开单炉配方供整批库存预检");
        check(saveToJson(scope.session.g) == before, "制作预览不改变制作数值、资源、时钟或RNG");
    }
}
}
