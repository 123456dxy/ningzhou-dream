// Internal save implementation; public API remains in core/save.h.
#include "core/persistence/internal.h"
#include "core/persistence/json_helpers.h"

namespace nz::persistence {

Value personToJson(const Person& person) {
    Value personObject(json::Object{});
    personObject.set("name", str(person.name));
    personObject.set("gender", str(person.gender));
    personObject.set("age", num(person.age));
    personObject.set("lifespan", num(person.lifespan));
    personObject.set("stage", num(person.stage));
    personObject.set("xp", num(person.xp));
    personObject.set("apt", num(person.apt));
    personObject.set("luck", num(person.luck));
    personObject.set("insight", num(person.insight));
    personObject.set("sword", num(person.sword));
    personObject.set("alchemy", num(person.alchemy));
    personObject.set("health", num(person.health));
    personObject.set("stones", num(person.stones));
    personObject.set("herbs", num(person.herbs));
    personObject.set("pills", num(person.pills));
    personObject.set("cores", num(person.cores));
    personObject.set("body", num(person.body));
    personObject.set("lifeBonus", num(person.lifeBonus));
    personObject.set("pillLifeBonus", num(person.PillLifeBonus));
    personObject.set("location", str(person.location));
    personObject.set("relations", mapToJson(person.relations));
    return personObject;
}

void personFromJson(const Value& personObject, Person& person) {
    person.name = personObject["name"].asString();
    person.gender = personObject["gender"].asString("女");
    person.age = personObject["age"].asInt();
    person.lifespan = personObject["lifespan"].asInt(100);
    person.stage = personObject["stage"].asInt();
    person.xp = personObject["xp"].asInt();
    person.apt = personObject["apt"].asInt(55);
    person.luck = personObject["luck"].asInt(45);
    person.insight = personObject["insight"].asInt(10);
    person.sword = personObject["sword"].asInt(2);
    person.alchemy = personObject["alchemy"].asInt(5);
    person.health = personObject["health"].asInt(100);
    person.stones = personObject["stones"].asInt(100);
    person.herbs = personObject["herbs"].asInt(3);
    person.pills = personObject["pills"].asInt();
    person.cores = personObject["cores"].asInt();
    person.body = personObject["body"].asInt();
    person.lifeBonus = personObject["lifeBonus"].asInt();
    person.PillLifeBonus = personObject["pillLifeBonus"].asInt();
    person.location = personObject["location"].asString("wuling");
    person.relations = jsonToMap<int>(personObject["relations"]);
}

} // namespace nz::persistence
