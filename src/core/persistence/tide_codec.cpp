// 兽潮存档边界：只编码事实，不在读取过程中模拟积分或发放奖励。
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "data/reference_tables.h"
#include "core/simulation/tide_rules.h"
#include <set>

namespace nz::persistence {
// 用途：保存当前赛事账本；参与手动档/自动档；传兽潮状态，返回JSON对象。
Value tideToJson(const BeastTide& tide) {
    Value out(json::Object{});
    out.set("active", Value(tide.active)); out.set("startYear", num(tide.startYear));
    out.set("monthsLeft", num(tide.monthsLeft)); out.set("severity", num(tide.severity));
    out.set("wavesFought", num(tide.wavesFought)); out.set("wavesWon", num(tide.wavesWon));
    out.set("npcDeaths", num(tide.npcDeaths)); out.set("warned", Value(tide.warned));
    out.set("rulesVersion", num(tide.rulesVersion)); out.set("eventId", str(tide.eventId));
    out.set("startDay", num(tide.startDay)); out.set("endDay", num(tide.endDay));
    out.set("durationDays", num(tide.durationDays)); out.set("lastProcessedDay", num(tide.lastProcessedDay));
    out.set("playerScore", num(tide.playerScore)); out.set("playerEntryStage", num(tide.playerEntryStage));
    out.set("playerRank", num(tide.playerRank)); out.set("playerJoined", Value(tide.playerJoined));
    out.set("settled", Value(tide.settled)); out.set("rewardPaid", Value(tide.rewardPaid));
    out.set("rewardStones", num(tide.rewardStones)); out.set("totalScore", num(tide.totalScore));
    out.set("rewardRenown", num(tide.RewardRenown));
    out.set("marketExpiresDay", num(tide.marketExpiresDay));
    json::Array fallen, participants, stock;
    for (const auto& name : tide.fallen) fallen.push_back(str(name));
    for (const auto& actor : tide.participants) {
        Value row(json::Object{});
        row.set("actorId", str(actor.actorId)); row.set("name", str(actor.name));
        row.set("sect", str(actor.sect)); row.set("combatProfileId", str(actor.combatProfileId));
        row.set("entryStage", num(actor.entryStage)); row.set("score", num(actor.score));
        row.set("rank", num(actor.rank)); row.set("elite", Value(actor.elite));
        participants.push_back(std::move(row));
    }
    for (const auto& item : tide.marketStock) {
        Value row(json::Object{});
        row.set("kind", str(item.kind)); row.set("id", str(item.id)); row.set("stock", num(item.stock));
        stock.push_back(std::move(row));
    }
    out.set("fallen", Value(std::move(fallen))); out.set("participants", Value(std::move(participants)));
    out.set("marketStock", Value(std::move(stock)));
    return out;
}

// 用途：还原已校验的兽潮字段；参与载入，缺字段保留旧版默认；无副作用模拟、无返回值。
void tideFromJson(const Value& in, BeastTide& tide) {
    if (!in.isObject()) return;
    tide.active=in["active"].asBool(); tide.startYear=in["startYear"].asInt(-1);
    tide.monthsLeft=in["monthsLeft"].asInt(); tide.severity=in["severity"].asInt(1);
    tide.wavesFought=in["wavesFought"].asInt(); tide.wavesWon=in["wavesWon"].asInt();
    tide.npcDeaths=in["npcDeaths"].asInt(); tide.warned=in["warned"].asBool();
    tide.rulesVersion=in["rulesVersion"].asInt(); tide.eventId=in["eventId"].asString();
    tide.startDay=in["startDay"].asInt(-1); tide.endDay=in["endDay"].asInt(-1);
    tide.durationDays=in["durationDays"].asInt(60); tide.lastProcessedDay=in["lastProcessedDay"].asInt(-1);
    tide.playerScore=in["playerScore"].asInt(); tide.playerEntryStage=in["playerEntryStage"].asInt();
    tide.playerRank=in["playerRank"].asInt(); tide.playerJoined=in["playerJoined"].asBool();
    tide.settled=in["settled"].asBool(); tide.rewardPaid=in["rewardPaid"].asBool();
    tide.rewardStones=in["rewardStones"].asInt(); tide.totalScore=in["totalScore"].asInt();
    tide.RewardRenown=in["rewardRenown"].asInt(0);
    tide.marketExpiresDay=in["marketExpiresDay"].asInt(-1);
    tide.fallen.clear(); tide.participants.clear(); tide.marketStock.clear();
    for (const auto& name : in["fallen"].asArray()) tide.fallen.push_back(name.asString());
    for (const auto& row : in["participants"].asArray()) {
        TideParticipant actor;
        actor.actorId=row["actorId"].asString(); actor.name=row["name"].asString();
        actor.sect=row["sect"].asString(); actor.combatProfileId=row["combatProfileId"].asString();
        actor.entryStage=row["entryStage"].asInt(); actor.score=row["score"].asInt();
        actor.rank=row["rank"].asInt(); actor.elite=row["elite"].asBool();
        tide.participants.push_back(std::move(actor));
    }
    for (const auto& row : in["marketStock"].asArray())
        tide.marketStock.push_back({row["kind"].asString(),row["id"].asString(),row["stock"].asInt()});
}

// 用途：在解码前拒绝错误类型和过大数组；参与外部导入；旧档缺新字段可接受，返回是否合法。
bool tideShapeValid(const Value& in) {
    if (in.isNull()) return true;
    if (!in.isObject()) return false;
    for (const auto& key : {"active","warned","playerJoined","settled","rewardPaid"})
        if (!in[key].isNull() && !in[key].isBool()) return false;
    for (const auto& key : {"eventId"})
        if (!in[key].isNull() && (!in[key].isString() || in[key].asString().size()>128)) return false;
    for (const auto& key : {"rulesVersion","startYear","monthsLeft","severity","wavesFought","wavesWon",
            "npcDeaths","startDay","endDay","durationDays","lastProcessedDay","playerScore",
            "playerEntryStage","playerRank","rewardStones","totalScore","marketExpiresDay"})
        if (!in[key].isNull() && !saveInteger(in[key],-1,1000000000)) return false;
    if (!in["rewardRenown"].isNull() && !saveInteger(in["rewardRenown"], 0, 50)) return false;
    for (const auto& key : {"fallen","participants","marketStock"})
        if (!in[key].isNull() && (!in[key].isArray() || in[key].asArray().size()>10000)) return false;
    for (const auto& name : in["fallen"].asArray()) if (!name.isString()) return false;
    for (const auto& actor : in["participants"].asArray()) {
        if (!actor.isObject() || !actor["elite"].isBool()) return false;
        for (const auto& key : {"actorId","name","sect","combatProfileId"})
            if (!actor[key].isString() || actor[key].asString().size()>256) return false;
        for (const auto& key : {"entryStage","score","rank"}) if (!saveInteger(actor[key],0,1000000000)) return false;
    }
    for (const auto& item : in["marketStock"].asArray())
        if (!item.isObject() || !item["kind"].isString() || !item["id"].isString() ||
            !saveInteger(item["stock"],0,tide::rules::MaximumStock)) return false;
    return true;
}

// 用途：验证已加载账本的期限、分数和库存引用；参与存档状态检查；返回空字符串或错误原因。
std::string tideStateError(const Game& game) {
    const auto& tide=game.tide;
    if (tide.rulesVersion<0 || tide.rulesVersion>1 || tide.severity<1 || tide.severity>3 ||
        tide.monthsLeft<0 || tide.wavesFought<0 || tide.wavesWon<0 || tide.wavesWon>tide.wavesFought ||
        tide.npcDeaths<0 || tide.playerScore<0 || tide.totalScore<0 || tide.rewardStones<0 ||
        tide.RewardRenown<0 || tide.RewardRenown>50 || (tide.RewardRenown>0 && !tide.rewardPaid) ||
        tide.playerEntryStage<0 || tide.playerEntryStage>12) return "兽潮账本数值无效";
    if (tide.rulesVersion==0 && tide.eventId.empty()) return "";
    if (tide.rulesVersion!=1 || tide.eventId.empty()) return "现代兽潮场次标识无效";
    if (tide.startDay<0 || tide.endDay<tide.startDay || tide.endDay-tide.startDay!=tide.durationDays ||
        (tide.durationDays!=30 && tide.durationDays!=60) ||
        tide.lastProcessedDay<tide.startDay || tide.lastProcessedDay>game.clock.days ||
        (tide.active && tide.settled) || (tide.rewardPaid && !tide.settled) ||
        (tide.settled && tide.marketExpiresDay<tide.endDay)) return "兽潮战期或结算状态无效";
    std::set<string> actors, items;
    for (const auto& actor : tide.participants)
        if (actor.actorId.empty() || actor.actorId=="player" || actor.name.empty() ||
            !actors.insert(actor.actorId).second || actor.entryStage<0 || actor.entryStage>12 ||
            actor.score<0 || actor.rank<0 || actor.rank>static_cast<int>(tide.participants.size())+1)
            return "兽潮金榜记录无效";
    for (const auto& item : tide.marketStock) {
        int grade=0;
        if (item.kind=="herb") for (const auto& herb : data::HerbDef_TABLE()) if (herb.id==item.id) grade=herb.grade;
        if (item.kind=="material") for (const auto& material : data::MaterialDef_TABLE()) if (material.id==item.id) grade=material.grade;
        if (grade<3 || grade>5 || item.stock<0 || item.stock>tide::rules::MaximumStock ||
            !items.insert(item.kind+":"+item.id).second) return "兽潮坊市库存无效";
    }
    return "";
}
} // namespace nz::persistence
