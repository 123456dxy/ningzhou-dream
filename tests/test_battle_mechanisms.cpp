#include "harness.h"

#include <algorithm>

#include "core/arts.h"
#include "core/battle.h"

using namespace nz;

namespace nztest {
namespace {

Game fight(const vector<string>& spells, const vector<string>& nodes = {}, int stage = 12) {
    Game g;
    g.seed = g.rng = 20261011;
    g.player.stage = stage;
    g.player.health = 100;
    g.player.sword = 0;
    g.player.body = 0;
    ensureArts(g);
    g.player.arts.main.clear();
    g.player.arts.manuals.clear();
    g.player.arts.aux.clear();
    g.player.arts.equipped.clear();
    g.player.arts.spells = spells;
    g.player.arts.nodes = nodes;
    EnemySpec enemy;
    enemy.name = "机制试炼";
    enemy.style = "体";
    enemy.stage = stage;
    enemy.beast = true;
    battleStart(g, enemy);
    g.player.arts.battle->enemyHp = g.player.arts.battle->enemyMax = 1000000;
    return g;
}

vector<string> copies(const Battle& b, const string& spell) {
    vector<string> out;
    for (const auto& c : b.deck) if (c.spell == spell) out.push_back(c.uid);
    return out;
}

void hand(Battle& b, const vector<string>& spells) {
    b.hand.clear(); b.draw.clear(); b.discard.clear();
    for (const auto& id : spells) {
        for (const auto& c : b.deck) {
            if (c.spell == id && std::find(b.hand.begin(), b.hand.end(), c.uid) == b.hand.end() &&
                std::find(b.exhausted.begin(), b.exhausted.end(), c.uid) == b.exhausted.end()) {
                b.hand.push_back(c.uid); break;
            }
        }
    }
    for (const auto& c : b.deck)
        if (std::find(b.hand.begin(), b.hand.end(), c.uid) == b.hand.end() &&
            std::find(b.exhausted.begin(), b.exhausted.end(), c.uid) == b.exhausted.end()) b.draw.push_back(c.uid);
    b.mana = 12;
}

bool partitioned(const Battle& b) {
    map<string, int> occurrences;
    for (const auto* pile : {&b.hand, &b.draw, &b.discard, &b.exhausted})
        for (const auto& uid : *pile) occurrences[uid]++;
    if (occurrences.size() != b.deck.size()) return false;
    for (const auto& c : b.deck) if (occurrences[c.uid] != 1) return false;
    return b.hand.size() <= 8;
}

} // namespace

void runBattleMechanismTests() {
    std::printf("\n-- 神通槽位、境界门槛与灵力成长 --\n");
    {
        Player p;
        for (int major = 0; major <= 4; ++major) {
            p.stage = major * 3;
            checkEq(battleSpellSlots(p), 4 + major * 2, "大境界增加两个额外神通槽");
            checkEq(battleEnergy(p), 3 + major, "每大境界自然回复灵力增加1");
            check(battleManaCap(p) >= 12, "所有境界灵力容量至少12");
        }
        p.arts.main = "mind"; p.arts.manuals["mind"] = 5; p.arts.nodes = {"8-1"};
        check(battleEnergy(p) > 8, "高境界保留功法与悟道回灵，不受旧8点上限截断");
        Game g = fight({"gather"}, {"8-1"});
        g.player.arts.main = "mind"; g.player.arts.manuals["mind"] = 5;
        Battle& b = *g.player.arts.battle; b.maxMana = battleEnergy(g.player); b.carryMana = 2;
        const int predicted = battleNextMana(g.player, b);
        check(battleTurn(g, "end").ok, "高境界可以结束回合");
        checkEq(b.mana, predicted, "下回合灵力预测包含封存且与真实一致");
        check(b.mana > 8, "真实回灵也突破旧8点限制");
    }
    {
        Game g = fight({"strike", "defend", "gold", "gold", "rain", "missing", "water", "wood", "earth", "body"}, {}, 0);
        const Battle& b = *g.player.arts.battle;
        checkEq(copies(b, "strike").size(), 5, "旧档携带普通攻击不重复进牌组");
        checkEq(copies(b, "defend").size(), 5, "旧档携带普通防御不重复进牌组");
        checkEq(copies(b, "gold").size(), 2, "重复额外神通只携带两张副本");
        checkEq(copies(b, "rain").size(), 0, "境界不足的旧档神通不进战斗牌组");
        checkEq(copies(b, "missing").size(), 0, "未知神通不进战斗牌组");
        checkEq(b.deck.size(), 18, "炼气四个额外神通加固定十张普通招式");
        checkEq(copies(b, "body").size(), 0, "超出额外槽位的旧档神通被过滤");
    }
    {
        Game g = fight({"gold"}, {}, 0);
        Battle& b = *g.player.arts.battle;
        b.deck.push_back({"illegal-rain", "rain"}); b.hand.push_back("illegal-rain"); b.mana = 12;
        const int mana = b.mana, hp = b.enemyHp;
        check(!battleTurn(g, "illegal-rain").ok, "强行注入高境界手牌仍不能施放");
        checkEq(b.mana, mana, "境界拒绝不扣灵力");
        checkEq(b.enemyHp, hp, "境界拒绝不造成伤害");
    }
    {
        map<string, int> ids;
        for (const auto& sp : allSpells()) {
            ids[sp.id]++;
            check(!battleSpellDescription(sp).empty(), sp.name + "有权威效果说明");
        }
        bool unique = true; for (const auto& [id, count] : ids) if (count != 1) unique = false;
        check(unique, "神通定义ID唯一，聚气与持续聚气不再冲突");
        checkStr(battleSpell("gather")->kind, "mana", "基础聚气是炼气回灵牌");
        checkStr(battleSpell("qiFlow")->kind, "flow", "持续聚气使用独立ID");
        checkEq(battleSpell("gather")->stage, 0, "基础聚气境界门槛炼气");
        checkEq(battleSpell("qiFlow")->stage, 6, "持续聚气境界门槛金丹");
    }

    std::printf("\n-- 抽牌、换牌与本场移除 --\n");
    {
        Game g = fight({"skim", "gold", "wood", "water"});
        Battle& b = *g.player.arts.battle;
        hand(b, {"skim", "gold", "gold", "wood", "wood", "water", "water", "defend"});
        const string played = b.hand.front();
        check(battleTurn(g, played).ok, "满8手牌也可打出抽牌卡");
        checkEq(b.hand.size(), 8, "先移出施放卡，释放的手牌位置正确抽满");
        check(std::find(b.hand.begin(), b.hand.end(), played) == b.hand.end(), "施放中的抽牌卡不会被自己立即重抽");
        check(partitioned(b), "抽牌后四牌堆仍互斥且守恒");
    }
    {
        Game g = fight({"refresh", "gold", "wood", "water"}, {"0-4", "0-5"});
        Battle& b = *g.player.arts.battle; hand(b, {"refresh", "gold", "wood", "water"});
        const string played = b.hand.front();
        check(battleTurn(g, played).ok, "换牌卡可施放");
        checkEq(b.hand.size(), 4, "弃掉其余3张并重抽4张");
        checkEq(b.shield, jsRound(b.max * 0.02 * 3), "换牌实际弃置3张触发金系护盾");
        checkNear(b.discardPower, 0.12, "弃牌积攒下一击增伤");
        check(partitioned(b), "换牌后四牌堆互斥且守恒");
    }
    {
        Game g = fight({"purge", "gold", "water"}, {}, 3);
        Battle& b = *g.player.arts.battle; hand(b, {"purge", "gold", "strike"});
        const auto gold = copies(b, "gold");
        b.draw.erase(std::find(b.draw.begin(), b.draw.end(), gold.back()));
        b.discard.push_back(gold.back());
        const int deckSize = static_cast<int>(b.deck.size());
        const int mana = b.mana;
        const auto originalHand = b.hand;
        check(!battleTurn(g, "purge").ok, "移除牌缺少目标被拒绝");
        check(!battleTurn(g, "purge", "purge").ok, "不能移除当前神通自身");
        check(!battleTurn(g, "purge", "missing").ok, "未知移除目标被拒绝");
        checkEq(b.mana, mana, "所有目标拒绝均不扣灵力");
        check(b.hand == originalHand, "所有目标拒绝均不移出手牌");
        check(battleTurn(g, "purge", "gold").ok, "按神通ID移除所有副本");
        checkEq(b.exhausted.size(), 2, "手牌和弃牌中的目标副本全部移除");
        checkEq(b.deck.size(), deckSize, "保留deck供存档检索移除牌");
        check(partitioned(b), "移除后四牌堆互斥且守恒");
        b.cooldowns.clear(); hand(b, {"purge", "strike"});
        check(battleTurn(g, "purge", copies(b, "strike").front()).ok, "可用卡UID选择固定普通攻击");
        checkEq(b.exhausted.size(), 7, "普通攻击全部五张副本仅本场移除");
        for (int i = 0; i < 3; ++i) { b.intentKind = "guard"; battleTurn(g, "end"); check(partitioned(b), "移除牌经过弃牌洗牌不复活"); }
        checkEq(copies(b, "strike").size(), 5, "原牌组仍保留五张普通攻击定义");
        battleTurn(g, "escape");
        EnemySpec e; e.name = "下场"; battleStart(g, e);
        checkEq(g.player.arts.battle->exhausted.size(), 0, "新战斗移除清单重置");
        checkEq(copies(*g.player.arts.battle, "strike").size(), 5, "新战斗普通攻击重新固定携带");
    }

    std::printf("\n-- 可见增益与十二道组合触发 --\n");
    {
        Game g = fight({"swordFocus", "aegis", "gold"}); Battle& b = *g.player.arts.battle;
        const int baseline = battlePreview(g.player, b, "gold").damage;
        for (int i = 0; i < 4; ++i) { b.cooldowns.clear(); hand(b, {"swordFocus"}); check(battleTurn(g, "swordFocus").ok, "增伤神通可以施放"); }
        checkNear(b.buff, 0.30, "本场增伤叠加最高30%");
        check(battlePreview(g.player, b, "gold").damage > baseline, "增伤状态改变真实伤害预览");
        for (int i = 0; i < 5; ++i) { b.cooldowns.clear(); hand(b, {"aegis"}); battleTurn(g, "aegis"); }
        checkNear(b.reduction, 0.20, "本场额外减伤最高20%");
        checkNear(battleReduction(g.player, b), 0.20, "减伤权威helper包含新增buff");
        b.intentKind = "guard"; battleTurn(g, "end");
        checkNear(b.buff, 0.30, "本场增伤在回合结束后保留");
        checkNear(b.reduction, 0.20, "本场减伤在回合结束后保留");
    }
    {
        Game g = fight({"sword", "rain"}, {"7-4", "7-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"sword", "sword", "rain"});
        battleTurn(g, "sword");
        const int combo = battlePreview(g.player, b, "sword").damage;
        Battle plain = b; plain.swordSequence = 0;
        check(combo > battlePreview(g.player, plain, "sword").damage, "连续剑第二段获得独立连段加成");
        battleTurn(g, "sword"); const int mana = b.mana;
        battleTurn(g, "rain");
        checkEq(b.mana, mana - battleCost(g.player, *battleSpell("rain")) + 1, "第三张连续剑返还1灵力");
    }
    {
        Game g = fight({"waterSeal", "wavehit"}, {"2-4", "2-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"waterSeal", "waterSeal", "wavehit"});
        battleTurn(g, "waterSeal"); battleTurn(g, "waterSeal");
        checkEq(b.water, 4, "水系两张牌积攒4水势");
        const int mana = b.mana; battleTurn(g, "wavehit");
        checkEq(b.water, 2, "水道大成保留一半水势");
        checkEq(b.mana, mana - battleCost(g.player, *battleSpell("wavehit")) + 1, "水势爆发返还floor(4*0.35)灵力");
    }
    {
        Game g = fight({"fire", "fireburst"}, {"3-4", "3-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"fire", "fire", "fireburst"});
        battleTurn(g, "fire"); const int firstBurn = b.burn;
        battleTurn(g, "fire"); checkEq(b.burn, firstBurn * 2, "火道增强每次施加量且完整保留旧灼烧");
        const int burst = battlePreview(g.player, b, "fireburst").damage;
        Battle plain = b; plain.burn = plain.burnTurns = 0;
        check(burst > battlePreview(g.player, plain, "fireburst").damage, "引焰爆追加燃烧引爆伤害");
        battleTurn(g, "fireburst"); checkEq(b.burn, 0, "引爆消耗燃烧伤害"); checkEq(b.burnTurns, 0, "引爆清除燃烧持续回合");
    }
    {
        Game g = fight({"fire", "fireburst"}); Battle& b = *g.player.arts.battle;
        hand(b, {"fire", "fire"});
        battleTurn(g, "fire"); const int first = b.burn;
        checkEq(first, jsRound((BATTLE_POWER[g.player.stage] + 12) * 0.1), "无功法火诀施加基础灼烧");
        battleTurn(g, "fire"); checkEq(b.burn, first * 2, "无功法重复施火天然累积灼烧");
        for (int i = 0; i < 6; ++i) { hand(b, {"fire"}); battleTurn(g, "fire"); }
        checkEq(b.burn, first * 8, "多次施火不被旧战力60%上限截断");
        checkEq(b.burnTurns, 3, "每次施火刷新完整三回合");
        const int burn = b.burn;
        b.enemyShield = 0; hand(b, {"fireburst"});
        const Preview preview = battlePreview(g.player, b, "fireburst");
        Battle empty = b; empty.burn = empty.burnTurns = 0;
        checkEq(preview.damage - battlePreview(g.player, empty, "fireburst").damage, burn * 3,
                "无功法引爆完整消费剩余三回合灼烧预算");
        const int enemyHp = b.enemyHp;
        battleTurn(g, "fireburst"); checkEq(enemyHp - b.enemyHp, preview.damage, "天然叠燃引爆实际伤害等于预览");
        checkEq(b.burn, 0, "天然叠燃引爆后清空灼烧");
        checkEq(b.burnTurns, 0, "天然叠燃引爆后清空持续回合");
    }
    {
        Game plain = fight({"fire"}); hand(*plain.player.arts.battle, {"fire"}); battleTurn(plain, "fire");
        Game novice = fight({"fire"}); novice.player.arts.main = "fire"; novice.player.arts.manuals["fire"] = 1;
        hand(*novice.player.arts.battle, {"fire"}); battleTurn(novice, "fire");
        Game master = fight({"fire"}); master.player.arts.main = "fire"; master.player.arts.manuals["fire"] = 5;
        hand(*master.player.arts.battle, {"fire"}); battleTurn(master, "fire");
        check(novice.player.arts.battle->burn > plain.player.arts.battle->burn, "火系功法增加首张神通施加的灼烧");
        check(master.player.arts.battle->burn > novice.player.arts.battle->burn, "功法层数提高灼烧附加量");
        Game combo = fight({"woodSeal", "fire"}, {"3-2", "3-4"}); Battle& b = *combo.player.arts.battle;
        hand(b, {"woodSeal", "fire"}); battleTurn(combo, "woodSeal"); battleTurn(combo, "fire");
        checkEq(b.burn, jsRound((BATTLE_POWER[combo.player.stage] + 12) * 0.1 * 1.5 * 2 * 1.5),
                "相生、灼烧翻倍与薪火加成相乘，不受旧24%单次截断");
    }
    {
        Game g = fight({"fire", "fireburst"}); Battle& b = *g.player.arts.battle;
        hand(b, {"fire"}); battleTurn(g, "fire"); const int first = b.burn;
        b.intentKind = "guard"; const int hp = b.enemyHp; battleTurn(g, "end");
        checkEq(hp - b.enemyHp, first, "灼烧第一回合只跳一次且穿过护盾");
        checkEq(b.burnTurns, 2, "灼烧跳伤后剩两回合");
        hand(b, {"fire"}); battleTurn(g, "fire");
        checkEq(b.burn, first * 2, "跨回合再施火仍保留旧灼烧");
        checkEq(b.burnTurns, 3, "跨回合再施火刷新三回合");
        for (int remaining = 2; remaining >= 0; --remaining) {
            b.intentKind = "guard"; const int before = b.enemyHp; battleTurn(g, "end");
            checkEq(before - b.enemyHp, first * 2, "刷新后的灼烧每回合等量结算");
            checkEq(b.burnTurns, remaining, "灼烧持续回合逐次递减");
        }
        checkEq(b.burn, 0, "灼烧第三次跳伤后解除");
        b.intentKind = "guard"; const int after = b.enemyHp; battleTurn(g, "end");
        checkEq(b.enemyHp, after, "已到期灼烧不会继续跳伤");
    }
    {
        for (int remaining : {2, 1}) {
            Game g = fight({"fireburst"}, {"3-5"}); Battle& b = *g.player.arts.battle;
            b.burn = 100; b.burnTurns = remaining; hand(b, {"fireburst"});
            Battle plain = b; plain.burn = plain.burnTurns = 0;
            const auto preview = battlePreview(g.player, b, "fireburst");
            checkEq(preview.damage - battlePreview(g.player, plain, "fireburst").damage,
                    jsRound(100 * remaining * 1.5), "爆焰只放大剩余回合预算，不补已跳伤回合");
            const int before = b.enemyHp; battleTurn(g, "fireburst");
            checkEq(before - b.enemyHp, preview.damage, "剩余一或两回合引爆预览等于实际");
        }
    }
    {
        Game g = fight({"corrode", "poisonBurst", "fire"}); Battle& b = *g.player.arts.battle;
        for (int i = 0; i < 3; ++i) { hand(b, {"corrode"}); battleTurn(g, "corrode"); }
        checkEq(b.poison, 9, "无功法三次腐毒咒天然累积九层，不被六层封顶");
        const int enemyHp = b.enemyHp; b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(enemyHp - b.enemyHp, jsRound((BATTLE_POWER[g.player.stage] + 12) * 0.035 * 9),
                "九层毒按真实层数跳伤");
        checkEq(b.poison, 9, "无毒增长功法时持续中的毒保留全部层数");
        b.enemyShield = 0; hand(b, {"poisonBurst"}); const auto preview = battlePreview(g.player, b, "poisonBurst");
        const int before = b.enemyHp; battleTurn(g, "poisonBurst");
        checkEq(before - b.enemyHp, preview.damage, "高于六层的毒爆预览等于真实伤害");
        checkEq(b.poison, 0, "毒爆消费所有天然叠加毒层"); checkEq(b.poisonTurns, 0, "毒爆解除持续毒伤");
        b.poison = BATTLE_POISON_CAP - 1; b.poisonTurns = 2; hand(b, {"corrode"}); battleTurn(g, "corrode");
        checkEq(b.poison, BATTLE_POISON_CAP, "毒只在九十九层安全上限封顶");
        b.burn = BATTLE_BURN_CAP - 1; b.burnTurns = 1;
        hand(b, {"fire"}); const auto lastBurn = battlePreview(g.player, b, "fire");
        check(lastBurn.text.find("灼烧+1") != string::npos && lastBurn.text.find("剩余3回合") != string::npos,
              "灼烧预览明确展示安全限处实际新增一伤害及持续回合");
        battleTurn(g, "fire");
        checkEq(b.burn, BATTLE_BURN_CAP, "接近安全上限仍增加灼烧且不会溢出");
        checkEq(b.burnTurns, 3, "安全上限仍正常刷新持续回合");
        hand(b, {"fire"});
        check(battlePreview(g.player, b, "fire").text.find("灼烧+0") != string::npos,
              "灼烧已满安全限时明确展示本次新增零");
        hand(b, {"corrode"});
        check(battlePreview(g.player, b, "corrode").text.find("毒+0层") != string::npos,
              "毒已满安全限时明确展示本次新增零层");
    }
    {
        Game g = fight({"woodSeal"}, {"1-4", "1-5"}); Battle& b = *g.player.arts.battle;
        b.hp = b.max / 2; hand(b, {"woodSeal"}); const int hp = b.hp;
        battleTurn(g, "woodSeal"); check(b.hp > hp, "木系新增毒层触发毒疗");
        b.intentKind = "guard"; battleTurn(g, "end"); checkEq(b.poison, 3, "仍持续的毒在回合末生长1层");
    }
    {
        Game g = fight({"waterSeal", "soulspark", "mountainSeal", "sword"}); Battle& b = *g.player.arts.battle;
        b.water = 6; b.soul = 6; b.charge = 4; b.swordQi = 8;
        check(battlePreview(g.player, b, "waterSeal").text.find("水势+0层") != string::npos,
              "满水势预览显示实际新增零层");
        check(battlePreview(g.player, b, "soulspark").text.find("震荡+0层") != string::npos,
              "满震荡预览显示实际新增零层");
        check(battlePreview(g.player, b, "mountainSeal").text.find("蓄势+0层") != string::npos,
              "满蓄势预览显示实际新增零层");
        check(battlePreview(g.player, b, "sword").text.find("剑气+0层") != string::npos,
              "满剑气预览显示实际新增零层");
    }
    {
        Game g = fight({"corrode"}, {"1-5"}); Battle& b = *g.player.arts.battle;
        for (int i = 0; i < 3; ++i) { hand(b, {"corrode"}); battleTurn(g, "corrode"); }
        b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(b.poison, 10, "蔓毒在九层基础上继续生长，不退回旧六层上限");
        b.poison = BATTLE_POISON_CAP; b.poisonTurns = 2;
        b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(b.poison, BATTLE_POISON_CAP, "蔓毒增长只受九十九层安全上限限制");
        b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(b.poison, 0, "毒到期清除全部层数，蔓毒不能让已到期毒复生");
        checkEq(b.poisonTurns, 0, "高层毒三回合到期后解除持续标记");
    }
    {
        Game g = fight({"earth", "goldslash"}, {"4-4", "4-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"earth", "goldslash"}); battleTurn(g, "earth");
        checkEq(b.charge, 1, "土系实际获盾积攒重势");
        const int enhanced = battlePreview(g.player, b, "goldslash").damage;
        Battle plain = b; plain.shield = 0;
        check(enhanced > battlePreview(g.player, plain, "goldslash").damage, "重击追加现有护盾伤害");
    }
    {
        Game g = fight({"skim"}, {"5-4", "5-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"skim"}); checkEq(battleCost(g.player, *battleSpell("skim")), 0, "神道抽牌减费实际降至0");
        const int mana = b.mana; battleTurn(g, "skim");
        checkEq(b.hand.size(), 3, "神道抽牌加成多抽1张"); checkEq(b.mana, mana, "0费抽牌不扣灵力");
    }
    {
        Game g = fight({"hoard"}, {"8-4", "8-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"hoard"}); b.mana = 6; battleTurn(g, "hoard");
        checkEq(b.carryMana, 4, "气道封存基础2加额外2");
        const int predicted = battleNextMana(g.player, b); b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(b.mana, predicted, "少出牌聚灵加封存返还与预测一致"); checkEq(b.carryMana, 0, "封存只返还一次");
    }
    {
        Game g = fight({"woodSeal", "fire", "earth", "gold", "waterSeal"}, {"9-4", "9-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"woodSeal", "fire", "earth", "gold", "waterSeal"});
        battleTurn(g, "woodSeal"); const int mana = b.mana; battleTurn(g, "fire");
        checkEq(b.mana, mana - battleCost(g.player, *battleSpell("fire")) + 1, "阵系相生即时返灵1");
        checkEq(b.comboDraws, 1, "第一次相生额外抽1张");
        battleTurn(g, "earth"); battleTurn(g, "gold"); battleTurn(g, "waterSeal");
        checkEq(b.comboDraws, 2, "同回合多次相生最多额外抽2张");
        check(partitioned(b), "相生连抽后牌堆仍守恒");
    }
    {
        Game g = fight({"gather"}, {"6-4", "6-5"}); Battle& b = *g.player.arts.battle;
        b.hp = b.max / 2; hand(b, {"gather", "strike", "defend"}); const int hp = b.hp;
        battleTurn(g, "gather"); battleTurn(g, "strike"); battleTurn(g, "defend");
        checkEq(b.hp, hp + jsRound(b.max * 0.02), "体道每第三次出牌回复2%气血");
        b.intentKind = "attack"; b.intentName = "反震测试"; b.intentRaw = 20;
        const int enemy = b.enemyHp; battleTurn(g, "end");
        checkEq(b.enemyHp, enemy - 3, "格挡20伤害反震15%即3伤害");
    }
    {
        Game g = fight({"wood"}, {"10-4", "10-5"}); Battle& b = *g.player.arts.battle;
        hand(b, {"wood"}); b.playerPoison = 2; b.playerPoisonTurns = 3;
        battleTurn(g, "wood"); checkEq(b.playerPoison, 0, "丹道治疗神通清除2点自身毒伤");
        checkEq(b.playerPoisonTurns, 0, "自身毒伤归零清除持续回合");
    }
    {
        Game g = fight({"gold"}, {"11-4", "11-5"}); Battle& b = *g.player.arts.battle;
        checkEq(b.shield, jsRound(b.max * 0.14), "器道提供开局10%加本回合4%护盾");
        b.intentKind = "guard"; battleTurn(g, "end");
        checkEq(b.shield, jsRound(b.max * 0.04), "器道后续每回合重新提供4%护盾");
    }
}

} // namespace nztest
