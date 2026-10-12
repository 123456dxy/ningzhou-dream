#include "harness.h"

#include <stdexcept>

#include "core/content/game_ids.h"
#include "core/content/text_table.h"
#ifndef NZ_CONTENT_REGISTRY_ONLY
#include "core/arts.h"
#include "core/artifact.h"
#include "core/battle.h"
#endif

namespace nztest {
using namespace nz::content;

namespace {
template<class Operation> bool invalidText(Operation operation) {
    try { operation(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
}

void runContentRegistryTests() {
    std::printf("\n-- 稳定业务 ID 与展示词表 --\n");
    checkEq(allDaoIds().size(), 12, "十二大道均有稳定 ID");
    checkEq(allSectIds().size(), 5, "五宗门均有稳定 ID");
    checkEq(allSpellKinds().size(), 30, "全部三十类神通均有稳定 ID");
    checkEq(allBattleKinds().size(), 10, "全部十类斗法均有稳定 ID");
    const DaoId legacyOrder[] = {DaoId::Metal, DaoId::Wood, DaoId::Water, DaoId::Fire,
        DaoId::Earth, DaoId::Mind, DaoId::Body, DaoId::Sword, DaoId::Qi, DaoId::Array,
        DaoId::Alchemy, DaoId::Artifact};
    const char* legacyKeys[] = {"金", "木", "水", "火", "土", "神", "体", "剑", "气", "阵", "丹", "器"};
    for (int index = 0; index < 12; ++index) {
        check(parseDaoId(legacyKeys[index]) == legacyOrder[index] &&
              legacyDaoKey(legacyOrder[index]) == legacyKeys[index] &&
              daoOrdinal(legacyOrder[index]) == index,
              "旧存档大道键和节点/元素序号固定兼容");
    }
    for (const DaoId id : allDaoIds()) {
        check(parseDaoId(stableId(id)) == id, "大道 ASCII ID 往返");
        check(parseDaoId(legacyDaoKey(id)) == id, "旧中文大道键可解析");
    }
    for (const SectId id : allSectIds()) check(parseSectId(stableId(id)) == id, "宗门 ID 往返");
    for (const SpellKind id : allSpellKinds()) {
        check(parseSpellKind(stableId(id)) == id, "神通类别 ASCII ID 往返");
        check(parseSpellKind(legacyValue(id)) == id, "旧神通 wire 值兼容");
    }
    for (const BattleKind id : allBattleKinds()) {
        check(parseBattleKind(stableId(id)) == id, "战斗类别 ASCII ID 往返");
        check(parseBattleKind(legacyValue(id)) == id, "旧战斗 wire 值兼容");
    }
    check(parseDaoId("metal") == DaoId::Metal && parseDaoId("金") == DaoId::Metal,
          "稳定 ID 与旧金道键共享语义");
    check(parseSpellKind("fire_burst") == SpellKind::FireBurst &&
          parseSpellKind("fireBurst") == SpellKind::FireBurst, "驼峰神通值迁移不丢类别");
    check(parseBattleKind("sect_trial") == BattleKind::SectTrial &&
          parseBattleKind("sectTrial") == BattleKind::SectTrial, "旧宗门试炼类别兼容");
    check(!parseDaoId("未知") && !parseDaoId("") && !parseSectId("unknown") &&
          !parseSpellKind("unknown") && !parseBattleKind("unknown"), "未知标识不会落入有效类别");
    check(!parseDaoId("金道新版名称") && stableId(DaoId::Metal) == "metal" &&
          legacyDaoKey(DaoId::Metal) == "金", "展示改名不参与标识解析或旧键生成");
    check(parseArtifactElement("神念") == DaoId::Mind && parseArtifactElement("混元") == DaoId::Qi &&
          !parseArtifactElement("未开放属性"), "法器元素旧别名明确兼容而未知不伪装");
    check(generates(DaoId::Wood, DaoId::Fire) && !generates(DaoId::Fire, DaoId::Wood), "五行相生方向保留");
    check(sectDao(SectId::Lihuo) == DaoId::Fire, "宗门传承按 ID 解析");
    checkStr(formatText(TextId::DrawDescription, {{"count", "3"}}),
             "立即抽3张牌，手牌最多8张。", "词表参数产生兼容展示文本");
    checkStr(formatTextTemplate("{who}/{who}: {value}", {{"who", "甲"}, {"value", "{count}"}}),
             "甲/甲: {count}", "重复占位符可复用参数且参数内容不二次执行");
    check(invalidText([] { formatText(TextId::DrawDescription); }), "缺少参数明确失败");
    check(invalidText([] { formatText(TextId::DrawDescription, {{"count", "3"}, {"count", "4"}}); }), "重复参数明确失败");
    check(invalidText([] { formatText(TextId::DrawDescription, {{"count", "3"}, {"unused", "4"}}); }), "多余参数明确失败");
    check(invalidText([] { formatTextTemplate("{count", {{"count", "3"}}); }) &&
          invalidText([] { formatTextTemplate("count}"); }), "词表损坏占位符明确失败");

#ifndef NZ_CONTENT_REGISTRY_ONLY
    using namespace nz;
    checkEq(daoIndex("metal"), 0, "业务边界接受稳定大道 ID");
    checkEq(daoIndex("金"), 0, "旧大道节点编号兼容");
    Player player;
    player.stage = 0;
    player.arts.nodes = {"0-1", "0-2"};
    check(artHas(player.arts, "metal", 2), "稳定 ID 可查询旧节点");
    artGain(player, "metal", 30);
    checkEq(player.arts.exp["金"], 30, "稳定 ID 获得经验仍写旧存档键");
    check(!player.arts.exp.count("metal"), "存档不会产生第二套大道经验键");
    const auto before = player.arts.exp;
    artGain(player, "unknown", 500);
    check(player.arts.exp == before, "未知大道不能写入经验");
    const SpellDef* original = battleSpell("gold");
    check(original != nullptr, "金系神通基线存在");
    if (original) {
        SpellDef renamed = *original;
        renamed.name = "新的展示名";
        renamed.dao = "metal";
        checkEq(battleCost(player, renamed), 1, "显示名与 ASCII 道 ID 不影响费用");
        const Preview previous = battleReferencePreview(player, *original);
        const Preview current = battleReferencePreview(player, renamed);
        check(previous.valid && current.valid && previous.damage == current.damage &&
              previous.shield == current.shield && previous.energy == current.energy,
              "展示名称与兼容 ID 不影响试算效果");
        SpellDef unknown = *original; unknown.kind = "unknown";
        check(!battleReferencePreview(player, unknown).valid, "未知神通类别明确拒绝试算");
        check(spellToIntent(unknown).kind.empty(), "未知神通不能伪装成普通攻击意图");
    }
    player.stage = 12;
    for (const auto& spell : allSpells()) {
        const auto daoId = parseDaoId(spell.dao);
        const auto kind = parseSpellKind(spell.kind);
        check(daoId && kind, spell.id + "实际目录类别均可解析");
        if (!daoId || !kind) continue;
        SpellDef renamed = spell;
        renamed.name = "可自由修改的展示名称";
        renamed.dao = stableId(*daoId);
        renamed.kind = stableId(*kind);
        const Preview originalPreview = battleReferencePreview(player, spell);
        const Preview renamedPreview = battleReferencePreview(player, renamed);
        check(originalPreview.valid && renamedPreview.valid &&
              originalPreview.damage == renamedPreview.damage &&
              originalPreview.shield == renamedPreview.shield &&
              originalPreview.energy == renamedPreview.energy &&
              originalPreview.healPotential == renamedPreview.healPotential &&
              originalPreview.qi == renamedPreview.qi,
              spell.id + "改展示名与稳定ID后仍保持实际效果");
    }
    Battle battle;
    battle.kind = "spar";
    check(!battleDefeatEndsLife(battle), "切磋败北仍不结束人生");
    battle.kind = "hunt";
    check(battleDefeatEndsLife(battle), "狩猎败北仍结束人生");
    battle.shadowPlayer = std::make_shared<Player>();
    check(!battleDefeatEndsLife(battle), "影子斗法仍不结束人生");
    Game unknownBattleGame;
    const auto previousRng = unknownBattleGame.rng;
    EnemySpec unknownEnemy; unknownEnemy.kind = "unknown";
    battleStart(unknownBattleGame, unknownEnemy);
    check(!unknownBattleGame.player.arts.battle && unknownBattleGame.rng == previousRng,
          "未知战斗类别拒绝且不消费随机数");
    Game meditationGame; meditationGame.player.insight = 0;
    artAnnual(meditationGame, "meditate", "metal");
    check(meditationGame.player.arts.focus == "金" &&
          meditationGame.player.arts.exp.count("金") && !meditationGame.player.arts.exp.count("metal"),
          "参悟稳定ID归一旧经验键和专注协议值");
    check(artInstant(meditationGame, "focus", "fire").ok && meditationGame.player.arts.focus == "火",
          "即时专注稳定ID归一旧协议值");
    Npc npc; npc.stage = 6; npc.sect = "lihuo"; npc.dao = "水";
    const Arts loadout = artNpcLoadout(npc);
    check(loadout.focus == "火" && loadout.main == "fire", "NPC宗门传承保持火系功法行为");
    Gear gear; gear.id = "content-artifact"; gear.name = "旧混元剑";
    gear.crafted = true; gear.shape = "sword"; gear.slot = "weapon";
    gear.grade = 2; gear.polarity = "地阴"; gear.element = "混元";
    const ArtifactAbility ability = artifactAbility(gear);
    check(ability.valid && ability.spell.dao == "气" && ability.spell.kind == "steal", "旧混元地阴剑仍夺灵");
#endif
}
} // namespace nztest
