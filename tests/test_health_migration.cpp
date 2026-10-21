#include "harness.h"

#include "core/battle.h"
#include "core/json.h"
#include "core/save.h"
#include "core/sim.h"

namespace nztest {
namespace {
nz::Game mfMigrationGame() {
    nz::CreateOptions Options;
    Options.seed = 163052;
    nz::Game State = nz::createGame(Options);
    State.pending.reset();
    State.npcs.clear();
    return State;
}
}

void mfRunHealthMigrationTests() {
    using nz::json::Value;
    std::printf("\n-- 绝对气血与丹药旧档迁移 --\n");
    {
        auto State = mfMigrationGame();
        State.player.health = 50;
        State.player.arts.BaseHealthBonus = 350;
        State.player.arts.FoundationHealthBonus = 20;
        State.player.PillLifeBonus = 37;
        State.player.lifeBonus = State.player.build.lifeBonus + 37;
        State.player.lifespan = nz::realmOf(State.player.stage).life + State.player.lifeBonus;
        const auto Root = Value::parse(nz::saveToJson(State));
        checkEq(Root["healthRulesVersion"].asInt(), 2, "现代存档显式标记绝对气血版本");
        nz::Game Loaded;
        std::string Error;
        check(nz::loadFromJson(Root.dump(), Loaded, &Error), "现代气血和永久增量可载入：" + Error);
        checkEq(Loaded.player.health, 50, "绝对50点不被误判成50%");
        checkEq(Loaded.player.arts.BaseHealthBonus, 350, "体魄丹基础气血往返保真");
        checkEq(Loaded.player.arts.FoundationHealthBonus, 20, "筑基根基与体魄丹分开保存");
        checkEq(Loaded.player.PillLifeBonus, 37, "续命丹寿元独立于天赋保存");
    }
    {
        auto State = mfMigrationGame();
        State.player.health = 50;
        auto Root = Value::parse(nz::saveToJson(State));
        Root.asObject().erase("healthRulesVersion");
        Root.asObject().at("player").asObject().erase("pillLifeBonus");
        Root.asObject().at("player").set("lifeBonus", Value(State.player.build.lifeBonus + 10));
        Root.asObject().at("player").set("lifespan", Value(nz::realmOf(State.player.stage).life + State.player.build.lifeBonus + 10));
        const auto Rng = State.rng;
        nz::Game Loaded;
        std::string Error;
        check(nz::loadFromJson(Root.dump(), Loaded, &Error), "旧百分比存档可迁移：" + Error);
        checkEq(Loaded.player.health, nz::jsRound(nz::battleReferenceState(Loaded.player).max * .5), "旧50%按新上限换算");
        checkEq(Loaded.player.PillLifeBonus, 10, "旧寿元扣除天赋后得到丹药贡献");
        checkEq(Loaded.rng, Rng, "迁移不消耗活动随机数");
        nz::Game Again;
        check(nz::loadFromJson(nz::saveToJson(Loaded), Again, &Error), "迁移后可再次往返");
        checkEq(Again.player.health, Loaded.player.health, "再次载入不重复扩大气血");
    }
    {
        auto State = mfMigrationGame();
        State.player.arts.potions = {{"5519", 2}, {"5420", 1}, {"5318", 3}, {"5319", 4}};
        State.player.arts.used = {{"5519", 4}, {"5318", 1}};
        State.player.arts.herbs["诱妖草"] = 3;
        State.player.arts.herbs["霸王花"] = 4;
        auto Root = Value::parse(nz::saveToJson(State));
        Root.asObject().erase("healthRulesVersion");
        Root.asObject().at("player").set("health", Value(100));
        nz::Game Loaded;
        std::string Error;
        check(nz::loadFromJson(Root.dump(), Loaded, &Error), "退役丹药旧库存不导致拒载：" + Error);
        checkEq(Loaded.player.arts.potions["5420"], 3, "太素祛邪旧库存转换为保留的化毒丹");
        check(!Loaded.player.arts.potions.count("5519") && !Loaded.player.arts.potions.count("5318") && !Loaded.player.arts.potions.count("5319"), "退役丹药库存已清退");
        checkEq(Loaded.player.arts.used["5519"], 4, "保留退役耐药历史且不占新丹耐药");
    }
    {
        const auto State = mfMigrationGame();
        for (const auto Invalid : {Value(0), Value(3), Value("2"), Value(1.5), Value()}) {
            auto Root = Value::parse(nz::saveToJson(State));
            Root.set("healthRulesVersion", Invalid);
            nz::Game Loaded = State;
            std::string Error;
            check(!nz::loadFromJson(Root.dump(), Loaded, &Error), "非法气血版本拒绝");
            checkEq(Loaded.player.health, State.player.health, "拒绝导入保持活动角色");
        }
        auto Root = Value::parse(nz::saveToJson(State));
        Root.asObject().at("player").asObject().at("arts").set("baseHealthBonus", Value(-1));
        nz::Game Loaded;
        std::string Error;
        check(!nz::loadFromJson(Root.dump(), Loaded, &Error), "负基础气血拒绝");
        Root = Value::parse(nz::saveToJson(State));
        Root.asObject().erase("healthRulesVersion");
        Root.asObject().at("player").set("health", Value(101));
        check(!nz::loadFromJson(Root.dump(), Loaded, &Error), "旧百分比超过100拒绝而非夹紧掩盖");
    }
}
}
