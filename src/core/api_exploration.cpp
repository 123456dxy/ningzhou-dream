#include "core/api_exploration.h"
#include "core/arts.h"
#include "core/battle.h"
#include "core/calendar.h"
#include "core/cultivate.h"
#include "core/dungeon.h"
#include "core/shop.h"

namespace nz::api {
using json::Value;
namespace {
Value num(int n) { return Value(static_cast<double>(n)); }
Value strings(const vector<string>& values) {
    json::Array a; for (const auto& s : values) a.emplace_back(s); return Value(std::move(a));
}
Value counts(const map<string,int>& values) {
    Value o(json::Object{}); for (const auto& [id,n] : values) o.set(id,num(n)); return o;
}
void target(Value& o, const string& kind,const string& id,const string& name,int grade) {
    o.set("targetKind",Value(kind)); o.set("targetId",Value(id));
    o.set("targetName",Value(name)); o.set("targetGrade",num(grade));
}
Value rewardView(const Game& g, const DungeonReward& reward) {
    Value a(json::Object{}); a.set("id",Value(reward.id)); a.set("kind",Value(reward.kind));
    a.set("name",Value(reward.name)); string description=reward.desc;
    if (reward.kind=="card" || reward.kind=="buyCard" || reward.kind=="learnSpell")
        if (const auto* spell=battleSpell(reward.spell))
            description+="\n消耗"+std::to_string(battleCost(g.player,*spell))+"灵气 · "+battleSpellDescription(*spell);
    a.set("desc",Value(description)); a.set("spell",Value(reward.spell)); a.set("manual",Value(reward.manual));
    a.set("relic",Value(reward.relic)); a.set("amount",num(reward.amount)); a.set("grade",num(reward.grade));
    a.set("permanent",Value(reward.kind=="learnSpell" || reward.kind=="manual" || reward.kind=="stones"));
    return a;
}
Value rewardsView(const Game& g, const vector<DungeonReward>& rewards) {
    json::Array a; for (const auto& reward:rewards) a.push_back(rewardView(g,reward)); return Value(std::move(a));
}
}
Value dungeonView(const Game& g) {
    Value o(json::Object{}); const int year = calendarDate(g).year;
    o.set("earlyPeriod",num(3)); o.set("latePeriod",num(50)); o.set("lateStage",num(9));
    o.set("nextSpawnYear",num(g.dungeons.lastSpawnYear + (g.player.stage < 9 ? 3 : 50)));
    json::Array sites;
    for (const auto& site : g.dungeons.sites) {
        Value s(json::Object{});
        s.set("id",Value(site.id)); s.set("name",Value(site.name)); s.set("type",Value(site.type));
        s.set("location",Value(site.location));
        const auto* loc=findLoc(site.location);
        s.set("locationName",Value(loc ? string(loc->name) : site.location));
        s.set("openedYear",num(site.openedYear)); s.set("expiresYear",num(site.expiresYear));
        s.set("stage",num(site.stage)); s.set("realmName",Value(realmName(site.stage)));
        s.set("revealed",Value(site.revealed)); s.set("cleared",Value(site.cleared));
        const auto entered=g.flags.find("dungeon-enter:"+site.id);
        const bool wasEntered=entered!=g.flags.end() && entered->second>0;
        s.set("available",Value(site.revealed && !site.cleared && !wasEntered && year>=site.openedYear && year<site.expiresYear));
        const string reason=dungeonEnterError(g,site);
        s.set("reason",Value(reason)); s.set("entryEnabled",Value(reason.empty()));
        s.set("entered",Value(wasEntered));
        const int period=site.type=="sea"?10:site.type=="tianmo"?50:0;
        s.set("nextOpenYear",num(period ? (year/period+1)*period : 0));
        target(s,site.targetKind,site.targetId,site.targetName,site.targetGrade);
        s.set("bossRewards",rewardsView(g,site.bossRewards));
        sites.push_back(std::move(s));
    }
    o.set("sites",Value(std::move(sites)));
    json::Array specials;
    for (const auto& type : {string("sea"),string("tianmo")}) {
        const int period=type=="sea"?10:50, duration=type=="sea"?2:5;
        Value s(json::Object{}); s.set("type",Value(type)); s.set("location",Value(type));
        s.set("name",Value(type=="sea"?"无尽海·潮生遗境":"天魔眼·封魔古塔"));
        s.set("period",num(period)); s.set("duration",num(duration));
        s.set("stage",num(type=="sea"?3:6));
        s.set("nextOpenYear",num((year/period+1)*period));
        s.set("open",Value(year>=period && year%period<duration)); specials.push_back(std::move(s));
    }
    o.set("specials",Value(std::move(specials)));
    json::Array intel;
    for (const auto& option : dungeonIntelOptions(g)) {
        Value i(json::Object{}); i.set("kind",Value(option.kind)); i.set("id",Value(option.id));
        i.set("name",Value(option.name)); i.set("desc",Value(option.desc)); i.set("grade",num(option.grade));
        i.set("stage",num(option.stage)); i.set("stageName",Value(realmName(option.stage)));
        i.set("cost",num(option.cost)); i.set("enabled",Value(option.enabled)); i.set("reason",Value(option.reason));
        intel.push_back(std::move(i));
    }
    o.set("intel",Value(std::move(intel)));
    if (!g.dungeons.run) { o.set("run",Value()); return o; }
    const auto& run=*g.dungeons.run;
    Value r(json::Object{});
    r.set("siteId",Value(run.siteId)); r.set("siteName",Value(run.siteName)); r.set("type",Value(run.type));
    r.set("location",Value(run.location)); r.set("phase",Value(run.phase)); r.set("current",Value(run.current));
    r.set("stage",num(run.stage)); r.set("realmName",Value(realmName(run.stage))); r.set("health",num(run.health));
    r.set("row",num(run.row)); r.set("elapsedDays",num(run.elapsedDays)); r.set("cleared",Value(run.cleared));
    r.set("stones",num(run.stones)); r.set("targetGranted",Value(run.targetGranted));
    r.set("bossRewards",rewardsView(g,run.bossRewards)); r.set("bossRewardClaimed",Value(run.bossRewardClaimed));
    r.set("bossRewardTaken",run.bossRewardTaken?rewardView(g,*run.bossRewardTaken):Value());
    target(r,run.targetKind,run.targetId,run.targetName,run.targetGrade);
    json::Array nodes;
    for (const auto& node:run.nodes) {
        Value n(json::Object{}); n.set("id",Value(node.id)); n.set("type",Value(node.type));
        n.set("name",Value(node.name)); n.set("text",Value(node.text)); n.set("row",num(node.row));
        n.set("lane",num(node.lane)); n.set("next",strings(node.next)); n.set("visited",Value(node.visited));
        n.set("available",Value(dungeonNodeAvailable(run,node.id))); nodes.push_back(std::move(n));
    }
    r.set("nodes",Value(std::move(nodes)));
    json::Array deck;
    for (const auto& card:run.deck) {
        const auto* spell=battleSpell(card.spell); Value c(json::Object{});
        c.set("uid",Value(card.uid)); c.set("spell",Value(card.spell));
        c.set("name",Value(spell?spell->name:card.spell)); c.set("dao",Value(spell?spell->dao:""));
        c.set("cost",num(spell?battleCost(g.player,*spell):0)); deck.push_back(std::move(c));
    }
    r.set("deck",Value(std::move(deck)));
    json::Array relics;
    for (const auto& id:run.relics) for (const auto& def:dungeonRelics()) if (id==def.id) {
        Value a(json::Object{}); a.set("id",Value(id)); a.set("name",Value(def.name));
        a.set("desc",Value(def.desc)); relics.push_back(std::move(a));
    }
    r.set("relics",Value(std::move(relics)));
    json::Array rewards;
    for (const auto& reward:run.rewards) {
        Value a=rewardView(g,reward);
        const bool purchase=reward.kind=="buyCard"||reward.kind=="buyRelic"||reward.kind=="buyHeal";
        const string reason=purchase&&run.stones<reward.amount?"本次探索获得的灵石不足。":
            (reward.kind=="card"||reward.kind=="buyCard")&&run.deck.size()>=60?"临时牌组已满。":"";
        a.set("cost",num(purchase?reward.amount:0)); a.set("enabled",Value(reason.empty())); a.set("reason",Value(reason));
        rewards.push_back(std::move(a));
    }
    r.set("rewards",Value(std::move(rewards)));
    Value loot(json::Object{}); loot.set("herbs",counts(run.herbs)); loot.set("materials",counts(run.materials));
    loot.set("potions",counts(run.potions)); loot.set("stones",num(run.stones));
    loot.set("cores",num(run.cores)); loot.set("insight",num(run.insight)); r.set("loot",std::move(loot));
    json::Array items;
    auto item=[&](const string& kind,const string& id,const string& name,int count) {
        Value i(json::Object{}); i.set("kind",Value(kind)); i.set("id",Value(id)); i.set("name",Value(name));
        i.set("count",num(count)); items.push_back(std::move(i));
    };
    for (const auto& [id,count]:run.herbs) item("herb",id,id,count);
    for (const auto& [id,count]:run.materials) {
        string name=id; for (const auto& def:data::MaterialDef_TABLE()) if (def.id==id) name=def.name;
        item("material",id,name,count);
    }
    for (const auto& [id,count]:run.potions) {
        string name=id; for (const auto& def:data::RecipeDef_TABLE()) if (def.id==id) name=def.name;
        item("potion",id,name,count);
    }
    r.set("lootItems",Value(std::move(items))); o.set("run",std::move(r)); return o;
}
Value caveView(const Game& g) {
    const auto& tiers=shop::caveTierDefs(); const int tier=clampInt(g.player.caveTier,0,2);
    Value o(json::Object{}); o.set("tier",num(tier)); o.set("tierName",Value(tiers[tier].name));
    o.set("maxSupply",num(shop::caveMaxSupply(g.player)));
    if (tier<2) {
        const auto& next=tiers[tier+1]; Value u(json::Object{}); const string reason=shop::caveUpgradeError(g);
        u.set("enabled",Value(reason.empty())); u.set("reason",Value(reason)); u.set("tier",num(next.tier));
        u.set("name",Value(next.name)); u.set("cost",num(next.cost)); u.set("minRenown",num(next.minRenown));
        u.set("desc",Value(next.desc)); o.set("upgrade",std::move(u));
    } else o.set("upgrade",Value());
    json::Array supplies;
    for (int level=0;level<=3;++level) {
        Value s(json::Object{}); const auto& need=tiers[std::max(0,level-1)];
        const bool locked=level>shop::caveMaxSupply(g.player)|| (level>0&&g.player.cave.empty());
        s.set("level",num(level)); s.set("name",Value(cultivate::supplyName("qi",level)));
        s.set("desc",Value(cultivate::supplyDesc("qi",level))); s.set("tierNeeded",num(need.tier));
        s.set("tierName",Value(need.name)); s.set("minRenown",num(need.minRenown));
        s.set("locked",Value(locked)); s.set("enabled",Value(!locked));
        s.set("reason",Value(locked?(g.player.cave.empty()?"请先安置洞府。":"需先升级至「"+need.name+"」。"):""));
        supplies.push_back(std::move(s));
    }
    o.set("supplies",Value(std::move(supplies))); return o;
}
Value checkpointView(const Game& g) {
    const auto& c=g.checkpoint; Value o(json::Object{});
    o.set("enabled",Value(c.enabled)); o.set("days",num(c.days)); o.set("lastAutoYear",num(c.lastAutoYear));
    o.set("nextAutoDay",num((c.lastAutoYear+10)*360)); o.set("restores",num(c.restores));
    o.set("slot",num(c.slot)); o.set("source",Value(c.source)); o.set("lastReason",Value(c.lastReason)); return o;
}
}
