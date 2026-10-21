#include "harness.h"

#include <algorithm>

#include "core/api.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/cultivate.h"
#include "core/crafting/herb_acquisition.h"
#include "core/content/forge_content.h"
#include "core/economy.h"
#include "core/json.h"
#include "core/location_services.h"
#include "core/play.h"
#include "core/save.h"
#include "core/shop.h"
#include "core/sim.h"
#include "core/source_limits.h"

namespace nztest {
using namespace nz;
namespace {
Game sourceGame(int stage = 0) {
    Game g = createGame(CreateOptions{});
    g.player.stage = g.player.highestStage = stage;
    g.player.location = "wuling";
    g.player.lifespan = realmOf(stage).life + g.player.lifeBonus;
    g.player.stones = 100000;
    g.pending.reset(); g.job.reset(); g.player.arts.battle.reset();
    return g;
}

void safeBuyReject(Game& g, const string& kind, const string& id, const string& label) {
    const string before = saveToJson(g);
    check(!play::marketBuy(g, kind, id, 1).empty(), label + "被拒绝");
    check(saveToJson(g) == before, label + "不扣款、不生成物品");
}

bool lowMaterialsOnly(const Arts& arts) {
    for (const auto& m : data::MaterialDef_TABLE()) {
        const auto found = arts.materials.find(m.id);
        if (m.grade > 2 && found != arts.materials.end() && found->second > 0) return false;
    }
    return true;
}

http::Response sourceRequest(const string& route, const json::Value& body = json::Object{}) {
    return api::handle({"POST", "/api/" + route, body.dump(), ""});
}
json::Value buyInput(const string& kind, const string& id) {
    json::Value body(json::Object{});
    body.set("kind", kind); body.set("id", id); body.set("count", 1);
    return body;
}
json::Value supplyInput(const string& kind, int level) {
    json::Value body(json::Object{}); body.set("kind", kind); body.set("level", level); return body;
}
void apiSourceReject(const string& route, const json::Value& body, const string& label) {
    const string before = saveToJson(api::game());
    check(sourceRequest(route, body).status >= 400, label + "接口拒绝");
    check(saveToJson(api::game()) == before, label + "接口不改变存档");
}
}

// 此入口须排在 runApiTests 之后，以保留其冷启动断言。
void runMaterialSourceTests() {
    std::printf("\n===== 材料来源与洞府档位回归 =====\n");
    {
        Game g = sourceGame(12);
        for (const auto& herb : data::HerbDef_TABLE()) {
            if (herb.grade < 3) continue;
            safeBuyReject(g, "herb", herb.id, herb.name + "高品药材");
        }
        const auto* high = findHerbById("6201");
        check(high != nullptr, "高品药材测试样本存在");
        if (high) safeBuyReject(g, "herb", high->name, "直接使用高品药材名称");
        for (const auto& material : data::MaterialDef_TABLE())
            if (material.grade > 2) safeBuyReject(g, "material", material.id, material.name + "高阶材料");
        safeBuyReject(g, "gear", "sword-3", "直接购买三阶法器");
        safeBuyReject(g, "gear", "pearl-5", "直接购买五阶法器");
        safeBuyReject(g, "gear", "sword-99999999999999999999", "伪造溢出法器品阶");
        for (const auto& item : play::marketList(g)) {
            if (item.kind == "herb") check(findHerbById(item.id)->grade <= 2, "高境界坊市药材仍至多二品");
            if (item.kind == "material") check(item.id != "gold" && item.id != "meteor" && item.id != "dragon", "坊市材料列表没有高阶直购");
        }
        for (const auto& item : play::gearList(g)) check(item.id.back() <= '2', "坊市成品法器至多二阶");
    }
    {
        Game g = sourceGame();
        safeBuyReject(g, "material", "copper", "炼气购买二阶火铜");
        const int before = g.player.stones;
        const int oldIron = g.player.arts.materials["iron"];
        check(play::marketBuy(g, "material", "iron", 2).empty(), "炼气可购买两份一阶材料");
        checkEq(g.player.arts.materials["iron"], oldIron + 2, "材料真实进入炼器行囊");
        check(g.player.stones < before, "材料购买真实扣除灵石");
        g.player.stage = g.player.highestStage = 3;
        check(play::marketBuy(g, "material", "copper", 1).empty(), "筑基解锁二阶材料直购");
        checkEq(g.player.arts.materials["copper"], 1, "二阶材料到手一份");
    }
    {
        Game g = sourceGame(12);
        g.player.arts.materials.clear();
        for (int i = 0; i < 25; ++i) artAnnual(g, "mine", "");
        check(lowMaterialsOnly(g.player.arts), "普通寻矿不能无限产三至五阶材料");
        g.player.arts.materials.clear();
        EnemySpec enemy; enemy.name = "常规猎妖"; enemy.stage = g.player.stage;
        enemy.kind = "hunt"; enemy.beast = true; enemy.huntFee = 100;
        battleStart(g, enemy);
        if (g.player.arts.battle) {
            g.player.arts.battle->enemyHp = 0;
            battleFinish(g, true);
        }
        int PartKinds = 0;
        bool MatchesEnemyGrade = true;
        for (const auto& [Id, Count] : g.player.arts.materials) {
            const auto* Definition = content::mfFindForgeMaterial(Id);
            if (!Count) continue;
            ++PartKinds;
            MatchesEnemyGrade &= Definition && Definition->Source == content::ForgeMaterialSource::BeastDrop &&
                Definition->Material.grade == 5 && Count == 1;
        }
        check(PartKinds == 5 && MatchesEnemyGrade, "猎妖按所杀五阶妖兽发五部位，不再按普通矿材二阶封顶");
    }
    {
        Game g = sourceGame(12);
        g.player.arts.herbs.clear(); g.player.herbs = 0;
        bool SourcesAllowed = true, NoMonsterCore = true;
        for (const auto& location : LOCATIONS()) {
            if (string(location.type) != "wild") continue;
            const auto PreviousHerbs = g.player.arts.herbs;
            const auto AllowedHerbs = crafting::mfGatherableHerbs(g.player, location.id);
            Job job; job.actionType = "locationService"; job.kind = "gather";
            job.origin = location.id; job.name = "采集边界检查";
            locationServiceComplete(g, job);
            for (const auto& [Name, Count] : g.player.arts.herbs) {
                const auto Previous = PreviousHerbs.find(Name);
                const int BeforeCount = Previous == PreviousHerbs.end() ? 0 : Previous->second;
                if (Count <= BeforeCount) continue;
                const auto* Herb = findHerbByName(Name);
                SourcesAllowed = SourcesAllowed && Herb &&
                    crafting::mfHerbAcquisitionStage(Herb->id) <= g.player.stage &&
                    std::any_of(AllowedHerbs.begin(), AllowedHerbs.end(), [&](const auto* Entry) { return Herb && Entry->id == Herb->id; });
                NoMonsterCore = NoMonsterCore && Herb && crafting::mfIsPlantHerb(Herb->id);
            }
        }
        check(SourcesAllowed && g.player.herbs > 0, "真实野外采药符合当前用途境界与对应地点权威生境且仍有产出");
        check(NoMonsterCore, "普通采药按稳定分类排除妖丹");
        g.player.arts.materials.clear();
        for (const string origin : {"wojiao", "lianshi", "guangling", "dongshi", "tianji"}) {
            Job job; job.kind = origin == "dongshi" || origin == "tianji" ? "auction" : "mineral";
            job.origin = origin; job.name = "旧材料任务";
            locationServiceComplete(g, job);
        }
        check(lowMaterialsOnly(g.player.arts), "普通采矿、材料包和旧高阶拍卖任务都封顶二阶");
        g.player.location = "tianji";
        string tab;
        check(!locationServiceBegin(g, "auction", tab).empty(), "天机阁旧珍品材料购买入口已关闭");
        check(locationServiceBegin(g, "intel", tab).empty() && tab == "dungeon" && !g.job,
              "天机阁入口打开秘境情报界面");
    }
    {
        Game g = sourceGame(); g.player.cave = "wuling";
        checkEq(shop::caveMaxSupply(g.player), 1, "初始洞府只解锁一档配给");
        const string before = saveToJson(g);
        check(!shop::caveSupplySet(g, "qi", 2).ok, "低档洞府不能设置二档灵气");
        check(saveToJson(g) == before, "拒绝高配给不改变资源");
        check(!shop::caveUpgrade(g).ok, "低声望不能升级洞府");
        check(saveToJson(g) == before, "声望不足不扣升级费");
        g.player.renown = 20;
        const int stones = g.player.stones;
        check(shop::caveUpgrade(g).ok, "声望20可升灵脉洞府");
        checkEq(g.player.caveTier, 1, "洞府档位真实升至一档");
        checkEq(g.player.stones, stones - 120, "灵脉洞府准确扣120灵石");
        check(shop::caveSupplySet(g, "qi", 2).ok && g.player.qiSupply == 2, "灵脉洞府可设二档灵气");
        check(!shop::caveSupplySet(g, "qi", 3).ok, "二档洞府仍不能设最高灵气配给");
        check(shop::caveSupplySet(g, "stone", 3).ok && g.player.stoneSupply == 3,
              "灵石投入可在任意洞府设三档，不受灵气档位限制");
        checkNear(cultivate::caveMul(g.player), 1.10, "灵脉气场在府内生效10%");
        g.player.renown = 60;
        const int secondStones = g.player.stones;
        check(shop::caveUpgrade(g).ok, "声望60可升聚灵仙府");
        checkEq(g.player.stones, secondStones - 400, "聚灵仙府准确扣400灵石");
        checkEq(shop::caveMaxSupply(g.player), 3, "聚灵仙府解锁三档配给");
        check(shop::caveSupplySet(g, "qi", 3).ok, "聚灵仙府可设最高灵气配给");
        checkNear(cultivate::caveMul(g.player), 1.20, "聚灵气场在府内生效20%");
        checkNear(cultivate::breakdown(g.player).total, cultivate::rate(g.player), "洞府展示速度与实际总速度一致");
        const string top = saveToJson(g);
        check(!shop::caveUpgrade(g).ok && saveToJson(g) == top, "最高档不可重复付费升档");
        g.player.location = "yifeng";
        checkNear(cultivate::caveMul(g.player), 1.0, "离开洞府时气场失效");
        checkNear(cultivate::qiSupplyMul(g.player), 1.0, "离开洞府时灵气配给失效");
        check(shop::caveMove(g).ok && g.player.cave == "yifeng" && g.player.caveTier == 2, "迁府保留已购最高档位");
        Job job; job.actionType = "plan"; job.total = job.remaining = 3; job.plan = "rest"; g.job = job;
        const string busy = saveToJson(g);
        check(!shop::caveSupplySet(g, "qi", 0).ok && saveToJson(g) == busy, "安排途中不能调整配给破坏结算快照");
        check(!shop::caveMove(g).ok && saveToJson(g) == busy, "安排途中不能迁府");
        g.job.reset(); g.player.cave.clear();
        check(!shop::caveSupplySet(g, "qi", 1).ok, "未安府无法启用灵气配给");
        check(!shop::caveSupplySet(g, "stone", -1).ok && !shop::caveSupplySet(g, "stone", 4).ok,
              "负数或超范围配给不能被静默截断");
    }
    // API 必须和列表、规则函数使用相同限制，不能靠手写请求绕过。
    {
        Game& g = api::game(); const Game old = g; g = sourceGame(12);
        apiSourceReject("buy", buyInput("herb", "6201"), "手写三品药材购买");
        apiSourceReject("buy", buyInput("material", "dragon"), "手写五阶材料购买");
        check(sourceRequest("buy", buyInput("material", "copper")).status == 200,
              "材料购买接口可实际购买合法二阶材料");
        g.player.cave = "wuling"; g.player.caveTier = 0; g.player.renown = 19;
        apiSourceReject("supply", supplyInput("qi", 3), "手写最高档灵气");
        apiSourceReject("cave/upgrade", {}, "手写声望不足升档");
        g.player.renown = 20;
        const int before = g.player.stones;
        check(sourceRequest("cave/upgrade").status == 200 && g.player.caveTier == 1,
              "升级接口实际升档");
        checkEq(g.player.stones, before - 120, "升级接口扣正确费用");
        check(sourceRequest("supply", supplyInput("qi", 2)).status == 200 && g.player.qiSupply == 2,
              "升级后接口允许二档配给");
        g = old;
    }
}
} // namespace nztest
