// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"
#include "core/crafting/market_cycle.h"

namespace nz::persistence {

Value gameToJson(const Game& game) {
    Value root(json::Object{});
    root.set("format", str(SAVE_FORMAT));
    root.set("version", num(game.version));
    root.set("combatRulesVersion", num(5));
    root.set("rng", num(static_cast<double>(game.rng)));
    root.set("seed", num(static_cast<double>(game.seed)));
    root.set("year", num(game.year));
    root.set("eventSeq", num(game.eventSeq));
    root.set("npcSeq", num(game.npcSeq));
    root.set("ended", Value(game.ended));
    root.set("lifeId", str(game.lifeId));
    root.set("plan", str(game.plan));
    root.set("flags", mapToJson(game.flags));
    root.set("dungeons", dungeonWorldToJson(game.dungeons));
    root.set("garden", mfGardenToJson(game.Garden));
    {
        Value checkpoint(json::Object{});
        checkpoint.set("enabled", Value(game.checkpoint.enabled)); checkpoint.set("days", num(game.checkpoint.days));
        checkpoint.set("lastAutoYear", num(game.checkpoint.lastAutoYear)); checkpoint.set("restores", num(game.checkpoint.restores)); checkpoint.set("slot", num(game.checkpoint.slot));
        checkpoint.set("source", str(game.checkpoint.source)); checkpoint.set("snapshot", str(game.checkpoint.snapshot)); checkpoint.set("lastReason", str(game.checkpoint.lastReason));
        root.set("checkpoint", std::move(checkpoint));
    }
    if (game.ending.has_value()) {
        const Ending& ending = *game.ending;
        Value recordObject(json::Object{});
        recordObject.set("cause", str(ending.cause));
        recordObject.set("year", num(ending.year));
        recordObject.set("age", num(ending.age));
        recordObject.set("realm", str(ending.realm));
        recordObject.set("friends", num(ending.friends));
        recordObject.set("score", num(ending.score));
        recordObject.set("legacyRewardPoints", num(ending.legacyRewardPoints));
        recordObject.set("legacyEligible", Value(ending.legacyEligible));
        json::Array reasons;
        for (const auto& sect : ending.legacyReasons) reasons.push_back(str(sect));
        recordObject.set("legacyReasons", Value(std::move(reasons)));
        root.set("ending", std::move(recordObject));
    }

    root.set("player", playerToJson(game.player));
    root.set("tide", tideToJson(game.tide));

    // 世界
    {
        Value worldObject(json::Object{});
        worldObject.set("prosperity", num(game.world.prosperity));
        worldObject.set("market", num(game.world.market));
        worldObject.set("mineOwner", str(game.world.mineOwner));
        worldObject.set("herbs", num(game.world.herbs));
        worldObject.set("coreStock", num(game.world.coreStock));
        worldObject.set("pillStock", num(game.world.pillStock));
        worldObject.set("weather", str(game.world.weather));
        Value sects(json::Object{});
        for (const auto& [id, sect] : game.world.sects) {
            Value sectObject(json::Object{});
            sectObject.set("treasury", num(sect.treasury));
            sectObject.set("influence", num(sect.influence));
            sectObject.set("members", num(sect.members));
            sects.set(id, std::move(sectObject));
        }
        worldObject.set("sects", std::move(sects));
        root.set("world", std::move(worldObject));
    }

    // 月历
    {
        Value clockObject(json::Object{});
        clockObject.set("days", num(game.clock.days));
        clockObject.set("ageBase", num(game.clock.ageBase));
        clockObject.set("carry", mapToJson(game.clock.carry));
        root.set("clock", std::move(clockObject));
    }

    // 营生
    {
        Value record(json::Object{});
        json::Array books;
        for (const auto& bondsObject : game.economy.books) books.push_back(str(bondsObject));
        record.set("books", Value(std::move(books)));
        record.set("contribution", num(game.economy.contribution));
        record.set("taskCounts", mapToJson(game.economy.taskCounts));
        record.set("huntCounts", num(game.economy.huntCounts));
        record.set("commissionBrew", num(game.economy.commissionBrew));
        record.set("commissionForge", num(game.economy.commissionForge));
        record.set("cityTasks", num(game.economy.cityTasks));
        record.set("month", num(game.economy.month));
        record.set("orders", mapToJson(game.economy.orders));
        record.set("sold", mapToJson(game.economy.sold));
        record.set("marketCash", num(game.economy.marketCash));
        record.set("marketCycleIndex", num(game.economy.MarketCycleIndex));
        record.set("seq", num(game.economy.seq));
        root.set("economy", std::move(record));
    }

    // 人情
    {
        Value bondsObject(json::Object{});
        Value payrollObject(json::Object{});
        payrollObject.set("sect", str(game.bonds.payroll.sect));
        payrollObject.set("lastMonth", num(game.bonds.payroll.lastMonth));
        payrollObject.set("balance", num(game.bonds.payroll.balance));
        payrollObject.set("credited", num(game.bonds.payroll.credited));
        bondsObject.set("payroll", std::move(payrollObject));
        bondsObject.set("master", str(game.bonds.master));
        {
            json::Array context;
            for (const auto& x : game.bonds.disciples) context.push_back(str(x));
            bondsObject.set("disciples", Value(std::move(context)));
        }
        bondsObject.set("partner", str(game.bonds.partner));
        bondsObject.set("stipend", num(game.bonds.stipend));
        bondsObject.set("lessons", num(game.bonds.lessons));
        bondsObject.set("mentorMonth", num(game.bonds.mentorMonth));
        bondsObject.set("discipleMonth", num(game.bonds.discipleMonth));
        bondsObject.set("partnerMonth", num(game.bonds.partnerMonth));
        bondsObject.set("bondDays", num(game.bonds.bondDays));
        bondsObject.set("notoriety", num(game.bonds.notoriety));
        bondsObject.set("wildWins", num(game.bonds.wildWins));
        bondsObject.set("wildMonth", num(game.bonds.wildMonth));
        bondsObject.set("wildCount", num(game.bonds.wildCount));
        bondsObject.set("aptStory", num(game.bonds.aptStory));
        bondsObject.set("stories", mapToJson(game.bonds.stories));
        { json::Array arr; for (const auto& sect : game.bonds.achievementTexts) arr.push_back(str(sect));
          bondsObject.set("achievementTexts", Value(std::move(arr))); }
        { json::Array arr; for (const auto& [id, text] : game.bonds.origins) {
              Value recordObject(json::Object{}); recordObject.set("id", str(id)); recordObject.set("text", str(text));
              arr.push_back(std::move(recordObject)); }
          bondsObject.set("origins", Value(std::move(arr))); }
        root.set("bonds", std::move(bondsObject));
    }

    // 游历
    {
        Value adventureObject(json::Object{});
        adventureObject.set("since", num(game.adventure.since));
        adventureObject.set("total", num(game.adventure.total));
        adventureObject.set("seq", num(game.adventure.seq));
        adventureObject.set("events", num(game.adventure.events));
        adventureObject.set("wins", num(game.adventure.wins));
        adventureObject.set("lastKind", str(game.adventure.lastKind));
        adventureObject.set("backhillMonth", num(game.adventure.backhillMonth));
        root.set("adventure", std::move(adventureObject));
    }

    // 在途工作
    if (game.job.has_value()) {
        const Job& job = *game.job;
        Value recordObject(json::Object{});
        recordObject.set("actionType", str(job.actionType));
        recordObject.set("name", str(job.name));
        recordObject.set("plan", str(job.plan));
        recordObject.set("total", num(job.total));
        recordObject.set("remaining", num(job.remaining));
        recordObject.set("started", num(job.started));
        recordObject.set("cost", num(job.cost));
        recordObject.set("paid", Value(job.paid));
        recordObject.set("gift", num(job.gift));
        recordObject.set("chance", num(job.chance));
        recordObject.set("fee", num(job.fee));
        recordObject.set("batch", num(job.batch));
        recordObject.set("craftOutput", num(job.craftOutput));
        recordObject.set("travelChecks", num(job.travelChecks));
        recordObject.set("travelAmbushes", num(job.travelAmbushes));
        recordObject.set("destination", str(job.destination));
        recordObject.set("origin", str(job.origin.empty() && job.actionType == "travel" ?
                             game.player.location : job.origin));
        recordObject.set("travelMode", str(job.travelMode.empty() && job.actionType == "travel" ?
                                 "boat" : job.travelMode));
        recordObject.set("sect", str(job.sect));
        recordObject.set("npcId", str(job.npcId));
        recordObject.set("kind", str(job.kind));
        recordObject.set("breakthroughPrepared", Value(job.breakthroughPrepared));
        recordObject.set("breakthroughMajor", Value(job.breakthroughMajor));
        recordObject.set("breakthroughChance", num(job.breakthroughChance));
        recordObject.set("breakthroughFailureLoss", num(job.breakthroughFailureLoss));
        root.set("job", std::move(recordObject));
    }

    // 待处理抉择
    if (game.pending.has_value()) {
        const Pending& pending = *game.pending;
        Value recordObject(json::Object{});
        recordObject.set("type", str(pending.type));
        recordObject.set("title", str(pending.title));
        recordObject.set("subtitle", str(pending.subtitle));
        recordObject.set("text", str(pending.text));
        recordObject.set("npcId", str(pending.npcId));
        json::Array opts;
        for (const auto& clockObject : pending.options) {
            Value choiceObject(json::Object{});
            choiceObject.set("id", str(clockObject.id));
            choiceObject.set("name", str(clockObject.name));
            choiceObject.set("desc", str(clockObject.desc));
            choiceObject.set("disabled", Value(clockObject.disabled));
            opts.push_back(std::move(choiceObject));
        }
        recordObject.set("options", Value(std::move(opts)));
        if (pending.adventure.has_value()) {
            const AdvContext& context = *pending.adventure;
            Value adventureObject(json::Object{});
            adventureObject.set("kind", str(context.kind));
            adventureObject.set("seq", num(context.seq));
            adventureObject.set("stage", num(context.stage));
            adventureObject.set("fee", num(context.fee));
            adventureObject.set("grade", num(context.grade));
            adventureObject.set("book", str(context.book));
            adventureObject.set("npcId", str(context.npcId));
            adventureObject.set("style", str(context.style));
            recordObject.set("adventure", std::move(adventureObject));
        }
        root.set("pending", std::move(recordObject));
    }

    // NPC
    {
        json::Array npcs;
        for (const auto& npc : game.npcs) npcs.push_back(npcToJson(npc));
        root.set("npcs", Value(std::move(npcs)));
    }

    // 纪事
    {
        json::Array arr;
        for (const auto& record : game.logs) {
            Value recordObject(json::Object{});
            recordObject.set("id", num(record.id));
            recordObject.set("year", num(record.year));
            recordObject.set("age", num(record.age));
            recordObject.set("category", str(record.category));
            recordObject.set("text", str(record.text));
            recordObject.set("important", Value(record.important));
            recordObject.set("npcId", str(record.npcId.value_or("")));
            recordObject.set("delta", str(record.delta));
            recordObject.set("days", num(record.days.value_or(0)));
            arr.push_back(std::move(recordObject));
        }
        root.set("logs", Value(std::move(arr)));
    }

    return root;
}


Game gameFromJson(const Value& root) {
    Game game;
    game.version = root["version"].asInt(VERSION);
    game.rng = static_cast<uint32_t>(root["rng"].asNumber(game.rng));
    game.seed = static_cast<uint32_t>(root["seed"].asNumber(game.seed));
    game.year = root["year"].asInt();
    game.eventSeq = root["eventSeq"].asInt();
    game.npcSeq = root["npcSeq"].asInt();
    game.ended = root["ended"].asBool();
    game.lifeId = root["lifeId"].asString();
    game.plan = root["plan"].asString("cultivate");
    game.flags = jsonToMap<int>(root["flags"]);
    dungeonWorldFromJson(root["dungeons"], game.dungeons);
    mfGardenFromJson(root["garden"], game.Garden);
    if (root["checkpoint"].isObject()) {
        const auto& checkpoint = root["checkpoint"];
        game.checkpoint.enabled = checkpoint["enabled"].asBool(); game.checkpoint.days = checkpoint["days"].asInt();
        game.checkpoint.lastAutoYear = checkpoint["lastAutoYear"].asInt(); game.checkpoint.restores = checkpoint["restores"].asInt(); game.checkpoint.slot = checkpoint["slot"].asInt(4);
        game.checkpoint.source = checkpoint["source"].asString("auto"); game.checkpoint.snapshot = checkpoint["snapshot"].asString(); game.checkpoint.lastReason = checkpoint["lastReason"].asString();
    }
    if (root["ending"].isObject()) {
        const Value& recordObject = root["ending"];
        Ending ending;
        ending.cause = recordObject["cause"].asString();
        ending.year = recordObject["year"].asInt(); ending.age = recordObject["age"].asInt();
        ending.realm = recordObject["realm"].asString(); ending.friends = recordObject["friends"].asInt();
        ending.score = recordObject["score"].asInt();
        ending.legacyRewardPoints = recordObject["legacyRewardPoints"].asInt();
        ending.legacyEligible = recordObject["legacyEligible"].asBool();
        ending.legacyReasons = jsonToStringVec(recordObject["legacyReasons"]);
        game.ending = std::move(ending);
    }

    playerFromJson(root["player"], game.player, game.seed);
    // 兽潮（旧档没有这个字段，缺了就是「没有兽潮」）
    tideFromJson(root["tide"], game.tide);

    {
        const Value& worldObject = root["world"];
        game.world.prosperity = worldObject["prosperity"].asInt(72);
        game.world.market = worldObject["market"].asNumber(1.0);
        game.world.mineOwner = worldObject["mineOwner"].asString("jinhong");
        game.world.herbs = worldObject["herbs"].asInt(3200);
        game.world.coreStock = worldObject["coreStock"].asInt(120);
        game.world.pillStock = worldObject["pillStock"].asInt(90);
        game.world.weather = worldObject["weather"].asString("春和景明");
        for (const auto& [id, sectObject] : worldObject["sects"].asObject()) {
            SectState sect;
            sect.treasury = sectObject["treasury"].asInt(3000);
            sect.influence = sectObject["influence"].asInt(50);
            sect.members = sectObject["members"].asInt();
            game.world.sects[id] = sect;
        }
    }

    {
        const Value& clockObject = root["clock"];
        game.clock.days = clockObject["days"].asInt(game.year * 360);
        game.clock.ageBase = clockObject["ageBase"].asInt(game.player.age - game.clock.days / 360);
        game.clock.carry = jsonToMap<double>(clockObject["carry"]);
    }

    {
        const Value& economyObject = root["economy"];
        game.economy.books = jsonToStringVec(economyObject["books"]);
        game.economy.contribution = economyObject["contribution"].asInt();
        game.economy.taskCounts = jsonToMap<int>(economyObject["taskCounts"]);
        game.economy.huntCounts = economyObject["huntCounts"].asInt();
        game.economy.commissionBrew = economyObject["commissionBrew"].asInt();
        game.economy.commissionForge = economyObject["commissionForge"].asInt();
        game.economy.cityTasks = economyObject["cityTasks"].asInt();
        game.economy.month = economyObject["month"].asInt(-1);
        game.economy.orders = jsonToMap<int>(economyObject["orders"]);
        game.economy.sold = jsonToMap<int>(economyObject["sold"]);
        game.economy.marketCash = economyObject["marketCash"].asInt(400);
        game.economy.MarketCycleIndex = economyObject["marketCycleIndex"].asInt(
            std::max(0, game.economy.month) / (crafting::MarketCycleDurationDays / 30));
        game.economy.seq = economyObject["seq"].asInt();
    }

    {
        const Value& bondsObject = root["bonds"];
        const Value& payrollObject = bondsObject["payroll"];
        game.bonds.payroll.sect = payrollObject["sect"].asString();
        game.bonds.payroll.lastMonth = payrollObject["lastMonth"].asInt();
        game.bonds.payroll.balance = payrollObject["balance"].asInt();
        game.bonds.payroll.credited = payrollObject["credited"].asInt();
        game.bonds.master = bondsObject["master"].asString();
        game.bonds.disciples = jsonToStringVec(bondsObject["disciples"]);
        game.bonds.partner = bondsObject["partner"].asString();
        game.bonds.stipend = bondsObject["stipend"].asInt(-1);
        game.bonds.lessons = bondsObject["lessons"].asInt();
        game.bonds.mentorMonth = bondsObject["mentorMonth"].asInt(-1);
        game.bonds.discipleMonth = bondsObject["discipleMonth"].asInt(-1);
        game.bonds.partnerMonth = bondsObject["partnerMonth"].asInt(-1);
        game.bonds.bondDays = bondsObject["bondDays"].asInt();
        game.bonds.notoriety = bondsObject["notoriety"].asInt();
        game.bonds.wildWins = bondsObject["wildWins"].asInt();
        game.bonds.wildMonth = bondsObject["wildMonth"].asInt(-1);
        game.bonds.wildCount = bondsObject["wildCount"].asInt();
        game.bonds.aptStory = bondsObject["aptStory"].asInt();
        game.bonds.stories = jsonToMap<int>(bondsObject["stories"]);
        game.bonds.achievementTexts = jsonToStringVec(bondsObject["achievementTexts"]);
        for (const auto& recordObject : bondsObject["origins"].asArray())
            game.bonds.origins.emplace_back(recordObject["id"].asString(), recordObject["text"].asString());
    }

    {
        const Value& adventureObject = root["adventure"];
        game.adventure.since = adventureObject["since"].asInt();
        game.adventure.total = adventureObject["total"].asInt();
        game.adventure.seq = adventureObject["seq"].asInt();
        game.adventure.events = adventureObject["events"].asInt();
        game.adventure.wins = adventureObject["wins"].asInt();
        game.adventure.lastKind = adventureObject["lastKind"].asString();
        game.adventure.backhillMonth = adventureObject["backhillMonth"].asInt(-1);
    }

    if (root["job"].isObject()) {
        const Value& recordObject = root["job"];
        Job job;
        job.actionType = recordObject["actionType"].asString();
        job.name = recordObject["name"].asString();
        job.plan = recordObject["plan"].asString();
        job.total = recordObject["total"].asInt();
        job.remaining = recordObject["remaining"].asInt();
        job.started = recordObject["started"].asInt();
        job.cost = recordObject["cost"].asInt();
        job.paid = recordObject["paid"].asBool(true);
        job.gift = recordObject["gift"].asInt();
        job.chance = recordObject["chance"].asNumber(1.0);
        job.fee = recordObject["fee"].asInt();
        job.batch = recordObject["batch"].asInt(1);
        job.craftOutput = recordObject["craftOutput"].asInt(1);
        job.travelChecks = recordObject["travelChecks"].asInt();
        job.travelAmbushes = recordObject["travelAmbushes"].asInt();
        job.destination = recordObject["destination"].asString();
        job.origin = recordObject["origin"].asString();
        job.travelMode = recordObject["travelMode"].asString();
        if (job.actionType == "travel") {
            if (recordObject["origin"].isNull()) job.origin = game.player.location;
            if (recordObject["travelMode"].isNull()) job.travelMode = "boat";
        }
        job.sect = recordObject["sect"].asString();
        job.npcId = recordObject["npcId"].asString();
        job.kind = recordObject["kind"].asString();
        job.breakthroughPrepared = recordObject["breakthroughPrepared"].asBool();
        job.breakthroughMajor = recordObject["breakthroughMajor"].asBool();
        job.breakthroughChance = recordObject["breakthroughChance"].asNumber();
        job.breakthroughFailureLoss = recordObject["breakthroughFailureLoss"].asNumber();
        if (job.actionType == "brew" &&
            (!root["player"]["build"].isObject() || recordObject["batch"].isNull())) {
            // 旧版实际炉数借存 gift，batch 始终是默认 1；旧单炉档 gift 可能缺失。
            if (job.gift == 0) job.gift = 1;
            if (job.gift == 1 || job.gift == 3 || job.gift == 5 || job.gift == 10) job.batch = job.gift;
        }
        game.job = job;
    }

    if (root["pending"].isObject()) {
        const Value& recordObject = root["pending"];
        Pending pending;
        pending.type = recordObject["type"].asString();
        pending.title = recordObject["title"].asString();
        pending.subtitle = recordObject["subtitle"].asString();
        pending.text = recordObject["text"].asString();
        pending.npcId = recordObject["npcId"].asString();
        for (const auto& choiceObject : recordObject["options"].asArray()) {
            ChoiceOption clockObject;
            clockObject.id = choiceObject["id"].asString();
            clockObject.name = choiceObject["name"].asString();
            clockObject.desc = choiceObject["desc"].asString();
            clockObject.disabled = choiceObject["disabled"].asBool();
            pending.options.push_back(clockObject);
        }
        if (recordObject["adventure"].isObject()) {
            const Value& adventureObject = recordObject["adventure"];
            AdvContext context;
            context.kind = adventureObject["kind"].asString();
            context.seq = adventureObject["seq"].asInt();
            context.stage = adventureObject["stage"].asInt();
            context.fee = adventureObject["fee"].asInt();
            context.grade = adventureObject["grade"].asInt(1);
            context.book = adventureObject["book"].asString();
            context.npcId = adventureObject["npcId"].asString();
            context.style = adventureObject["style"].asString();
            pending.adventure = context;
        }
        game.pending = pending;
    }

    for (const auto& npc : root["npcs"].asArray()) game.npcs.push_back(npcFromJson(npc));

    for (const auto& recordObject : root["logs"].asArray()) {
        LogEntry record;
        record.id = recordObject["id"].asInt();
        record.year = recordObject["year"].asInt();
        record.age = recordObject["age"].asInt();
        record.category = recordObject["category"].asString();
        record.text = recordObject["text"].asString();
        record.important = recordObject["important"].asBool();
        const std::string npcId = recordObject["npcId"].asString();
        if (!npcId.empty()) record.npcId = npcId;
        record.delta = recordObject["delta"].asString();
        if (recordObject["days"].isNumber()) record.days = recordObject["days"].asInt();
        game.logs.push_back(record);
    }

    return game;
}

} // namespace nz::persistence
