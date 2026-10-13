#include "core/location_services.h"
#include <algorithm>
#include "core/arts.h"
#include "core/battle.h"
#include "core/crafting/herb_acquisition.h"
#include "core/economy.h"
#include "core/sim.h"
#include "core/source_limits.h"
namespace nz {
namespace {
// 用途：按地点映射本地采集与感悟的大道。
// 参与流程：地点服务列表及完成奖励。
// 使用方式：传地点 ID，未配置时归气道。
// 返回值：本地大道旧键。
string localDao(const string& id) {
    if (id == "xinghe" || id == "fenglei" || id == "sea" || id == "daiyu") return "水";
    if (id == "jinhong" || id == "lianshi" || id == "yifeng") return "金";
    if (id == "lihuo" || id == "wojiao") return "火";
    if (id == "huachen" || id == "zhengyang" || id == "qingshi") return "土";
    if (id == "zhushan" || id == "fanghu" || id == "yunxi" || id == "jiuyi") return "木";
    if (id == "tianji" || id == "xiuluo" || id == "xuandao" || id == "tianmo") return "神";
    return "气";
}
}
// 用途：列出指定地点的服务和真实资格。
// 参与流程：地图及地点详情展示。
// 使用方式：传游戏与地点 ID，只读查询费用、耗时和阻塞。
// 返回值：服务列表；地点无效时为空。
vector<LocationService> locationServices(const Game& g, const string& id) {
    vector<LocationService> result;
    const auto* loc = findLoc(id);
    if (!loc) return result;
    const string type = loc->type;
    auto add = [&](string key, string name, string desc, int days, int cost, int stage = 0, string tab = "") {
        LocationService service; service.id = key; service.name = name; service.desc = desc;
        service.days = days; service.cost = cost; service.stage = stage; service.targetTab = tab;
        if (g.player.location != id) service.reason = "请先抵达此地。";
        else if (g.ended || g.pending || g.player.arts.battle || g.job) service.reason = "请先完成当前安排、事件或斗法。";
        else if (g.player.stage < stage) service.reason = "需达到" + realmName(stage) + "。";
        else if (g.player.stones < cost) service.reason = "灵石不足。";
        else if (key == "gather" && g.world.herbs < 3) service.reason = "药材资源不足，请等待恢复。";
        else if (key == "cores" && g.world.coreStock <= 0) service.reason = "灵核库存已耗尽。";
        else if (key == "insight" && g.flags.count("local:" + id) && g.flags.at("local:" + id) == g.clock.days / 30 + 1) service.reason = "本月已在此感悟。";
        service.enabled = service.reason.empty(); result.push_back(std::move(service));
    };
    if (type == "city") {
        string marketName = id == "yunxi" ? "中草阁" : id == "guangling" ? "百宝行" : id == "yifeng" ? "宝器轩" : id == "wuling" ? "灵药堂" : "坊市";
        add("market", marketName + "·交易", "丹药、药材、材料、法器与抄本交易。", 0, 0, 0, "market");
    }
    if (findSect(id)) {
        add("sect", "宗门差事与藏经阁", "拜入宗门后领取差事、俸禄和传承。", 0, 0, 0, "sect");
        add("insight", localDao(id) + "系传功", localDao(id) + "道经验+12；每月一次。", 3, 6);
    } else if (id == "tianji") {
        add("insight", "外务研读", "研读各地情报：神道经验+12；每月一次。", 2, 8);
        add("intel", "秘境情报兑换", "查看并兑换野外秘境情报，探索高阶药材与炼器材料的来源。", 0, 0, 0, "dungeon");
    } else if (id == "xuandao") {
        add("insight", "天魔眼集结讲法", "研究魔物应对：神道经验+12；每月一次。", 3, 8, 3);
    } else if (type == "wild") {
        const int stage = loc->danger >= 60 ? 3 : 0;
        add("gather", "本地采药", "采集当前境界可得的本地草药3份、木道经验+4；取得难度按最早丹方用途匹配，高品草药仍受原生境限制，妖丹需猎妖或秘境。", 4, 2, stage);
        add("insight", id == "lianshi" ? "剑壁感悟" : id == "fanghu" ? "碧幽潭感悟" : id == "xiuluo" ? "古迹调查" : "山川感悟",
            localDao(id) + "道经验+12；每月一次。", 4, 5, stage);
        if (id == "qingshi" || id == "tianmo")
            add("cores", "灵核采集", "采集灵核1枚、器道经验+4，消耗世界库存。", 6, 6, id == "tianmo" ? 6 : 0);
        else if (id == "wojiao" || id == "lianshi" || id == "zhengyang" || id == "fenglei" || id == "sea")
            add("mineral", "本地采矿", "采集低阶矿材1份、器道经验+4，最多二阶；高阶材料需探索秘境。", 5, 4, stage);
    }
    if (id == "wuling") add("hunt", "白帝楼悬赏", "前往白帝楼任务面板，猎妖后获取灵石与声望。", 0, 0, 0, "city");
    if (id == "yifeng") add("insight", "斗法台观摩", "观摩金系连招：金道经验+12；每月一次。", 3, 5);
    if (id == "yunxi") add("insight", "药阁辨药", "辨识药草：木道经验+12、丹道经验+6；每月一次。", 3, 5);
    if (id == "tianxing") add("insight", "问道楼研读", "研读入门丹器遁法：气道经验+12、器道经验+6；每月一次。", 3, 8);
    if (id == "guangling") add("mineral", "百宝行材料包", "购入1份符合境界的一至二阶材料、器道经验+4。", 1, 18);
    if (id == "dongshi") {
        add("insight", "仙师集会", "与集会修士交流：气道经验+12；每月一次。", 3, 5);
        add("auction", "伴月楼拍卖会", "低阶材料竞购：二阶材料1份；高阶珍材需前往秘境。", 2, 24, 3);
    }
    if (id == "yongan") add("insight", "学堂静心", "气道经验+12、心境+1；每月一次。", 3, 4);
    return result;
}
// 用途：执行服务导航或收费建立地点工作。
// 参与流程：地点服务动作入口。
// 使用方式：传游戏、服务 ID 和可写 targetTab，按当前位置服务校验。
// 返回值：成功为空；填写导航页或更新灵石、月标记和 Job。
string locationServiceBegin(Game& g, const string& id, string& targetTab) {
    for (const auto& service : locationServices(g, g.player.location)) {
        if (service.id != id) continue;
        if (!service.enabled) return service.reason;
        targetTab = service.targetTab;
        if (!targetTab.empty()) return "";
        if (id == "cores" && g.world.coreStock <= 0) return "灵核库存已耗尽。";
        g.player.stones -= service.cost;
        if (id == "insight") g.flags["local:" + g.player.location] = g.clock.days / 30 + 1;
        Job job; job.actionType = "locationService"; job.kind = id; job.name = service.name; job.origin = g.player.location;
        job.total = job.remaining = service.days; job.started = g.clock.days; job.cost = service.cost;
        g.job = job; return "";
    }
    return "此地没有这项功能。";
}
// 用途：兑现地点感悟或真实采集奖励。
// 参与流程：地点 Job 完成回调。
// 使用方式：传游戏与 Job，按 origin/kind 使用当地与世界库存规则。
// 返回值：无；更新成长、行囊、世界资源、RNG 和完成日志。
void locationServiceComplete(Game& g, const Job& job) {
    Player& p = g.player;
    const string dao = localDao(job.origin);
    if (job.kind == "insight") {
        artGain(p, dao, 12);
        if (job.origin == "yunxi") artGain(p, "丹", 6);
        if (job.origin == "tianxing") artGain(p, "器", 6);
        if (job.origin == "yongan") p.arts.heart += 1;
    } else if (job.kind == "gather") {
        const auto pool = crafting::mfGatherableHerbs(p, job.origin);
        const int count = std::min(3, g.world.herbs);
        if (!pool.empty()) { p.arts.herbs[pickOf(g, pool)->name] += count; p.herbs += count; g.world.herbs -= count; }
        artSyncHerbs(p); artGain(p, "木", 4);
    } else if (job.kind == "cores") {
        const int count = std::min(1, g.world.coreStock);
        g.world.coreStock -= count; p.cores += count; artGain(p, "器", 4);
    } else if (job.kind == "mineral" || job.kind == "auction") {
        vector<const data::MaterialDef*> pool;
        // 旧存档中正在进行的材料包/拍卖也共用新上限，不能借存量任务绕过。
        const int grade = ordinaryMaterialGradeLimit(p);
        for (const auto& m : data::MaterialDef_TABLE())
            if (m.grade == grade && m.element == dao) pool.push_back(&m);
        if (pool.empty()) for (const auto& m : data::MaterialDef_TABLE()) if (m.grade == grade) pool.push_back(&m);
        if (!pool.empty()) p.arts.materials[pickOf(g, pool)->id] += 1;
        artGain(p, "器", 4);
    }
    logEvent(g, "self", "完成「" + job.name + "」。当地修行与采集所得已入行囊。");
}
}
