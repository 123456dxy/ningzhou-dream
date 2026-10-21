#include "harness.h"
#include "core/api.h"
#include "core/artifact.h"
#include "core/arts.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/renown.h"
#include "core/save.h"
#include "core/sim.h"
#include <algorithm>

namespace nztest {
using namespace nz;
namespace {
Game fresh() {
    CreateOptions opts; opts.seed = 109411;
    Game g = createGame(opts); g.pending.reset(); g.job.reset(); g.npcs.clear();
    g.player.stones = 10000; return g;
}
renown::Source source(const Game& g, const string& id) {
    for (const auto& item : renown::sources(g)) if (item.id == id) return item;
    return {};
}
json::Value request(const string& route, const json::Value& body = json::Object{}) {
    return json::Value::parse(api::handle({"POST", "/api/" + route, body.dump(), ""}).body);
}
json::Value forgeInput(const string& main, const string& aux, const string& core, const string& shape) {
    json::Value o(json::Object{}); o.set("main", main); o.set("aux", aux); o.set("core", core); o.set("shape", shape); return o;
}
json::Value forgeInput(const string& main, const string& shape) {
    return forgeInput(main, main, main, shape);
}
}
void runRenownArtifactApiTests() {
    std::printf("\n--- 声望来源与法宝界面契约 ---\n");
    Game g = fresh(); const string before = saveToJson(g);
    const auto sources = renown::sources(g);
    checkEq(sources.size(), 9, "九条声望目录来自实际结算机制");
    checkStr(saveToJson(g), before, "查询声望资格不会修改随机数、天数或缺失的map键");
    for (const auto& item : sources)
        check(!item.id.empty() && !item.name.empty() && !item.gainText.empty() && !item.conditions.empty() &&
              !item.limit.empty() && !item.entry.tab.empty() && !item.reason.empty(), "来源包含收益、条件、限制和可定位入口：" + item.id);
    check(!source(g, "friend45").enabled && source(g, "friend45").id.empty(), "目录不虚构同道交情达到45的声望奖励");
    check(source(g, "explore").enabled && source(g, "explore").gainText.find("900") != string::npos, "游历明确每900个实际游历日获得一点声望");
    check(!source(g, "breakthrough").enabled, "修为未圆满不标记可以突破获得声望");
    g.player.location = "fanghu";
    check(source(g, "wildTask").enabled, "野外气血与报名费充足时悬赏资格可用");
    g.bonds.wildMonth = g.clock.days / 30; g.bonds.wildCount = 3;
    check(!source(g, "wildTask").enabled && source(g, "wildTask").limit.find("3 / 3") != string::npos, "悬赏显示已用三次月限并关闭资格");
    g.bonds.wildMonth = -1;
    check(source(g, "wildTask").enabled && source(g, "wildTask").limit.find("0 / 3") != string::npos, "其他月份的悬赏记录不占当前月份资格");
    g.player.health = 29;
    check(!source(g, "wildTask").enabled, "伤势低于30点不能报名悬赏");
    g.player.health = 100; g.player.location = "yifeng"; g.player.arts.progress.wins = 3;
    check(source(g, "oldblade").enabled, "旧剑归途显示真实所在地与胜场前置");
    g.bonds.stories["oldblade"] = g.year;
    check(!source(g, "oldblade").enabled && source(g, "oldblade").reason.find("不能再次") != string::npos, "完成过的仁厚支线不可重复获取声望");
    g.player.sect = "zhushan";
    check(!source(g, "sectTrial").enabled, "已入门的角色不能重复获取入门声望");
    g.tide.active = true; g.tide.severity = 3; g.tide.monthsLeft = 8;
    check(source(g, "beastTide").enabled && source(g, "beastTide").gainText.find("9") != string::npos, "兽潮按当前严重度显示每胜实际获得9点");
    g.player.arts.battle.emplace();
    check(!source(g, "explore").enabled && !source(g, "beastTide").enabled, "战斗中不把其他声望任务标记为可执行");

    Game oldApi = api::game(); api::game() = fresh(); auto& a = api::game();
    a.player.arts.materials.clear();
    const string unchanged = saveToJson(a);
    const auto state = request("state")["state"]; const auto catalog = state["renownInfo"]["sources"];
    checkEq(catalog.asArray().size(), 9, "API下发全部结构化声望来源");
    if (!catalog.asArray().empty()) check(catalog.asArray()[0]["conditions"].isArray() && catalog.asArray()[0]["entry"].isObject(), "声望条件数组与入口对象可直接渲染");
    const auto missing = request("forge-preview", forgeInput("not-material", "bell"));
    check(missing["ok"].asBool() && !missing["info"]["valid"].asBool() && missing["info"]["ability"].isNull(), "无效配料预览明确无能力，不默认造法宝");
    check(!missing["info"]["enabled"].asBool() && missing["info"]["materials"].asArray().empty() &&
          missing["info"]["stonesRequired"].asInt() == 0 && !missing["info"]["blockers"].asArray().empty(), "无效配料不显示伪造的开炉成本，明确阻止开炉");
    const auto dry = request("forge-preview", forgeInput("iron", "bell"));
    check(dry["info"]["valid"].asBool() && !dry["info"]["enabled"].asBool() && dry["info"]["ability"].isObject(), "无材料也可先查看器型能力，但不能开炉");
    const auto dryMaterials = dry["info"]["materials"].asArray();
    checkEq(dryMaterials.size(), 1, "三槽使用同料时只列一条材料成本");
    if (!dryMaterials.empty()) check(dryMaterials[0]["id"].asString() == "iron" &&
        dryMaterials[0]["name"].asString() == "锻剑铁" && dryMaterials[0]["required"].asInt() == 10 &&
        dryMaterials[0]["owned"].asInt() == 0 && dryMaterials[0]["missing"].asInt() == 10 &&
        dryMaterials[0]["roles"].asArray().size() == 3, "同料合计需求十份，三种用途、持有与缺口明确下发");
    checkStr(saveToJson(a), unchanged, "目录与无材料配方预览不推进时间、不耗材料或随机数");

    a.player.stones = 12; a.player.arts.materials = {{"iron", 3}, {"jade", 1}};
    const auto mixedInput = forgeInput("iron", "jade", "wood", "sword");
    const string poorBefore = saveToJson(a);
    const auto poor = request("forge-preview", mixedInput)["info"];
    const auto costs = poor["materials"].asArray();
    checkEq(costs.size(), 3, "不同材料按主、辅、灵核顺序各列成本");
    if (costs.size() == 3) check(costs[0]["required"].asInt() == 4 && costs[0]["owned"].asInt() == 3 && costs[0]["missing"].asInt() == 1 &&
        costs[1]["required"].asInt() == 3 && costs[1]["owned"].asInt() == 1 && costs[1]["missing"].asInt() == 2 &&
        costs[2]["required"].asInt() == 3 && costs[2]["owned"].asInt() == 0 && costs[2]["missing"].asInt() == 3,
        "预览准确列出三项材料的全部缺口，不只报告第一项");
    check(!poor["enabled"].asBool() && poor["stonesRequired"].asInt() == 33 && poor["stonesOwned"].asInt() == 12 &&
          poor["stonesMissing"].asInt() == 21 && poor["blockers"].asArray().size() == 4, "三项缺料和缺灵石同时下发，不受首个拒绝原因影响");
    checkStr(saveToJson(a), poorBefore, "缺料预览不插入桃木库存或消耗任何资源");
    check(!request("forge", mixedInput)["ok"].asBool() && saveToJson(a) == poorBefore, "缺料缺资粮的真实开炉被拒绝且状态不变");
    a.player.stones = 10000;
    const auto materialReason = request("forge-preview", mixedInput)["info"]["reason"].asString();
    check(materialReason.find("锻剑铁") != string::npos && materialReason.find("岩玉") != string::npos &&
          materialReason.find("桃木") != string::npos, "真实缺料校验也用中文名称列出全部材料");

    a.player.arts.materials = {{"iron", 9}};
    const string nineBefore = saveToJson(a);
    const auto nine = request("forge-preview", forgeInput("iron", "bell"))["info"];
    check(!nine["enabled"].asBool() && nine["materials"].asArray()[0]["missing"].asInt() == 1,
          "同料九份不能因每个槽位单独够用而误报可开炉");
    check(!request("forge", forgeInput("iron", "bell"))["ok"].asBool() && saveToJson(a) == nineBefore,
          "真实开炉严格拒绝同料合计差一份的配方");
    a.player.arts.materials["iron"] = 10;
    const auto enough = request("forge-preview", forgeInput("iron", "bell"))["info"];
    check(enough["enabled"].asBool() && enough["blockers"].asArray().empty() && enough["stonesMissing"].asInt() == 0,
          "材料灵石全部足量时明确可开炉且没有缺口提示");
    ecoBegin(a, "forge", "iron|iron|iron|bell");
    check(a.job.has_value() && a.player.arts.materials["iron"] == 0 && a.player.stones == 9967,
          "真实开炉与预览共用聚合需求，恰好十份一次扣尽并支付33灵石");
    a = fresh(); a.player.arts.materials = {{"iron", 7}, {"wood", 3}};
    const auto pair = request("forge-preview", forgeInput("iron", "iron", "wood", "sword"))["info"];
    check(pair["enabled"].asBool() && pair["materials"].asArray().size() == 2 &&
          pair["materials"].asArray()[0]["required"].asInt() == 7 && pair["materials"].asArray()[0]["roles"].asArray().size() == 2,
          "主辅同料合计七份，独立灵核三份，成本和角色均正确合并");
    const auto invalidShape = request("forge-preview", forgeInput("iron", "not-shape"))["info"];
    check(!invalidShape["valid"].asBool() && !invalidShape["enabled"].asBool() && invalidShape["materials"].asArray().empty(),
          "无效器型不显示可开炉或错误材料账单");
    a.player.arts.materials = {{"copper", 10}};
    const auto stageGate = request("forge-preview", forgeInput("copper", "sword"))["info"];
    check(stageGate["valid"].asBool() && !stageGate["enabled"].asBool() && stageGate["reason"].asString().find("境界不足") != string::npos,
          "高阶材料足量时仍保留炼制境界门槛");
    a.player.stage = 3;
    const auto craftGate = request("forge-preview", forgeInput("copper", "sword"))["info"];
    check(!craftGate["enabled"].asBool() && craftGate["reason"].asString().find("器道不足") != string::npos,
          "满足境界后仍校验器道等级");
    a.player.arts.exp["器"] = 30;
    check(request("forge-preview", forgeInput("copper", "sword"))["info"]["enabled"].asBool(),
          "材料、灵石、境界与器道全部满足后高阶炼器才启用");
    a = fresh(); a.player.arts.materials = {{"iron", 10}};
    for (int gate = 0; gate < 4; ++gate) {
        if (gate == 0) a.ended = true;
        if (gate == 1) a.pending.emplace();
        if (gate == 2) a.job.emplace();
        if (gate == 3) a.player.arts.battle.emplace();
        const auto blocked = request("forge-preview", forgeInput("iron", "bell"))["info"];
        check(blocked["valid"].asBool() && !blocked["enabled"].asBool() && !blocked["reason"].asString().empty() &&
              !blocked["blockers"].asArray().empty() && blocked["blockers"].asArray()[0].asString() == blocked["reason"].asString(),
              "足料预览仍保留寿尽、事件、日程或斗法门槛：" + std::to_string(gate));
        a.ended = false; a.pending.reset(); a.job.reset(); a.player.arts.battle.reset();
    }
    a.player.stones = 0; a.player.arts.materials.clear(); a.pending.emplace();
    const auto eventPoor = request("forge-preview", mixedInput)["info"];
    check(!eventPoor["enabled"].asBool() && eventPoor["blockers"].asArray().size() == 5 &&
          eventPoor["blockers"].asArray()[0].asString() == eventPoor["reason"].asString(),
          "事件门槛与全部材料、灵石缺口同时显示");
    a.pending.reset(); a.player.stones = 10000;
    a.player.arts.materials["iron"] = 30;
    for (const auto& shape : data::ShapeDef_TABLE()) {
        const auto preview = request("forge-preview", forgeInput("iron", shape.id))["info"];
        check(preview["enabled"].asBool() && preview["shapeId"].asString() == shape.id && preview["ability"]["shape"].asString() == shape.id &&
              preview["ability"]["cost"].isNumber() && preview["ability"]["cooldown"].isNumber(), "每种器型的配方预览下发实际能力与消耗：" + shape.id);
    }
    Gear bell; bell.id = "crafted-bell"; bell.name = "锻剑铁钟"; bell.slot = "armor"; bell.shape = "bell";
    bell.element = "剑"; bell.polarity = "天阳"; bell.crafted = true; bell.grade = 1;
    a.player.arts.gear.push_back(bell); a.player.arts.equipped["armor"] = bell.id;
    EnemySpec enemy; enemy.name = "法宝接口靶子"; enemy.beast = true; battleStart(a, enemy);
    a.player.arts.battle->mana = 0;
    const auto battleState = request("state")["state"];
    const auto abilities = battleState["battle"]["artifacts"];
    checkEq(abilities.asArray().size(), 1, "战斗下发已装备自炼法宝的独立常驻能力");
    if (!abilities.asArray().empty()) check(abilities.asArray()[0]["playable"].asBool() && abilities.asArray()[0]["cost"].asInt() == 0, "零费钟灵力不足时也可主动使用");
    json::Value cast(json::Object{}); cast.set("id", "artifact:" + bell.id);
    check(request("battle/turn", cast)["ok"].asBool(), "法宝用已有战斗API直接施放无需手牌uid");
    const auto cooling = request("state")["state"]["battle"]["artifacts"].asArray();
    check(!cooling.empty() && !cooling[0]["playable"].asBool() && cooling[0]["cooldownRemaining"].asInt() > 0 && !cooling[0]["reason"].asString().empty(), "法宝冷却与不可用原因真实下发");
    const string cooldownSave = saveToJson(a);
    check(!request("battle/turn", cast)["ok"].asBool() && saveToJson(a) == cooldownSave, "重复催动被后端拒绝且不改真实状态");
    api::game() = std::move(oldApi);
}
} // namespace nztest
