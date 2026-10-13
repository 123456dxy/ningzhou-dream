#include "harness.h"
#include "core/api.h"
#include "core/character_build.h"
#include "core/interface/views.h"
#include "core/json.h"
#include "core/roster.h"
#include "core/save.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>

namespace nztest {
namespace {
using namespace nz;
struct CharacterBuildScope {
    string previous = defaultSaveDir();
    std::filesystem::path directory = std::filesystem::current_path() /
        ("character-build-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    api::Session session;
    CharacterBuildScope() {
        std::filesystem::create_directory(directory);
        const auto utf8 = directory.u8string();
        setSaveDirOverride(string(reinterpret_cast<const char*>(utf8.data()), utf8.size()));
        api::bindSession(&session);
    }
    ~CharacterBuildScope() {
        api::bindSession(nullptr); setSaveDirOverride(previous);
        std::error_code error;
        // Only files the tested endpoints can create in this unique test-owned directory.
        for (const char* name : {"legacy.json", "slot4.json"}) std::filesystem::remove(directory / name, error);
        std::filesystem::remove(directory, error);
    }
};
struct SaveEntrySnapshot {
    std::filesystem::file_type type;
    std::filesystem::file_time_type modified;
    string bytes;
    bool operator==(const SaveEntrySnapshot&) const = default;
};
std::map<std::filesystem::path, SaveEntrySnapshot> saveDirectorySnapshot(const std::filesystem::path& directory) {
    const auto readEntry = [](const std::filesystem::path& path) {
        SaveEntrySnapshot entry{std::filesystem::status(path).type(), std::filesystem::last_write_time(path), {}};
        if (std::filesystem::is_regular_file(path)) {
            std::ifstream file(path, std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read character build save fixture");
            entry.bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        }
        return entry;
    };
    std::map<std::filesystem::path, SaveEntrySnapshot> snapshot;
    snapshot.emplace(".", readEntry(directory));
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
        snapshot.emplace(entry.path().lexically_relative(directory), readEntry(entry.path()));
    return snapshot;
}
http::Response buildRequest(const string& path, const json::Value& input) {
    return api::handle({"POST", "/api/" + path, input.dump(), ""});
}
json::Value draft(const vector<string>& traits = {}) {
    json::Value input(json::Object{});
    input.set("name", "锁定开局"); input.set("gender", "女"); input.set("seed", 734);
    input.set("portraitId", "jinhong-female");
    json::Array list; for (const auto& id : traits) list.push_back(json::Value(id));
    input.set("traits", json::Value(list)); input.set("allocation", json::Value(json::Object{}));
    return input;
}
}

void runCharacterBuildTests() {
    using namespace nz;
    std::printf("\n===== 建角锁定、真实预览与立绘持久化 =====\n");
    CharacterBuildScope scope;
    const auto initial = saveToJson(scope.session.g);
    std::set<string> ids;
    for (const auto& portrait : playerPortraits()) {
        check(ids.insert(portrait.id).second, string(portrait.id) + "立绘ID唯一");
        check(string(portrait.src).rfind("/assets/portraits/", 0) == 0,
              string(portrait.id) + "立绘只引用已打包资源");
        CreateOptions options; options.gender = portrait.gender; options.portraitId = portrait.id;
        options.traitsExplicit = options.allocationExplicit = true;
        const Game game = createGame(options);
        Game loaded; string error;
        check(loadFromJson(saveToJson(game), loaded, &error), string(portrait.id) + "立绘可往返存档");
        checkStr(loaded.player.portraitId, portrait.id, "所选立绘ID持久化");
        checkStr(playerPortrait(loaded.player), portrait.src, "读档后仍显示所选立绘");
    }
    const auto optionsResponse = json::Value::parse(buildRequest("build-options", json::Value(json::Object{})).body);
    checkEq(optionsResponse["portraits"].asArray().size(), playerPortraits().size(), "创建面板得到完整权威立绘清单");
    json::Value zeroSeed(json::Object{}); zeroSeed.set("seed", 0);
    checkEq(json::Value::parse(buildRequest("build-options", zeroSeed).body)["innate"]["seed"].asInt(), 0,
            "显式零种子在选项、预览与创建中保持一致");

    CreateOptions baselineOptions; baselineOptions.seed = 734;
    baselineOptions.traitsExplicit = baselineOptions.allocationExplicit = true;
    const Game baseline = createGame(baselineOptions);
    // Expected deltas come from the 44 public talent contracts, not from buildCharacter.
    // apt, luck, insight, sword marker, alchemy, body, lifespan, stones, herbs.
    const map<string, vector<int>> initialDeltas = {
        {"sword", {6,0,0,16,0,0,0,0,0}}, {"herb", {0,0,0,0,20,0,0,0,4}},
        {"root", {22,0,0,0,0,0,0,-30,0}}, {"luck", {0,22,0,0,0,0,0,0,0}},
        {"longlife", {0,0,0,0,0,0,15,0,0}}, {"body", {0,0,0,0,0,2,0,0,0}},
        {"savvy", {0,0,12,0,0,0,0,0,0}}, {"rich", {0,0,0,0,0,0,0,100,0}},
        {"poor", {0,0,0,0,0,0,0,-70,0}}, {"weak", {-8,0,0,0,0,0,0,0,0}},
        {"unlucky", {0,-15,0,0,0,0,0,0,0}}, {"short", {0,0,0,0,0,0,-12,0,0}},
        {"bright", {0,0,7,0,0,0,0,0,0}}, {"craft", {0,0,0,0,0,1,0,15,0}},
        {"silver", {0,0,0,0,0,0,0,0,6}}, {"blade", {0,0,0,12,0,0,0,0,0}},
        {"calm", {0,0,6,0,0,0,0,0,0}}, {"wild", {0,10,0,0,0,0,0,0,0}},
        {"sturdy", {0,0,0,0,0,3,0,0,0}}, {"water", {12,0,0,0,0,0,0,0,0}},
        {"fire", {12,0,0,0,5,0,0,0,0}}, {"wood", {12,0,0,0,0,0,0,0,2}},
        {"gold", {12,0,0,3,0,0,0,0,0}}, {"earth", {12,0,0,0,0,1,0,0,0}},
        {"genius", {0,0,25,0,0,0,0,0,0}}, {"fortune", {0,14,0,0,0,0,0,0,0}},
        {"frugal", {0,0,0,0,0,0,0,45,0}}, {"alchemy", {0,0,0,0,14,0,0,0,0}},
        {"wanderer", {0,3,5,0,0,0,0,0,0}}, {"hard", {0,0,4,0,0,2,0,0,0}},
        {"mortal", {-12,0,0,0,0,0,0,0,0}}, {"immortal", {0,0,10,0,0,0,0,0,0}},
        {"dragonBlood", {0,0,0,0,0,1,0,0,0}},
    };
    const auto attributes = [](const Player& player) {
        return vector<int>{player.apt, player.luck, player.insight, player.sword, player.alchemy,
                           player.body, player.lifespan, player.stones, player.herbs};
    };
    const auto baseAttributes = attributes(baseline.player);

    // Every selectable talent goes through real startup, including post-build grants.
    for (const auto& talent : TALENTS()) {
        const auto input = draft({talent.id});
        const auto response = buildRequest("build-check", input);
        const auto body = json::Value::parse(response.body);
        check(response.status == 200 && body["valid"].asBool(), string(talent.id) + "开局预览合法");
        CreateOptions options; options.name = "锁定开局"; options.gender = "女";
        options.seed = 734; options.portraitId = "jinhong-female";
        options.traits = {talent.id}; options.traitsExplicit = options.allocationExplicit = true;
        const Game actual = createGame(options);
        const auto got = attributes(actual.player);
        const auto delta = initialDeltas.find(talent.id);
        bool attributesMatch = true;
        for (size_t index = 0; index < got.size(); ++index)
            attributesMatch = attributesMatch && got[index] == baseAttributes[index] +
                (delta == initialDeltas.end() ? 0 : delta->second[index]);
        check(attributesMatch, string(talent.id) + "真实开局九项属性符合独立天赋矩阵");
        const string id = talent.id;
        const int swordExp = id == "sword" ? 32 : id == "blade" ? 24 : 0;
        checkEq(actual.player.arts.exp.at("剑"), swordExp, id + "剑道经验正确发放且不混入金道");
        checkEq(actual.player.arts.exp.at("金"), id == "gold" ? 6 : 0, id + "金道经验正确发放");
        checkEq(actual.player.arts.mind, id == "spiritEyes" ? 3 : 0, id + "起始神识正确发放");
        if (id == "immortal") checkEq(actual.player.arts.points, 1, "转世仙人开局悟道点实际发放一次");
        if (id == "family") {
            checkEq(actual.player.arts.manuals.at("bull"), 1, "修真家族实际修得功法一层");
            check(std::find(actual.player.arts.knownSpells.begin(), actual.player.arts.knownSpells.end(), "gather") !=
                  actual.player.arts.knownSpells.end(), "修真家族实际领悟聚气");
        }
        json::Value state(json::Object{});
        api::view::appendPlayerState(actual, state); api::view::appendArtsState(actual, state);
        api::view::appendEquipmentState(actual, state);
        for (const char* field : {"player", "arts", "gears"})
            checkStr(body["preview"][field].dump(), state[field].dump(), string(talent.id) + "预览与真实开局" + field + "一致");
        checkEq(body["allocSpent"].asInt(), 0, "显式空分配不会偷偷把余点加到资质");
        checkEq(body["remain"].asInt(), BASE_POINTS - talent.cost, "预览保留未花点数并正确返还磨砺点数");
        checkStr(saveToJson(scope.session.g), initial, "预览不改变当前Game、RNG或检查点");
        check(!scope.session.started && scope.session.lastAutomatic.empty() && scope.session.writeError.empty(),
              "预览不改变当前会话生命周期");
        check(std::filesystem::is_empty(scope.directory), "预览不写账号存档或传承");
    }
    auto input = draft({"family", "spiritEyes", "poor"});
    json::Value allocation(json::Object{}); allocation.set("insight", 7); input.set("allocation", allocation);
    const auto preview = json::Value::parse(buildRequest("build-check", input).body);
    check(preview["valid"].asBool(), "复合天赋和悟性配点可锁定预览");
    const auto created = buildRequest("new", input);
    const auto newBody = json::Value::parse(created.body);
    checkEq(created.status, 200, "锁定方案可正式创建");
    for (const char* field : {"player", "arts", "gears"})
        checkStr(preview["preview"][field].dump(), newBody["state"][field].dump(), string("锁定预览与new最终") + field + "完全一致");
    const string saved = saveToJson(scope.session.g);
    // /new intentionally writes its automatic checkpoint; preview must preserve that exact baseline.
    check(std::filesystem::exists(std::filesystem::u8path(savePathFor(4))), "正式创建已写入自动存档，作为预览只读基准");
    const auto filesBeforePreview = saveDirectorySnapshot(scope.directory);
    const string automaticBeforePreview = scope.session.lastAutomatic;
    const string writeErrorBeforePreview = scope.session.writeError;
    const auto repeated = json::Value::parse(buildRequest("build-check", input).body);
    check(repeated["valid"].asBool(), "已开局时也可只读预览新方案");
    checkStr(saveToJson(scope.session.g), saved, "已有角色预览不会结算或替换人生");
    check(saveDirectorySnapshot(scope.directory) == filesBeforePreview,
          "已有角色预览不新增删除文件、不改存档字节或修改时间");
    check(scope.session.started && scope.session.lastAutomatic == automaticBeforePreview &&
          scope.session.writeError == writeErrorBeforePreview, "已有角色预览保留会话与自动存档状态");
    for (const char* bad : {"../player-male", "https://example.com/image.webp", "unknown", "player-male"}) {
        auto invalid = input; invalid.set("portraitId", bad);
        check(!json::Value::parse(buildRequest("build-check", invalid).body)["valid"].asBool(), "预览拒绝未授权或性别不符立绘");
        check(buildRequest("new", invalid).status == 400, "正式创建同样拒绝非法立绘");
        checkStr(saveToJson(scope.session.g), saved, "非法立绘不会替换当前角色");
    }
    for (const auto& invalidValue : {json::Value("not-an-object"), json::Value(json::Array{}), json::Value(true)}) {
        auto invalid = input; invalid.set("allocation", invalidValue);
        check(buildRequest("new", invalid).status == 400, "拒绝错误类型的属性分配");
    }
    auto unknown = input; json::Value unknownAllocation(json::Object{}); unknownAllocation.set("invisible", 0);
    unknown.set("allocation", unknownAllocation);
    check(!json::Value::parse(buildRequest("build-check", unknown).body)["valid"].asBool(), "预检拒绝未知属性键");
    check(buildRequest("new", unknown).status == 400 && saveToJson(scope.session.g) == saved,
          "未知属性键不会被静默丢弃或导致当前人生变化");
    check(!play::checkBuild({}, {{"invisible", 0}}, BASE_POINTS).ok, "核心建角预检也拒绝未知属性键");
    Game output = scope.session.g; string error;
    auto legacy = json::Value::parse(saved); auto player = legacy["player"];
    player.asObject().erase("portraitId"); legacy.set("player", player);
    check(loadFromJson(legacy.dump(), output, &error), "不带立绘字段的旧档仍可载入");
    checkStr(playerPortrait(output.player), "/assets/portraits/player-female.webp", "旧档回退为同性别默认立绘");
    player.set("portraitId", "../escape"); legacy.set("player", player);
    const string outputBefore = saveToJson(output);
    check(!loadFromJson(legacy.dump(), output, &error) && saveToJson(output) == outputBefore,
          "恶意存档立绘ID校验失败且原输出完整保留");
    player.set("portraitId", 123); legacy.set("player", player);
    check(!loadFromJson(legacy.dump(), output, &error), "立绘存档字段类型也经过结构校验");
}
} // namespace nztest
