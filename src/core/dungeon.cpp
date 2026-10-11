#include "core/dungeon.h"

#include <algorithm>
#include <set>
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/sim.h"

namespace nz {
namespace {

uint32_t hashText(const string& text) {
    uint32_t result = 2166136261u;
    for (unsigned char c : text) result = (result ^ c) * 16777619u;
    return result;
}
Rng localRng(const Game& g, const string& tag) {
    return Rng(mixSeed(g.seed ^ hashText(tag)));
}
int currentYear(const Game& g) { return calendarDate(g).year; }
int minimumStage(int grade) { return clampInt((grade - 1) * 3, 0, 12); }
int matchedGrade(int stage, const string& kind) {
    return std::min(kind == "herb" ? 6 : 5, 1 + clampInt(stage, 0, 12) / 3);
}
string busyError(const Game& g) {
    if (g.ended || g.player.health <= 0) return "此世已结束或伤势过重。";
    if (g.pending || g.job || g.player.arts.battle) return "请先完成当前安排、事件或斗法。";
    if (g.dungeons.run) return "请先结束当前秘境探索。";
    return "";
}
string runError(const Game& g) {
    if (!g.dungeons.run) return "没有正在探索的秘境。";
    if (g.ended || g.pending || g.job) return "请先处理当前事件。";
    if (g.player.location != g.dungeons.run->location) return "秘境探索期间不能离开入口所在地。";
    return "";
}
DungeonSite* siteOf(Game& g, const string& id) {
    for (auto& site : g.dungeons.sites) if (site.id == id) return &site;
    return nullptr;
}
const DungeonNode* nodeOf(const DungeonRun& run, const string& id) {
    for (const auto& node : run.nodes) if (node.id == id) return &node;
    return nullptr;
}
DungeonNode* nodeOf(DungeonRun& run, const string& id) {
    for (auto& node : run.nodes) if (node.id == id) return &node;
    return nullptr;
}
const DungeonRelicDef* relicOf(const string& id) {
    for (const auto& relic : dungeonRelics()) if (relic.id == id) return &relic;
    return nullptr;
}
bool hasRelic(const DungeonRun& run, const string& id) {
    return std::find(run.relics.begin(), run.relics.end(), id) != run.relics.end();
}
bool knowsSpell(const Game& g, const string& id) {
    const auto& a = g.player.arts;
    return std::find(a.knownSpells.begin(), a.knownSpells.end(), id) != a.knownSpells.end() ||
        std::find(a.spells.begin(), a.spells.end(), id) != a.spells.end();
}
bool ownsManual(const Game& g, const string& id) {
    const auto& a = g.player.arts;
    const auto& books = g.economy.books;
    const auto found = a.manuals.find(id);
    return (found != a.manuals.end() && found->second > 0) ||
        std::find(a.bookList.begin(), a.bookList.end(), id) != a.bookList.end() ||
        std::find(books.begin(), books.end(), id) != books.end();
}
int bossStones(int stage, const string& type) {
    return 250 + clampInt(stage, 0, 12) * 80 + (type == "sea" ? 100 : type == "tianmo" ? 180 : 0);
}
int bossFallback(const DungeonRun& run) {
    int amount = 0;
    for (const auto& reward : run.bossRewards) if (reward.kind == "stones") amount = std::max(amount, reward.amount);
    return amount;
}
bool sameReward(const DungeonReward& a, const DungeonReward& b) {
    return a.id == b.id && a.kind == b.kind && a.name == b.name && a.desc == b.desc &&
        a.spell == b.spell && a.manual == b.manual && a.relic == b.relic && a.amount == b.amount && a.grade == b.grade;
}
bool sameRewards(const vector<DungeonReward>& a, const vector<DungeonReward>& b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), sameReward);
}
void freezeBossRewards(const Game& g, DungeonSite& site) {
    Rng rng = localRng(g, site.id + ":boss-rewards:" + std::to_string(site.stage));
    vector<const SpellDef*> spells;
    vector<const data::BaseManualDef*> manuals;
    for (const auto& spell : allSpells()) if (spell.id != "strike" && spell.id != "defend" && spell.stage <= site.stage && !knowsSpell(g, spell.id)) spells.push_back(&spell);
    for (const auto& manual : data::BaseManualDef_TABLE()) if (manual.stage <= site.stage && !ownsManual(g, manual.id)) manuals.push_back(&manual);
    const auto currentTierSpell = std::find_if(spells.begin(), spells.end(), [&](const auto* spell) { return spell->stage / 3 == site.stage / 3; });
    if (currentTierSpell != spells.end()) spells.erase(std::remove_if(spells.begin(), spells.end(), [&](const auto* spell) { return spell->stage / 3 != site.stage / 3; }), spells.end());
    const auto currentTierManual = std::find_if(manuals.begin(), manuals.end(), [&](const auto* manual) { return manual->stage / 3 == site.stage / 3; });
    if (currentTierManual != manuals.end()) manuals.erase(std::remove_if(manuals.begin(), manuals.end(), [&](const auto* manual) { return manual->stage / 3 != site.stage / 3; }), manuals.end());
    site.bossRewards.clear();
    const int stones = bossStones(site.stage, site.type);
    if (!spells.empty()) {
        const auto* spell = pick(rng, spells);
        DungeonReward reward;
        reward.id = "boss-spell"; reward.kind = "learnSpell"; reward.name = "永久神通·" + spell->name;
        reward.spell = spell->id;
        reward.desc = "永久领悟「" + spell->name + "」，离开后仍保留；不自动装备。若已经学会，固定折算" + std::to_string(stones) + "灵石。";
        site.bossRewards.push_back(std::move(reward));
    }
    if (!manuals.empty()) {
        const auto* manual = pick(rng, manuals);
        DungeonReward reward;
        reward.id = "boss-manual"; reward.kind = "manual"; reward.name = "功法抄本·" + manual->name;
        reward.manual = manual->id;
        reward.desc = "获得《" + manual->name + "》永久抄本，可在洞府修习；不自动修炼。若已拥有，固定折算" + std::to_string(stones) + "灵石。";
        site.bossRewards.push_back(std::move(reward));
    }
    DungeonReward reward;
    reward.id = "boss-stones"; reward.kind = "stones"; reward.name = "灵石宝藏·" + std::to_string(stones);
    reward.amount = stones; reward.desc = "获得" + std::to_string(stones) + "灵石，存入探索行囊并在离开时带出。";
    site.bossRewards.push_back(std::move(reward));
}
string randomWildLocation(Rng& rng) {
    vector<string> pool;
    for (const auto& location : LOCATIONS())
        if (string(location.type) == "wild" && string(location.id) != "sea" && string(location.id) != "tianmo") pool.push_back(location.id);
    return pick(rng, pool);
}
void spawnWild(Game& g, int year) {
    DungeonSite site;
    site.id = "wild-" + std::to_string(year) + "-" + std::to_string(++g.dungeons.seq);
    Rng rng = localRng(g, site.id + ":site");
    site.location = randomWildLocation(rng);
    static const vector<string> names = {"云隐古洞", "伏龙遗府", "青萝秘境", "忘尘石窟", "听雷幽谷", "栖霞古塔"};
    site.name = pick(rng, names); site.type = "wild";
    site.openedYear = year; site.expiresYear = year + (g.player.stage < 9 ? 3 : 50);
    site.stage = clampInt(g.player.stage, 0, 12); site.revealed = true;
    freezeBossRewards(g, site);
    g.dungeons.sites.push_back(std::move(site));
    g.dungeons.lastSpawnYear = year;
}
void synchronizeSpecials(Game& g, int year) {
    struct Special { const char* type; const char* name; const char* location; int period, duration, stage; };
    for (const auto& special : vector<Special>{{"sea", "无尽海·潮生遗境", "sea", 10, 2, 3}, {"tianmo", "天魔眼·封魔古塔", "tianmo", 50, 5, 6}}) {
        const int opening = year / special.period * special.period;
        if (opening < special.period || year >= opening + special.duration) continue;
        const string id = string(special.type) + "-" + std::to_string(opening);
        if (siteOf(g, id) || g.flags.count("dungeon-enter:" + id)) continue;
        DungeonSite site;
        site.id = id; site.type = special.type; site.name = special.name; site.location = special.location;
        site.openedYear = opening; site.expiresYear = opening + special.duration;
        site.stage = std::max(special.stage, clampInt(g.player.stage, 0, 12));
        freezeBossRewards(g, site); g.dungeons.sites.push_back(std::move(site));
    }
}
void pruneSites(Game& g) {
    const int year = currentYear(g);
    const string active = g.dungeons.run ? g.dungeons.run->siteId : "";
    auto& sites = g.dungeons.sites;
    sites.erase(std::remove_if(sites.begin(), sites.end(), [&](const auto& site) {
        return site.id != active && site.expiresYear + 5 < year;
    }), sites.end());
}
string nodeName(const string& type) {
    if (type == "battle") return "守境妖兽";
    if (type == "elite") return "精英守卫";
    if (type == "camp") return "灵泉营地";
    if (type == "event") return "古迹奇遇";
    if (type == "chest") return "封存宝匣";
    if (type == "merchant") return "秘境游商";
    return "镇境首领";
}
void createRoute(const Game& g, DungeonRun& run) {
    Rng rng = localRng(g, run.siteId + ":route:" + std::to_string(run.stage));
    const int floors = 9 + run.stage / 3;
    for (int row = 0; row < floors; ++row) {
        for (int lane = 0; lane < 3; ++lane) {
            if (row == floors - 1 && lane != 1) continue;
            DungeonNode node;
            node.id = "floor-" + std::to_string(row) + "-" + std::to_string(lane);
            node.row = row; node.lane = lane;
            if (row == floors - 1) node.type = "boss";
            else if (row == 0) node.type = "battle";
            else if (row == floors - 2 || row == 4) node.type = "camp";
            else if (row == 1) node.type = vector<string>{"event", "chest", "battle"}[lane];
            else if (row == 2) node.type = lane == 1 ? "elite" : "battle";
            else if (row == 3) node.type = vector<string>{"merchant", "battle", "chest"}[lane];
            else node.type = pick(rng, vector<string>{"battle", "battle", "elite", "event", "chest", "merchant"});
            node.name = nodeName(node.type);
            node.text = "第" + std::to_string(row + 1) + "层 · " + node.name;
            if (row < floors - 1) {
                if (row == floors - 2) node.next.push_back("floor-" + std::to_string(row + 1) + "-1");
                else {
                    node.next.push_back("floor-" + std::to_string(row + 1) + "-" + std::to_string(lane));
                    int adjacent = lane == 0 ? 1 : lane == 2 ? 1 : randInt(rng, 0, 1) * 2;
                    if (rng.next() < 0.75) node.next.push_back("floor-" + std::to_string(row + 1) + "-" + std::to_string(adjacent));
                }
            }
            run.nodes.push_back(std::move(node));
        }
    }
}
DungeonReward makeReward(DungeonRun& run, const string& kind, const string& name, const string& desc, int amount = 0) {
    DungeonReward reward;
    reward.id = "choice-" + std::to_string(++run.seq); reward.kind = kind;
    reward.name = name; reward.desc = desc; reward.amount = amount;
    return reward;
}
void cardChoices(const Game& g, DungeonRun& run, bool merchant = false) {
    if (run.deck.size() >= 60) return;
    Rng rng = localRng(g, run.siteId + ":cards:" + run.current + ":" + std::to_string(run.seq));
    vector<const SpellDef*> pool;
    for (const auto& spell : allSpells()) if (spell.id != "strike" && spell.id != "defend" && spell.stage <= run.stage) pool.push_back(&spell);
    for (int i = 0; i < 3 && !pool.empty(); ++i) {
        const size_t selected = static_cast<size_t>(rng.next() * pool.size());
        const auto* spell = pool[selected]; pool.erase(pool.begin() + selected);
        auto reward = makeReward(run, merchant ? "buyCard" : "card", spell->name, "将1张加入本次秘境的临时牌组；离开后消失。", merchant ? 18 + run.stage * 3 : 1);
        reward.spell = spell->id; run.rewards.push_back(std::move(reward));
    }
}
void relicChoices(const Game& g, DungeonRun& run, bool merchant = false) {
    Rng rng = localRng(g, run.siteId + ":relics:" + run.current + ":" + std::to_string(run.seq));
    vector<const DungeonRelicDef*> pool;
    for (const auto& relic : dungeonRelics()) if (!hasRelic(run, relic.id)) pool.push_back(&relic);
    for (int i = 0; i < (merchant ? 1 : 3) && !pool.empty(); ++i) {
        const size_t selected = static_cast<size_t>(rng.next() * pool.size());
        const auto* relic = pool[selected]; pool.erase(pool.begin() + selected);
        auto reward = makeReward(run, merchant ? "buyRelic" : "relic", relic->name, relic->desc, merchant ? 35 + run.stage * 5 : 1);
        reward.relic = relic->id; run.rewards.push_back(std::move(reward));
    }
    if (run.rewards.empty()) run.rewards.push_back(makeReward(run, "rest", "灵泉余韵", "恢复20%气血。", 20));
}
void bankMaterial(const Game& g, DungeonRun& run, int count) {
    Rng rng = localRng(g, run.siteId + ":materials:" + run.current);
    const bool herb = rng.next() < 0.5;
    const int grade = std::min(2, matchedGrade(run.stage, herb ? "herb" : "material"));
    if (herb) {
        vector<const data::HerbDef*> pool;
        for (const auto& item : data::HerbDef_TABLE()) if (item.grade == grade) pool.push_back(&item);
        if (!pool.empty()) run.herbs[pick(rng, pool)->name] += count;
    } else {
        vector<const data::MaterialDef*> pool;
        for (const auto& item : data::MaterialDef_TABLE()) if (item.grade == grade) pool.push_back(&item);
        if (!pool.empty()) run.materials[pick(rng, pool)->id] += count;
    }
}
void grantTarget(DungeonRun& run) {
    if (run.targetGranted) return;
    if (run.targetKind == "herb") run.herbs[run.targetName] += 10;
    else if (run.targetKind == "material") run.materials[run.targetId] += 10;
    run.targetGranted = true;
}
void discardUnclearedSite(Game& g, const DungeonRun& run) {
    if (run.cleared) return;
    auto& sites = g.dungeons.sites;
    sites.erase(std::remove_if(sites.begin(), sites.end(), [&](const DungeonSite& site) {
        return site.id == run.siteId;
    }), sites.end());
}
void settleRun(Game& g, bool lost) {
    const DungeonRun run = *g.dungeons.run;
    auto amount = [&](int count) { return lost ? count / 2 : count; };
    g.player.stones += amount(run.stones); g.player.cores += amount(run.cores);
    g.player.insight = clampInt(g.player.insight + amount(run.insight), INSIGHT_MIN, INSIGHT_MAX);
    for (const auto& [name, count] : run.herbs) { g.player.arts.herbs[name] += amount(count); g.player.herbs += amount(count); }
    for (const auto& [id, count] : run.materials) g.player.arts.materials[id] += amount(count);
    for (const auto& [id, count] : run.potions) g.player.arts.potions[id] += amount(count);
    artSyncHerbs(g.player);
    g.player.health = std::max(1, run.health);
    discardUnclearedSite(g, run);
    g.dungeons.run.reset();
    if (run.elapsedDays > 0) {
        Job job; job.actionType = "socialElapsed"; job.kind = "dungeon";
        job.name = run.siteName + "·探索耗时"; job.total = job.remaining = run.elapsedDays;
        job.started = g.clock.days; job.origin = run.location;
        g.job = std::move(job);
    }
    logEvent(g, "self", "结束「" + run.siteName + "」探索：" + (run.cleared ? "通关" : lost ? "战败撤离，保留一半已得物资" : "主动撤离，带回已得物资") +
             (run.cleared ? "。" : "，未通关的秘境入口已消失。") + "临时神通与秘境遗物消散，实际耗时" + std::to_string(run.elapsedDays) + "天。", true);
}
bool validTarget(const string& kind, const string& id, const string& name, int grade) {
    if (kind == "herb") { const auto* item = findHerbById(id); return item && item->name == name && item->grade == grade; }
    if (kind == "material") for (const auto& item : data::MaterialDef_TABLE()) if (item.id == id) return item.name == name && item.grade == grade;
    return false;
}
string bossRewardsError(const vector<DungeonReward>& rewards, int stage, const string& type) {
    if (rewards.empty() || rewards.size() > 3) return "秘境必须包含固定永久通关奖励。";
    std::set<string> kinds;
    for (const auto& reward : rewards) {
        if (!kinds.insert(reward.kind).second || reward.grade != 1 || !reward.relic.empty()) return "永久通关奖励类型重复或包含临时遗物。";
        if (reward.kind == "learnSpell") {
            const auto* spell = battleSpell(reward.spell);
            if (reward.id != "boss-spell" || !spell || spell->stage > stage || reward.spell == "strike" || reward.spell == "defend" ||
                !reward.manual.empty() || reward.amount != 0 || reward.name != "永久神通·" + spell->name) return "永久神通通关奖励无效。";
        } else if (reward.kind == "manual") {
            ManualInfo info;
            if (reward.id != "boss-manual" || !findManualInfo(reward.manual, info) || info.stage > stage || !reward.spell.empty() ||
                reward.amount != 0 || reward.name != "功法抄本·" + info.name) return "永久抄本通关奖励无效。";
        } else if (reward.kind == "stones") {
            if (reward.id != "boss-stones" || !reward.spell.empty() || !reward.manual.empty() || reward.amount != bossStones(stage, type) ||
                reward.name != "灵石宝藏·" + std::to_string(reward.amount)) return "通关灵石宝藏数量无效。";
        } else return "永久通关奖励不能包含临时卡牌或材料。";
    }
    return kinds.count("stones") ? "" : "通关奖励缺少灵石兜底。";
}

} // namespace

const vector<DungeonRelicDef>& dungeonRelics() {
    static const vector<DungeonRelicDef> definitions = {
        {"spirit-bead", "聚灵珠", "每回合自然回复灵力+1，受灵力容量上限限制。", 1, 0, 0, 0},
        {"jade-slip", "洞观玉简", "每回合额外抽1张神通牌。", 0, 1, 0, 0},
        {"stone-ward", "磐石符印", "每场斗法起手获得气血上限8%的护盾。", 0, 0, 0, 0.08},
        {"spring-vial", "回春露瓶", "每场斗法获胜后恢复6%气血。", 0, 0, 6, 0},
        {"twin-leaf", "两生叶", "每回合额外抽1张牌，获胜后恢复3%气血。", 0, 1, 3, 0},
        {"guardian-bell", "镇魂铃", "每回合自然回复灵力+1（受容量限制），起手获得气血上限5%的护盾。", 1, 0, 0, 0.05},
    };
    return definitions;
}

void ensureDungeons(Game& g) {
    if (g.dungeons.initialized) return;
    g.dungeons.initialized = true;
    spawnWild(g, currentYear(g));
    synchronizeSpecials(g, currentYear(g));
}
void migrateDungeonRewards(Game& g) {
    if (!g.dungeons.initialized) return;
    for (auto& site : g.dungeons.sites) if (site.bossRewards.empty()) {
        if (site.type != "intel") {
            site.targetKind.clear(); site.targetId.clear(); site.targetName.clear(); site.targetGrade = 1;
        }
        freezeBossRewards(g, site);
    }
    if (!g.dungeons.run || !g.dungeons.run->bossRewards.empty()) return;
    auto& run = *g.dungeons.run;
    const auto* site = siteOf(g, run.siteId);
    if (!site) return;
    run.bossRewards = site->bossRewards;
    if (run.type != "intel") {
        run.targetKind.clear(); run.targetId.clear(); run.targetName.clear(); run.targetGrade = 1;
        run.targetGranted = false;
    }
    run.bossRewardClaimed = false;
    run.bossRewardTaken.reset();
    if (run.cleared) { run.phase = "reward"; run.rewards = run.bossRewards; }
}
void dungeonYearTick(Game& g) {
    ensureDungeons(g);
    const int year = currentYear(g);
    const int period = g.player.stage < 9 ? 3 : 50;
    if (year - g.dungeons.lastSpawnYear >= period) spawnWild(g, year);
    synchronizeSpecials(g, year); pruneSites(g);
}
void dungeonAbandon(Game& g) {
    if (!g.dungeons.run) return;
    const DungeonRun run = *g.dungeons.run;
    discardUnclearedSite(g, run);
    g.dungeons.run.reset();
}
string dungeonEnterError(const Game& g, const DungeonSite& site) {
    if (const string error = busyError(g); !error.empty()) return error;
    if (!site.revealed) return "尚未定位此秘境。";
    if (site.cleared) return "此秘境已通关。";
    if (g.flags.count("dungeon-enter:" + site.id)) return "此处秘境已经探索过，不能重复进入刷取奖励。";
    const int year = currentYear(g);
    if (year < site.openedYear || year >= site.expiresYear) return "秘境入口已关闭，需等待下次开启。";
    if (g.player.location != site.location) return "请先赶往" + string(findLoc(site.location) ? findLoc(site.location)->name : site.location) + "。";
    if (g.player.stage < site.stage) return "此秘境至少需要" + realmName(site.stage) + "。";
    return "";
}
bool dungeonNodeAvailable(const DungeonRun& run, const string& nodeId) {
    if (run.phase != "route") return false;
    const auto* node = nodeOf(run, nodeId);
    if (!node || node->visited || node->row != run.row + 1) return false;
    if (run.current.empty()) return node->row == 0;
    const auto* previous = nodeOf(run, run.current);
    return previous && std::find(previous->next.begin(), previous->next.end(), nodeId) != previous->next.end();
}
string dungeonEnter(Game& g, const string& siteId) {
    // 旧档尚未迁移时也保证失败的入场请求不改变任何数据。
    if (!g.dungeons.initialized) {
        Game candidate = g; ensureDungeons(candidate);
        const auto* candidateSite = siteOf(candidate, siteId);
        if (!candidateSite) return "此秘境不存在。";
        if (const string error = dungeonEnterError(candidate, *candidateSite); !error.empty()) return error;
        g.dungeons = std::move(candidate.dungeons);
    }
    auto* site = siteOf(g, siteId);
    if (!site) return "此秘境不存在。";
    if (const string error = dungeonEnterError(g, *site); !error.empty()) return error;
    DungeonRun run;
    run.siteId = site->id; run.siteName = site->name; run.type = site->type; run.location = site->location;
    run.stage = g.player.stage; run.health = g.player.health;
    run.targetKind = site->targetKind; run.targetId = site->targetId; run.targetName = site->targetName; run.targetGrade = site->targetGrade;
    run.bossRewards = site->bossRewards;
    for (const string spell : {"strike", "defend"})
        for (int count = 0; count < 5; ++count) run.deck.push_back({"dungeon-card-" + std::to_string(++run.seq), spell});
    int slots = battleSpellSlots(g.player);
    std::set<string> added;
    for (const auto& id : g.player.arts.spells) {
        const auto* spell = battleSpell(id);
        if (slots <= 0) break;
        if (!spell || spell->stage > run.stage || id == "strike" || id == "defend" || !added.insert(id).second) continue;
        --slots;
        for (int count = 0; count < 2; ++count) run.deck.push_back({"dungeon-card-" + std::to_string(++run.seq), id});
    }
    createRoute(g, run);
    g.flags["dungeon-enter:" + site->id] = 1;
    g.dungeons.run = std::move(run);
    string rewards;
    for (const auto& reward : site->bossRewards) { if (!rewards.empty()) rewards += "、"; rewards += reward.name; }
    logEvent(g, "self", "进入「" + site->name + "」。通关后从「" + rewards + "」中选一项永久奖励。" +
        (site->type == "intel" ? "另保底获得「" + site->targetName + "」×10。" : "") + "沿途神通牌与遗物仅供本次探索。", true);
    return "";
}
string dungeonChoose(Game& g, const string& nodeId) {
    if (const string error = runError(g); !error.empty()) return error;
    if (g.player.arts.battle) return "请先结束当前斗法。";
    auto& run = *g.dungeons.run;
    if (!dungeonNodeAvailable(run, nodeId)) return "只能沿连线前往下一层，且需先完成当前节点。";
    auto* node = nodeOf(run, nodeId);
    run.current = node->id; run.row = node->row; node->visited = true;
    run.rewards.clear();
    if (node->type == "battle" || node->type == "elite" || node->type == "boss") {
        run.phase = "battle";
        run.elapsedDays += node->type == "boss" ? 5 : 3;
        EnemySpec enemy;
        enemy.kind = "dungeon"; enemy.stage = clampInt(run.stage - (node->type == "battle" ? 1 : 0), 0, 12);
        enemy.health = node->type == "boss" ? 115 : node->type == "elite" ? 90 : 65;
        enemy.name = realmName(enemy.stage) + "·" + node->name;
        enemy.beast = run.type == "sea" || run.type == "tianmo" || node->type != "elite";
        enemy.sect = enemy.beast ? "" : vector<string>{"jinhong", "xinghe", "zhushan", "lihuo", "huachen"}[hashText(run.siteId + node->id) % 5];
        enemy.style = run.type == "sea" ? "水" : run.type == "tianmo" ? "神" : "体";
        enemy.dungeonSite = run.siteId; enemy.dungeonNode = node->id;
        // 上下文与 phase 必须先登记，敌方抢先出手可能立即结束战斗。
        g.player.health = run.health; battleStart(g, enemy);
        return "";
    }
    run.phase = "reward"; run.elapsedDays += node->type == "merchant" ? 1 : 2;
    if (node->type == "camp") {
        run.rewards.push_back(makeReward(run, "rest", "调息休整", "恢复25%气血。", 25));
        for (const auto& card : run.deck) if (card.spell != "strike" && card.spell != "defend") {
            const auto* spell = battleSpell(card.spell);
            auto reward = makeReward(run, "remove", "遗忘·" + (spell ? spell->name : card.spell), "移除临时牌组中的这一张牌；普通攻击、防御固定保留。", 1);
            reward.spell = card.uid; run.rewards.push_back(std::move(reward));
        }
    } else if (node->type == "chest") {
        run.stones += 15 + run.stage * 4; bankMaterial(g, run, 1); relicChoices(g, run);
    } else if (node->type == "merchant") {
        relicChoices(g, run, true); cardChoices(g, run, true);
        run.rewards.push_back(makeReward(run, "buyHeal", "灵泉药浴", "消耗本次探索获得的灵石，恢复30%气血；购买一项后离开。", 15 + run.stage * 2));
    } else {
        run.rewards.push_back(makeReward(run, "rest", "汲取灵泉", "恢复12%气血。", 12));
        run.rewards.push_back(makeReward(run, "cache", "搜寻遗藏", "获得灵石与1份一二阶附带材料。", 15 + run.stage * 4));
        run.rewards.push_back(makeReward(run, "risk", "破开禁制", "付出12%气血（不致死），获得3份一二阶附带材料。", 12));
    }
    return "";
}
string dungeonClaim(Game& g, const string& rewardId) {
    if (const string error = runError(g); !error.empty()) return error;
    if (g.player.arts.battle) return "请先结束当前斗法。";
    auto& run = *g.dungeons.run;
    if (run.phase != "reward") return "当前没有可选择的节点奖励。";
    if (run.cleared) {
        if (run.bossRewardClaimed) return "通关奖励已经领取。";
        const auto found = std::find_if(run.bossRewards.begin(), run.bossRewards.end(), [&](const auto& reward) {
            return rewardId == "skip" ? reward.kind == "stones" : reward.id == rewardId;
        });
        if (found == run.bossRewards.end()) return "请选择一项已冻结的永久通关奖励。";
        const DungeonReward reward = *found;
        DungeonReward taken = reward;
        string text;
        if (reward.kind == "learnSpell") {
            if (knowsSpell(g, reward.spell)) {
                run.stones += bossFallback(run); text = "已领悟「" + reward.name + "」，固定折算" + std::to_string(bossFallback(run)) + "灵石。";
                taken = *std::find_if(run.bossRewards.begin(), run.bossRewards.end(), [](const auto& option) { return option.kind == "stones"; });
            } else {
                const auto* spell = battleSpell(reward.spell);
                if (!spell || spell->stage > run.stage) return "永久神通奖励无效。";
                g.player.arts.knownSpells.push_back(reward.spell);
                text = "永久领悟「" + spell->name + "」，离开后仍保留；可在神通页自行装备。";
            }
        } else if (reward.kind == "manual") {
            if (ownsManual(g, reward.manual)) {
                run.stones += bossFallback(run); text = "已拥有「" + reward.name + "」，固定折算" + std::to_string(bossFallback(run)) + "灵石。";
                taken = *std::find_if(run.bossRewards.begin(), run.bossRewards.end(), [](const auto& option) { return option.kind == "stones"; });
            } else {
                ManualInfo info;
                if (!findManualInfo(reward.manual, info) || info.stage > run.stage) return "永久功法抄本奖励无效。";
                g.economy.books.push_back(reward.manual);
                g.player.arts.bookList.push_back(reward.manual);
                text = "获得《" + info.name + "》永久抄本，可在洞府修习。";
            }
        } else if (reward.kind == "stones") {
            run.stones += reward.amount; text = "获得灵石宝藏" + std::to_string(reward.amount) + "，离开秘境时带出。";
        } else return "永久奖励类型无效。";
        run.bossRewardClaimed = true; run.bossRewardTaken = std::move(taken); run.rewards.clear(); run.phase = "complete";
        logEvent(g, "self", "秘境通关奖励：" + text, true);
        return "";
    }
    if (rewardId == "skip") { run.rewards.clear(); run.phase = "route"; return ""; }
    const auto found = std::find_if(run.rewards.begin(), run.rewards.end(), [&](const auto& reward) { return reward.id == rewardId; });
    if (found == run.rewards.end()) return "这项奖励不存在，或已经领过。";
    const DungeonReward reward = *found;
    const bool purchase = reward.kind == "buyRelic" || reward.kind == "buyCard" || reward.kind == "buyHeal";
    if (purchase && run.stones < reward.amount) return "本次探索获得的灵石不足。";
    if ((reward.kind == "card" || reward.kind == "buyCard") && run.deck.size() >= 60) return "临时牌组已达到60张上限。";
    if ((reward.kind == "relic" || reward.kind == "buyRelic") && hasRelic(run, reward.relic)) return "已经持有这件秘境遗物。";
    if (reward.kind == "remove") {
        const auto card = std::find_if(run.deck.begin(), run.deck.end(), [&](const auto& item) { return item.uid == reward.spell; });
        if (card == run.deck.end() || card->spell == "strike" || card->spell == "defend") return "不能移除普通攻击、防御，或这张牌已经不存在。";
        run.deck.erase(card);
    } else if (reward.kind == "card" || reward.kind == "buyCard") {
        run.deck.push_back({"dungeon-card-" + std::to_string(++run.seq), reward.spell});
    } else if (reward.kind == "relic" || reward.kind == "buyRelic") run.relics.push_back(reward.relic);
    else if (reward.kind == "rest") run.health = std::min(100, run.health + reward.amount);
    else if (reward.kind == "buyHeal") run.health = std::min(100, run.health + 30);
    else if (reward.kind == "cache") { run.stones += reward.amount; bankMaterial(g, run, 1); }
    else if (reward.kind == "risk") { run.health = std::max(1, run.health - reward.amount); bankMaterial(g, run, 3); }
    else return "奖励类型无效。";
    if (purchase) run.stones -= reward.amount;
    g.player.health = run.health;
    logEvent(g, "self", "秘境抉择：「" + reward.name + "」。所得先存入探索行囊，离开时结算。", false);
    run.rewards.clear(); run.phase = "route";
    return "";
}
string dungeonLeave(Game& g) {
    if (const string error = runError(g); !error.empty()) return error;
    if (g.player.arts.battle || g.dungeons.run->phase == "battle") return "斗法中须通过撤离行动逃出，不能直接离开秘境。";
    if (g.dungeons.run->cleared && !g.dungeons.run->bossRewardClaimed) return "请先选择一项永久通关奖励，再离开秘境。";
    settleRun(g, false); return "";
}
vector<DungeonIntelOption> dungeonIntelOptions(const Game& g) {
    vector<DungeonIntelOption> result;
    auto add = [&](string kind, string id, string name, int grade) {
        if (grade <= 2) return;
        DungeonIntelOption option;
        option.kind = std::move(kind); option.id = std::move(id); option.name = std::move(name);
        option.grade = grade; option.stage = minimumStage(grade); option.cost = 15 + grade * grade * 6;
        option.desc = "购买一处定向材料秘境的位置，通关终点保底「" + option.name + "」×10，并可选择一项永久神通、抄本或灵石奖励；情报有效5年，每月最多购买1份。";
        option.reason = busyError(g);
        if (option.reason.empty() && g.player.location != "tianji") option.reason = "请先抵达天机阁。";
        if (option.reason.empty() && g.player.stage < option.stage) option.reason = "需达到" + realmName(option.stage) + "才能探索这一品阶的材料秘境。";
        if (option.reason.empty() && g.dungeons.intelMonth == g.clock.days / 30 && g.dungeons.intelPurchases >= 1) option.reason = "本月已经购买过材料秘境情报。";
        if (option.reason.empty() && g.player.stones < option.cost) option.reason = "灵石不足。";
        option.enabled = option.reason.empty(); result.push_back(std::move(option));
    };
    for (const auto& item : data::HerbDef_TABLE()) add("herb", item.id, item.name, item.grade);
    for (const auto& item : data::MaterialDef_TABLE()) add("material", item.id, item.name, item.grade);
    return result;
}
string dungeonIntel(Game& g, const string& kind, const string& id) {
    const auto options = dungeonIntelOptions(g);
    const auto found = std::find_if(options.begin(), options.end(), [&](const auto& option) { return option.kind == kind && option.id == id; });
    if (found == options.end()) return "请先选择要获取的药材或炼器材料。";
    if (!found->enabled) return found->reason;
    ensureDungeons(g);
    DungeonSite site;
    site.id = "intel-" + std::to_string(currentYear(g)) + "-" + std::to_string(++g.dungeons.seq);
    Rng rng = localRng(g, site.id + ":site");
    site.location = randomWildLocation(rng); site.type = "intel";
    site.name = found->name + "·定向秘境";
    site.openedYear = currentYear(g); site.expiresYear = site.openedYear + 5;
    site.stage = g.player.stage; site.revealed = true;
    site.targetKind = found->kind; site.targetId = found->id; site.targetName = found->name; site.targetGrade = found->grade;
    freezeBossRewards(g, site);
    g.player.stones -= found->cost;
    if (g.dungeons.intelMonth != g.clock.days / 30) { g.dungeons.intelMonth = g.clock.days / 30; g.dungeons.intelPurchases = 0; }
    ++g.dungeons.intelPurchases;
    logEvent(g, "self", "天机阁售出「" + site.targetName + "」材料秘境情报：入口位于" + string(findLoc(site.location)->name) +
             "，终点固定掉落10份；情报并非直接购买材料。", true);
    g.dungeons.sites.push_back(std::move(site)); pruneSites(g);
    return "";
}
void dungeonBattleComplete(Game& g, const Battle& battle, bool won) {
    if (!g.dungeons.run || battle.dungeonSite.empty()) return;
    auto& run = *g.dungeons.run;
    if (run.phase != "battle" || run.siteId != battle.dungeonSite || run.current != battle.dungeonNode) return;
    const auto* node = nodeOf(run, run.current);
    if (!node || !node->visited) return;
    run.health = clampInt(jsRound(battle.hp * 100.0 / std::max(1, battle.max)), 1, 100);
    g.player.health = run.health;
    if (!won || battle.retreat) { settleRun(g, true); return; }
    for (const auto& id : run.relics) if (const auto* relic = relicOf(id)) run.health = std::min(100, run.health + relic->heal);
    g.player.health = run.health;
    run.stones += 12 + run.stage * 5 + (node->type == "elite" ? 18 + run.stage * 3 : 0);
    if (node->type == "boss") {
        if (run.type == "intel") grantTarget(run);
        run.stones += 50 + run.stage * 10; run.insight += 3;
        if (run.stage >= 3) run.cores += run.type == "sea" || run.type == "tianmo" ? 3 : 1;
        run.cleared = true; run.phase = "reward"; run.rewards = run.bossRewards;
        if (auto* site = siteOf(g, run.siteId)) site->cleared = true;
        logEvent(g, "self", "通关「" + run.siteName + "」！请选择一项永久神通、功法抄本或灵石宝藏。" +
            (run.type == "intel" ? "「" + run.targetName + "」×10保底已存入探索行囊，离开时带出。" : "沿途材料只是附带收获。"), true);
    } else {
        bankMaterial(g, run, node->type == "elite" ? 2 : 1);
        run.phase = "reward"; run.rewards.clear();
        if (node->type == "elite") { ++run.cores; relicChoices(g, run); }
        else cardChoices(g, run);
    }
}

string dungeonStateError(const Game& g) {
    const auto& world = g.dungeons;
    if (!world.initialized) {
        if (!world.sites.empty() || world.run || world.lastSpawnYear != -1 || world.seq != 0 || world.intelMonth != -1 || world.intelPurchases != 0)
            return "秘境尚未初始化却包含探索状态。";
        return "";
    }
    if (world.lastSpawnYear < 1 || world.lastSpawnYear > currentYear(g) || world.seq < 1 || world.intelMonth < -1 || world.intelMonth > g.clock.days / 30 ||
        world.intelPurchases < 0 || world.intelPurchases > 1 || world.sites.size() > 160)
        return "秘境年表或情报次数无效。";
    std::set<string> siteIds;
    for (const auto& site : world.sites) {
        if (site.id.empty() || !siteIds.insert(site.id).second || !findLoc(site.location) || site.stage < 0 || site.stage > 12 ||
            site.openedYear < 1 || site.expiresYear <= site.openedYear)
            return "秘境入口、境界或目标材料无效。";
        if (site.type == "intel") {
            if (!validTarget(site.targetKind, site.targetId, site.targetName, site.targetGrade) || minimumStage(site.targetGrade) > site.stage) return "定向秘境材料目标无效或超过对应境界。";
        } else if (!site.targetKind.empty() || !site.targetId.empty() || !site.targetName.empty() || site.targetGrade != 1) return "普通秘境不能以材料十份作为主奖励。";
        if (const string error = bossRewardsError(site.bossRewards, site.stage, site.type); !error.empty()) return error;
        if (site.type != "wild" && site.type != "intel" && site.type != "sea" && site.type != "tianmo") return "秘境类型无效。";
    }
    if (!world.run) return "";
    const auto& run = *world.run;
    const auto site = std::find_if(world.sites.begin(), world.sites.end(), [&](const auto& item) { return item.id == run.siteId; });
    if (site == world.sites.end() || run.location != site->location || run.siteName != site->name || run.type != site->type ||
        run.stage < site->stage || run.stage > 12 || run.health < 1 || run.health > 100 || run.row < -1 || run.row > 12 || run.seq < 10 || run.elapsedDays < 0 || run.elapsedDays > 100 ||
        run.stones < 0 || run.stones > 1000000 || run.cores < 0 || run.cores > 1000 || run.insight < 0 || run.insight > 1000 || run.deck.size() < 10 || run.deck.size() > 60 || run.relics.size() > dungeonRelics().size())
        return "在途秘境状态无效。";
    if (g.player.location != run.location || g.player.stage != run.stage || g.job || g.pending || g.ended ||
        !g.flags.count("dungeon-enter:" + run.siteId) || g.flags.at("dungeon-enter:" + run.siteId) != 1) return "秘境探索与角色或日历状态冲突。";
    if (run.targetKind != site->targetKind || run.targetId != site->targetId || run.targetName != site->targetName || run.targetGrade != site->targetGrade) return "秘境终点材料不能在途中改变。";
    if (!sameRewards(run.bossRewards, site->bossRewards)) return "秘境永久通关目标不能在途中改变。";
    if (run.phase != "route" && run.phase != "reward" && run.phase != "battle" && run.phase != "complete") return "秘境阶段无效。";
    if (site->cleared != run.cleared || run.bossRewardClaimed != (run.phase == "complete") ||
        (run.cleared ? run.phase != "reward" && run.phase != "complete" : run.phase == "complete") ||
        run.targetGranted != (run.type == "intel" && run.cleared)) return "秘境通关奖励状态无效。";
    if (run.bossRewardClaimed != run.bossRewardTaken.has_value()) return "永久通关奖励领取记录缺失或提前发放。";
    if (run.bossRewardTaken) {
        const auto& taken = *run.bossRewardTaken;
        if (std::none_of(run.bossRewards.begin(), run.bossRewards.end(), [&](const auto& reward) { return sameReward(taken, reward); }) ||
            (taken.kind == "learnSpell" && !knowsSpell(g, taken.spell)) ||
            (taken.kind == "manual" && !ownsManual(g, taken.manual)) ||
            (taken.kind == "stones" && run.stones < taken.amount)) return "永久通关奖励实取记录与角色收获不一致。";
    }
    const int floors = 9 + run.stage / 3;
    if (run.nodes.size() != static_cast<size_t>((floors - 1) * 3 + 1)) return "秘境路线层数无效。";
    DungeonRun expectedRoute; expectedRoute.siteId = run.siteId; expectedRoute.stage = run.stage;
    createRoute(g, expectedRoute);
    std::set<string> nodeIds, cardIds, relicIds, rewardIds;
    vector<int> visits(floors, 0), widths(floors, 0);
    int strike = 0, defend = 0;
    for (const auto& node : run.nodes) {
        if (node.id.empty() || !nodeIds.insert(node.id).second || node.row < 0 || node.row >= floors || node.lane < 0 || node.lane > 2) return "秘境节点坐标或标识无效。";
        ++widths[node.row]; if (node.visited) ++visits[node.row];
        const auto* expected = nodeOf(expectedRoute, node.id);
        if (!expected || node.row != expected->row || node.lane != expected->lane || node.type != expected->type || node.next != expected->next)
            return "秘境路线不能通过读档重绘或改写。";
        if (node.type != "battle" && node.type != "elite" && node.type != "camp" && node.type != "event" && node.type != "chest" && node.type != "merchant" && node.type != "boss") return "秘境节点类型无效。";
        if ((node.type == "boss") != (node.row == floors - 1) || (node.row == floors - 1 && node.lane != 1)) return "秘境终点无效。";
        if ((node.row == floors - 1) != node.next.empty() || node.next.size() > 2) return "秘境路线缺少出口。";
        std::set<string> nextIds;
        for (const auto& id : node.next) {
            const auto* next = nodeOf(run, id);
            if (!next || next->row != node.row + 1 || std::abs(next->lane - node.lane) > 1 || !nextIds.insert(id).second) return "秘境路线连接无效。";
        }
    }
    for (int row = 0; row < floors; ++row) {
        if (widths[row] != (row == floors - 1 ? 1 : 3) || visits[row] != (row <= run.row ? 1 : 0)) return "秘境探索必须逐层且每层仅选择一个节点。";
        if (row > 0 && row <= run.row) {
            const auto previous = std::find_if(run.nodes.begin(), run.nodes.end(), [&](const auto& node) { return node.row == row - 1 && node.visited; });
            const auto present = std::find_if(run.nodes.begin(), run.nodes.end(), [&](const auto& node) { return node.row == row && node.visited; });
            if (std::find(previous->next.begin(), previous->next.end(), present->id) == previous->next.end()) return "秘境进度不能跳过路径连线。";
        }
    }
    const auto* node = nodeOf(run, run.current);
    if (run.row == -1 ? !run.current.empty() || run.phase != "route" : !node || !node->visited || node->row != run.row) return "秘境当前节点无效。";
    if (run.cleared && (!node || node->type != "boss" || run.row != floors - 1)) return "永久奖励仅在击败最后一层首领后发放。";
    if (run.cleared && !run.bossRewardClaimed && !sameRewards(run.rewards, run.bossRewards)) return "首领奖励必须与入场前冻结的选项一致。";
    const auto& battle = g.player.arts.battle;
    if (run.phase == "battle") {
        if (!battle || battle->dungeonSite != run.siteId || battle->dungeonNode != run.current || (node->type != "battle" && node->type != "elite" && node->type != "boss")) return "秘境战斗上下文无效。";
        if (battle->deck.size() != run.deck.size()) return "秘境斗法必须使用当前临时牌组。";
        for (const auto& card : battle->deck) {
            if (std::none_of(run.deck.begin(), run.deck.end(), [&](const auto& item) { return item.uid == card.uid && item.spell == card.spell; }))
                return "秘境斗法不能携带临时牌组以外的神通。";
        }
    } else if (battle) return "秘境阶段与斗法冲突。";
    for (const auto& card : run.deck) {
        const auto* spell = battleSpell(card.spell);
        if (card.uid.empty() || !cardIds.insert(card.uid).second || !spell || spell->stage > run.stage) return "秘境临时神通牌组无效。";
        strike += card.spell == "strike"; defend += card.spell == "defend";
    }
    if (strike != 5 || defend != 5) return "秘境必须携带五张普通攻击与五张普通防御。";
    for (const auto& id : run.relics) if (!relicOf(id) || !relicIds.insert(id).second) return "秘境遗物无效。";
    if (run.rewards.size() > 61 || (run.phase != "reward" && !run.rewards.empty())) return "秘境奖励与阶段冲突。";
    for (const auto& reward : run.rewards) {
        if (reward.id.empty() || !rewardIds.insert(reward.id).second || reward.amount < 0 || reward.amount > 100000 || reward.grade < 1 || reward.grade > 6) return "秘境奖励标识或数量无效。";
        if (reward.kind == "learnSpell" || reward.kind == "manual" || reward.kind == "stones") {
            if (!run.cleared || run.bossRewardClaimed || std::none_of(run.bossRewards.begin(), run.bossRewards.end(), [&](const auto& frozen) { return sameReward(reward, frozen); })) return "永久奖励不能从沿途普通节点领取。";
        } else if (reward.kind == "card" || reward.kind == "buyCard") {
            const auto* spell = battleSpell(reward.spell);
            if (!spell || spell->stage > run.stage || reward.spell == "strike" || reward.spell == "defend") return "秘境卡牌奖励无效。";
        } else if (reward.kind == "relic" || reward.kind == "buyRelic") {
            if (!relicOf(reward.relic) || hasRelic(run, reward.relic)) return "秘境遗物奖励无效。";
        } else if (reward.kind == "remove") {
            const auto card = std::find_if(run.deck.begin(), run.deck.end(), [&](const auto& item) { return item.uid == reward.spell; });
            if (card == run.deck.end() || card->spell == "strike" || card->spell == "defend") return "秘境删牌选项无效。";
        } else if (reward.kind != "rest" && reward.kind != "cache" && reward.kind != "risk" && reward.kind != "buyHeal") return "秘境奖励类型无效。";
    }
    for (const auto& [id, count] : run.herbs) {
        const auto* item = findHerbByName(id);
        if (!item || count < 0 || count > 100000 || (item->grade > matchedGrade(run.stage, "herb") &&
            !(run.targetGranted && run.targetKind == "herb" && item->id == run.targetId))) return "秘境药材行囊无效。";
    }
    for (const auto& [id, count] : run.materials) {
        const auto item = std::find_if(data::MaterialDef_TABLE().begin(), data::MaterialDef_TABLE().end(), [&](const auto& def) { return def.id == id; });
        if (item == data::MaterialDef_TABLE().end() || count < 0 || count > 100000 || item->grade > matchedGrade(run.stage, "material")) return "秘境矿材行囊无效。";
    }
    for (const auto& [id, count] : run.potions) if (!findRecipe(id) || count < 0 || count > 100000) return "秘境丹药行囊无效。";
    return "";
}

} // namespace nz
