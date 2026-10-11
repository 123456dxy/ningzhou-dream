// 宁州一梦 · C++ 版 — 神通卡牌斗法实现（对应 Demo 07 的 battle.mjs）
//
// Demo05 的「直接选招 + 立即反击」已被 Demo07 的卡牌循环取代，
// 所以本文件是重写而非修补。旧的逐回合数值对照测试也随之更新。
#include "core/battle.h"
#include "core/artifact.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/talents.h"
#include "core/sect_trial.h"

#include <algorithm>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/conflict.h"
#include "core/economy.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/tide.h"
#include "data/reference_tables.h"

namespace nz {

static void battleEnemyInit(Game& g, const EnemySpec& spec, bool opening = true);
static void battleEnemyPlan(Game& g);
static void battleEnemyAct(Game& g);
static int battleIncomingLoss(const Player& p, const Battle& b, int& blocked);

const int BATTLE_POWER[13] = {14, 24, 39, 65, 95, 140, 210, 310, 440,
                              660, 900, 1250, 1800};

// ------------------------------------------------------------
// 神通表（基础 12 种 + 拓展 28 种）
// ------------------------------------------------------------
const vector<SpellDef>& allSpells() {
    static const vector<SpellDef> data = [] {
        vector<SpellDef> v;
        auto add = [&](const char* id, const char* name, const char* dao,
                       int cost, const char* kind, double scale, int stage,
                       int cooldown = 0) {
            SpellDef s;
            s.id = id; s.name = name; s.dao = dao; s.cost = cost;
            s.kind = kind; s.scale = scale; s.stage = stage;
            s.cooldown = cooldown;
            v.push_back(s);
        };
        add("strike", "普通攻击", "气", 0, "attack", 0.48, 0);
        add("gold", "金芒刺", "金", 2, "attack", 0.8, 0);
        add("wood", "回春术", "木", 3, "heal", 0.22, 0);
        add("water", "水灵盾", "水", 2, "shield", 0.18, 0);
        add("fire", "焰火诀", "火", 3, "fire", 0.72, 0);
        add("earth", "岩甲术", "土", 3, "shield", 0.25, 0);
        add("sword", "御剑诀", "剑", 3, "sword", 0.85, 0);
        add("mind", "摄魂术", "神", 4, "drain", 0.8, 3);
        add("body", "崩山劲", "体", 2, "attack", 0.65, 0);
        add("array", "两仪守阵", "阵", 4, "guard", 0.28, 3, 2);
        add("rain", "剑雨", "剑", 6, "sword", 1.5, 6);
        add("sun", "烈阳真火", "火", 6, "fire", 1.35, 6);

        // ---- 灵力经营（参考杀戮尖塔的能量卡）----
        // 三条路线，每条都带代价，不存在白拿：
        //   蓄灵：把没用完的灵力留到下回合，代价是本回合不再回满
        //   聚气：每回合灵力变多，代价是气血上限被砍
        //   夺灵：立刻拿灵力，代价是此后每回合变少
        add("hoard", "藏灵诀", "气", 1, "store", 0.5, 3);
        add("qiFlow", "聚气诀", "气", 2, "flow", 0.5, 6);
        add("bloodfuel", "焚血诀", "体", 0, "burst", 3.0, 3);
        add("skim", "明心咒", "神", 1, "draw", 2.0, 0);
        add("scheme", "藏机术", "阵", 1, "foresee", 2.0, 3);
        add("insightdraw", "洞观", "神", 2, "draw", 3.0, 6, 2);
        add("refresh", "换念诀", "神", 2, "redraw", 1.0, 0, 1);
        add("purge", "忘机咒", "神", 2, "exhaust", 2.0, 3, 2);
        add("swordFocus", "剑意激发", "剑", 2, "empower", 0.10, 0, 2);
        add("aegis", "厚土护身", "土", 2, "ward", 0.05, 3, 2);
        add("fireburst", "引焰爆", "火", 4, "fireBurst", 0.65, 3, 1);
        for (const auto& bs : data::SpellDef_TABLE()) {
            SpellDef s;
            s.id = bs.id; s.name = bs.name; s.dao = bs.dao;
            s.cost = bs.cost; s.kind = bs.kind; s.scale = bs.scale;
            s.stage = bs.stage; s.cooldown = bs.cooldown;
            if (std::none_of(v.begin(), v.end(), [&](const SpellDef& old) {
                    return old.id == s.id;
                })) v.push_back(s);
        }
        // 所有治疗神通默认冷却 2 回合
        for (auto& s : v)
            if (s.kind == "heal" && s.cooldown == 0) s.cooldown = 2;
        return v;
    }();
    return data;
}

const SpellDef* battleSpell(const string& id) {
    // 守势是内置卡，不在神通表里
    static const SpellDef defend{"defend", "普通防御", "气", 1, "shield", 0.12, 0,
                                 0};
    if (id == "defend") return &defend;
    for (const auto& s : allSpells())
        if (s.id == id) return &s;
    return nullptr;
}

bool battleNeedsTarget(const SpellDef& spell) { return spell.kind == "exhaust"; }

static const SpellDef* battleResolveSpell(const Player& p, const string& id,
                                         optional<ArtifactAbility>& ability) {
    if (id.rfind("artifact:", 0) == 0) {
        ability = equippedArtifactAbility(p, id);
        return ability ? &ability->spell : nullptr;
    }
    return battleSpell(id);
}

string battleSpellDescription(const SpellDef& spell) {
    const string& k = spell.kind;
    string text;
    if (spell.id == "strike") text = "固定普通招式，消耗1灵力造成直接伤害；不占神通槽位。";
    else if (spell.id == "defend") text = "固定普通招式，消耗1灵力获得本回合12%气血护盾；不占神通槽位。";
    else if (k == "draw") text = "立即抽" + std::to_string(std::max(1, jsRound(spell.scale))) + "张牌，手牌最多8张。";
    else if (k == "foresee") text = "下一回合多抽" + std::to_string(std::max(1, jsRound(spell.scale))) + "张牌，手牌最多8张。";
    else if (k == "redraw") text = "弃置其余手牌，重抽等量牌，再抽" + std::to_string(std::max(1, jsRound(spell.scale))) + "张；弃牌可触发金系效果。";
    else if (k == "exhaust") text = "选择另一种神通，将其全部副本从本场手牌、抽牌堆与弃牌堆移除，再抽" + std::to_string(std::max(1, jsRound(spell.scale))) + "张。可选普通招式，下场恢复。";
    else if (k == "empower") text = "本场伤害加成+" + std::to_string(jsRound(spell.scale * 100)) + "%，累计最多30%，不消耗气血。";
    else if (k == "ward") text = "本场额外减伤+" + std::to_string(jsRound(spell.scale * 100)) + "%，累计最多20%；总减伤最多35%。";
    else if (k == "mana") text = "立即恢复" + std::to_string(std::max(1, jsRound(spell.scale / 2.0))) + "灵力，不造成伤害。";
    else if (k == "store") text = "封存剩余灵力至下回合，基础上限" + std::to_string(std::max(1, jsRound(spell.scale * 4))) + "；本回合灵力清零，气系功法增加封存上限。";
    else if (k == "flow") text = "本场每回合回复灵力+1，每次施放损失当前气血上限8%。";
    else if (k == "burst") text = "立即获得" + std::to_string(std::max(2, jsRound(spell.scale / 1.5))) + "灵力，本场此后每回合回复灵力−1。";
    else if (k == "float") text = "立即恢复2灵力，随后两回合自然回复−1。";
    else if (k == "heal") text = "回复气血，单次最高14%气血上限；丹系高阶效果可清除自身毒伤。";
    else if (k == "shield" || k == "guard") {
        text = "获得本回合" + std::to_string(jsRound(spell.scale * 100)) + "%气血护盾，护盾累计最多30%。";
        if (k == "guard") text += "本回合额外减伤10%。";
        if (spell.id == "array") text += "同时回复1灵力。";
    } else if (k == "sword") text = "剑系攻击并积攒剑气；每层剑气增伤7%，最多8层。连续剑牌可触发剑系功法。";
    else if (k == "human") text = "剑系爆发，消耗全部剑气与当前8%气血，气血最低保留1。";
    else if (k == "heavy") text = "消耗全部重势，每层增伤12%；土系功法可追加护盾伤害。";
    else if (k == "charge") text = "攻击并积攒1层重势，最多4层；重势强化重击。";
    else if (k == "water") text = "攻击并积攒2层水势，最多6层，可立即衔接浪击。";
    else if (k == "wave") text = "消耗水势，每层至少增伤15%；水系功法可保留水势、返还灵力。";
    else if (k == "poison") text = "攻击并施加" + std::to_string(spell.id == "corrode" || spell.id == "toxinRain" ? 3 : 2) + "层毒，天然叠加，最多" + std::to_string(BATTLE_POISON_CAP) + "层；再次施毒刷新3回合，木系功法可触发毒疗。";
    else if (k == "poisonBurst") text = "消耗全部毒层，每层追加基础战力18%的伤害。";
    else if (k == "fire") text = "攻击并施加灼烧，天然叠加并刷新3回合；火系功法增加每次施加量，相生提高灼烧伤害。";
    else if (k == "fireBurst") text = "攻击并引爆现有灼烧，追加剩余全部回合的灼烧伤害，然后清除燃烧。";
    else if (k == "soul") text = "攻击并积攒2层震荡，最多6层。";
    else if (k == "soulBurst") text = "消耗全部震荡，每层增伤15%。";
    else if (k == "bodyBurst") text = "体魄强化爆发，消耗重势，每层增伤10%；施放前灵力至少3再增伤15%。";
    else if (k == "drain") text = "神识攻击，穿透全部护盾。";
    else if (k == "steal") text = "攻击并回复1灵力，夺灵功法最多再增加1。";
    else text = "造成直接伤害，可衔接五行相生连击。";
    if (spell.cooldown > 0) text += "冷却" + std::to_string(spell.cooldown) + "回合。";
    return text;
}

// ------------------------------------------------------------
// 基础公式
// ------------------------------------------------------------
double battleSwordBonus(const Player& p) {
    (void)p;
    return 0;
}

int battleBasicCopies() { return 5; }

int battleSpeed(const Player& p) { return artEscapeSpeed(p); }

double battleEscapeChance(const Player& p, const Battle& b) {
    if (!b.lethal) return 1.0;
    const int playerSpeed = std::max(1, battleSpeed(p) + b.speed);
    const int enemySpeed = b.enemySpeed > 0 ? b.enemySpeed : 20 + 8 * b.enemyStage;
    return clamp(0.50 + (playerSpeed - enemySpeed) /
                         static_cast<double>(std::max(20, enemySpeed)) * 0.40 +
                     (BATTLE_POWER[clampInt(p.stage, 0, 12)] - b.enemyPower) /
                         static_cast<double>(std::max(60, b.enemyPower)) * 0.10,
                 0.10, 0.95);
}

int battleSpellSlots(const Player& p) {
    return 4 + 2 * clampInt(p.stage / 3, 0, 4);
}

int battleManaCap(const Player& p) {
    return std::max(12, talentMods(p).mana) + talentManaBonus(p);
}

int battleEnergy(const Player& p) {
    const int manaCap = talentMods(p).mana;
    const int v = 3 + clampInt(p.stage / 3, 0, 4) +
                  std::max(0, (manaCap - 8) / 4) +
                  (artHas(p.arts, "气", 1) ? 1 : 0);
    return std::min(battleManaCap(p), v);
}

int battleNextMana(const Player& p, const Battle& b) {
    const int quiet = b.played <= 2 ? jsFloor(talentMods(p).quietMana) : 0;
    const int base = std::max(1, b.maxMana + b.manaBonus - b.manaPenalty -
                                 (b.lock > 0 ? 1 : 0));
    return std::min(battleManaCap(p), base + b.carryMana + quiet);
}

double battleReduction(const Player& p, const Battle& b) {
    return clamp(talentMods(p).reduction + b.reduction + b.guard, -0.10, 0.35);
}

int battleCost(const Player& p, const SpellDef& x) {
    // 守势与灵击固定 1 点
    if (x.id == "defend" || x.id == "strike") return 1;
    int base = x.cost;
    // 气道二层对金/水两系减 1
    if (artHas(p.arts, x.dao, 2) && (x.dao == "金" || x.dao == "水")) base -= 1;
    if (base < 0) base = 0;
    int cost = (base + 1) / 2;   // 折半向上取整
    if (x.kind == "draw" || x.kind == "foresee" || x.kind == "redraw" ||
        x.kind == "exhaust") cost -= jsFloor(talentMods(p).drawCost);
    return std::max(0, cost);
}

// ------------------------------------------------------------
// 敌方意图
// ------------------------------------------------------------
Intent spellToIntent(const SpellDef& sp) {
    Intent it;
    it.spell = sp.id;
    it.name = sp.name;
    it.scale = sp.scale;
    const string& k = sp.kind;
    // 神通种类很多，归到执行端认识的几种效果上
    if (k == "guard" || k == "shield" || k == "float") {
        it.kind = "guard";
        it.scale = 0;
    } else if (k == "heal") {
        it.kind = "heal";
    } else if (k == "poison") {
        it.kind = "poison";
        it.scale = std::max(0.5, sp.scale);
    } else if (k == "drain" || k == "soul" || k == "soulBurst") {
        it.kind = "drain";
        it.scale = std::max(0.6, sp.scale);
    } else if (k == "steal" || k == "mana") {
        it.kind = "weaken";
        it.scale = std::max(0.4, sp.scale);
    } else if (k == "store" || k == "flow" || k == "burst" ||
               k == "draw" || k == "foresee" || k == "redraw" ||
               k == "exhaust" || k == "empower" || k == "ward") {
        // 灵力经营类：对方用出来就是「凝神蓄势」—— 归到凝盾，
        // 相当于它在为下一手做准备，玩家能看出来。
        it.kind = "guard";
        it.scale = 0;
    } else if (k == "heavy" || k == "bodyBurst" || k == "fire" || k == "wave") {
        it.kind = "heavy";
        it.scale = std::max(0.9, sp.scale);
    } else {
        it.kind = "attack";
    }
    return it;
}

Intent battleIntent(const Battle& b) {
    Intent it;
    const int next = b.turn + 1;

    // 每 4 回合必有一次凝盾回气（无论人兽，都留一口气）
    if (next % 4 == 0) {
        it.kind = "guard"; it.name = "凝盾回气"; it.scale = 0;
        return it;
    }

    // 人类修士：优先用本命神通（「神通 + 平A」配合）
    // 妖兽 enemyBeast=true，enemySpells 为空，直接落到下面的力大砖飞分支。
    if (!b.enemySpells.empty()) {
        // 奇数回合才动神通，偶数回合平A —— 这才是「配合」而不是狂甩神通。
        // 加上冷却下限 2，同一门神通不会连着两回合出现。
        if (next % 2 == 1) {
            vector<const SpellDef*> ready;
            for (const auto& id : b.enemySpells) {
                const SpellDef* sp = battleSpell(id);
                if (!sp) continue;
                auto cd = b.enemyCd.find(id);
                if (cd != b.enemyCd.end() && cd->second > 0) continue;
                ready.push_back(sp);
            }
            if (!ready.empty()) {
                // 用回合数做稳定挑选（战斗确定性，便于复现与测试）
                const SpellDef* sp = ready[static_cast<size_t>(b.turn) % ready.size()];
                return spellToIntent(*sp);
            }
        }
        // 平A 回合 / 神通都在冷却
        it.kind = "attack";
        it.name = (b.enemyDao.empty() ? string("正面攻击")
                                      : b.enemyDao + "系平击");
        it.scale = 0.85;
        return it;
    }

    // 妖兽 / 无神通的对手：力大砖飞，只会砸
    const string mode = b.style.empty() ? "剑" : b.style;
    if (mode == "神" && next % 3 == 0) {
        it.kind = "pierce"; it.name = "神识穿透"; it.scale = 1.0;
        return it;
    }
    if (mode == "火" && next % 3 == 0) {
        it.kind = "heavy"; it.name = "烈焰爆发"; it.scale = 1.3;
        return it;
    }
    if (mode == "木" && next % 3 == 0) {
        it.kind = "heal"; it.name = "回春反击"; it.scale = 0.65;
        return it;
    }
    it.kind = "attack";
    it.name = (mode == "体") ? "近身重拳"
              : (mode == "水") ? "水剑连斩"
                               : "正面攻击";
    it.scale = 0.85;
    return it;
}

bool battleActive(const Game& g) { return g.player.arts.battle.has_value(); }

// ------------------------------------------------------------
// 洗牌与抽牌
// ------------------------------------------------------------
static void battleShuffle(Game& g, vector<string>& pile) {
    // 与 JS 的 Fisher-Yates 一致（从后往前，j 取 [0, i]）
    for (int i = static_cast<int>(pile.size()) - 1; i > 0; --i) {
        const int j = static_cast<int>(randomOf(g) * (i + 1));
        std::swap(pile[static_cast<size_t>(i)], pile[static_cast<size_t>(j)]);
    }
}

void battleDraw(Game& g, int n) {
    if (!g.player.arts.battle.has_value()) return;
    Battle& b = *g.player.arts.battle;
    const auto removed = [&](const string& uid) {
        return std::find(b.exhausted.begin(), b.exhausted.end(), uid) != b.exhausted.end();
    };
    b.draw.erase(std::remove_if(b.draw.begin(), b.draw.end(), removed), b.draw.end());
    b.discard.erase(std::remove_if(b.discard.begin(), b.discard.end(), removed), b.discard.end());
    while (n-- > 0 && b.hand.size() < 8) {
        if (b.draw.empty()) {
            if (b.discard.empty()) break;   // 两堆都空，抽不出牌
            b.draw = b.discard;
            b.discard.clear();
            battleShuffle(g, b.draw);
            b.shuffles++;
        }
        b.hand.push_back(b.draw.back());
        b.draw.pop_back();
    }
}

static const Card* findCard(const Battle& b, const string& uid) {
    for (const auto& c : b.deck)
        if (c.uid == uid) return &c;
    return nullptr;
}

// 确定并存下敌方本回合意图（含已算好的原始伤害）
static void battlePlan(Game& g) {
    Battle& b = *g.player.arts.battle;
    if (b.enemyCards.enabled) {
        battleEnemyPlan(g);
        return;
    }
    const Intent it = battleIntent(b);
    b.intentKind = it.kind;
    b.intentName = it.name;
    b.intentScale = it.scale;
    b.intentSpell = it.spell;
    b.intentRaw = jsRound((b.enemyPower + 12) * it.scale *
                          (0.94 + randomOf(g) * 0.12));
    // 用掉的神通进冷却，避免对方每回合都甩同一门。
    // 下限 2：很多神通表里 cooldown 是 0，直接照搬会变成狂甩同一招。
    if (!it.spell.empty()) {
        const SpellDef* sp = battleSpell(it.spell);
        b.enemyCd[it.spell] = sp ? std::max(2, sp->cooldown) : 2;
    }
}

// ------------------------------------------------------------
// 初始化牌组（对应 battleInitCards）
// ------------------------------------------------------------
static int dungeonRelicValue(const Game& g, bool draw) {
    if (!g.dungeons.run || !g.player.arts.battle ||
        g.player.arts.battle->dungeonSite != g.dungeons.run->siteId) return 0;
    int amount = 0;
    for (const auto& id : g.dungeons.run->relics)
        for (const auto& relic : dungeonRelics()) if (relic.id == id)
            amount += draw ? relic.draw : relic.mana;
    return amount;
}

static void battleInitCards(Game& g, bool migrate) {
    Player& p = g.player;
    Battle& b = *p.arts.battle;

    // 额外神通各2张，普通攻防固定各5张，过滤旧档的基础牌和重复牌。
    vector<string> spells;
    vector<string> selected;
    for (const auto& id : p.arts.spells) {
        const SpellDef* spell = battleSpell(id);
        if (!spell || id == "strike" || id == "defend" || p.stage < spell->stage ||
            std::find(selected.begin(), selected.end(), id) != selected.end()) continue;
        if (static_cast<int>(selected.size()) >= battleSpellSlots(p)) break;
        selected.push_back(id);
        spells.push_back(id);
        spells.push_back(id);
    }
    for (int i = 0; i < battleBasicCopies(); ++i) {
        spells.push_back("strike");
        spells.push_back("defend");
    }

    b.deck.clear();
    for (size_t i = 0; i < spells.size(); ++i)
        b.deck.push_back({"card-" + std::to_string(i), spells[i]});
    if (g.dungeons.run && b.dungeonSite == g.dungeons.run->siteId)
        b.deck = g.dungeons.run->deck;

    b.draw.clear();
    for (const auto& c : b.deck) b.draw.push_back(c.uid);
    battleShuffle(g, b.draw);

    b.hand.clear();
    b.discard.clear();
    b.exhausted.clear();
    b.shuffles = 0;
    b.played = 0;
    b.attacked = false;
    b.guard = 0.0;
    b.maxMana = battleEnergy(p) + dungeonRelicValue(g, false);

    if (migrate) {
        // 读旧档：旧灵力折半向上取整
        b.mana = std::min(b.maxMana, (b.mana + 1) / 2);
    } else {
        const double opening = talentMods(p).openingMana +
                               p.arts.combatPrep["mana"];
        b.mana = std::min(battleManaCap(p), b.maxMana +
                                static_cast<int>(std::ceil(opening / 2.0)) +
                                (artHas(p.arts, "气", 2) ? 1 : 0));
    }
    battleDraw(g, 5 + dungeonRelicValue(g, true));
    battlePlan(g);
}

// ------------------------------------------------------------
// 开始斗法
// ------------------------------------------------------------
Battle battleReferenceState(const Player& p) {
    const Arts& a = p.arts;
    const ArtMods m = talentMods(p);
    const auto prep = [&](const char* key) {
        const auto it = a.combatPrep.find(key);
        return it == a.combatPrep.end() ? 0.0 : it->second;
    };
    const int base = BATTLE_POWER[clampInt(p.stage, 0, 12)];
    Battle b;
    b.max = jsRound((110 + base * 2.4) * m.hp *
                    (1.0 + cultivate::bodyHpBonus(p.body)));
    b.hp = std::max(1, jsRound(b.max * p.health / 100.0));
    b.shield = std::min(jsRound(b.max * 0.3),
        jsRound(b.max * ((artHas(a, "阵", 1) ? 0.12 : 0.0) + m.openingShield + m.roundShield)) +
        jsRound(b.max * prep("shield")));
    b.swordQi = std::min(8, jsFloor(m.openingQi) +
        (artHas(a, "剑", 2) ? 2 : 0) + jsFloor(prep("qi")));
    b.buff = a.combatBuff;
    b.reduction = prep("reduction");
    b.speed = jsRound(prep("speed"));
    b.maxMana = battleEnergy(p);
    b.mana = std::min(battleManaCap(p), b.maxMana +
        static_cast<int>(std::ceil((m.openingMana + prep("mana")) / 2.0)) +
        (artHas(a, "气", 2) ? 1 : 0));
    // 目标只有参考气血，不设护盾、减伤、功法和行动；不会接入真实战斗。
    b.enemyHp = b.enemyMax = 1000000000;
    return b;
}

void battleStart(Game& g, const EnemySpec& enemy) {
    Player& p = g.player;
    Arts& a = p.arts;
    const int enemyBase = BATTLE_POWER[clampInt(enemy.stage, 0, 12)];
    Battle b = battleReferenceState(p);
    const int maxHp = b.max;

    // Demo07：对手气血额外 ×1.45
    const int enemyMaxHp = jsRound(
        (110 + enemyBase * 2.4) * 1.45 *
        (enemy.health ? std::max(0.5, enemy.health / 100.0) : 1.0));

    b.enemy = enemy.name;
    b.npcId = enemy.id;
    b.quest = enemy.quest;
    b.kind = enemy.kind;
    b.lethal = enemy.lethal;
    b.huntFee = enemy.huntFee;
    b.wildTask = enemy.wildTask;
    b.dungeonSite = enemy.dungeonSite;
    b.dungeonNode = enemy.dungeonNode;
    b.advFee = enemy.wildFee;
    if (enemy.adventure) {
        const AdvContext& context = *enemy.adventure;
        b.advKind = context.kind;
        b.advSeq = context.seq;
        b.advChoice = enemy.adventureChoice;
        b.advFee = context.fee;
        b.advGrade = context.grade;
        b.advBook = context.book;
        b.advNpcId = context.npcId;
        b.advStyle = context.style;
    }
    b.turn = 0;
    b.max = maxHp;
    b.hp = std::max(1, jsRound(maxHp * p.health / 100.0));
    b.enemyMax = enemyMaxHp;
    b.enemyHp = enemyMaxHp;
    b.enemyPower = enemyBase;
    b.enemySword = (enemy.sword >= 0) ? enemy.sword
                                      : std::min(500, 15 + enemy.stage * 20);
    b.enemySwordCult = (enemy.style == "剑") || (enemy.sect == "jinhong");

    // 对方的功法：人类修士带本命神通（「神通 + 平A」配合），
    // 妖兽不带（enemySpells 空着，battleIntent 会走「力大砖飞」）。
    b.enemyDao.clear();
    b.enemySpells.clear();
    b.enemyBeast = false;
    b.enemyCd.clear();
    b.enemyWeaken = 0;
    if (!enemy.id.empty()) {
        for (const auto& n : g.npcs) {
            if (n.id != enemy.id) continue;
            b.enemyDao = n.dao;
            b.enemyBeast = n.beast;
            if (!n.beast) b.enemySpells = n.spells;
            break;
        }
    }

    b.enemyShield = 0;
    b.burn = 0;
    b.guard = 0.0;
    b.attacked = false;
    b.played = 0;

    if (!enemy.style.empty()) {
        b.style = enemy.style;
    } else {
        static const std::pair<const char*, const char*> table[5] = {
            {"jinhong", "剑"}, {"xinghe", "水"}, {"zhushan", "木"},
            {"lihuo", "火"}, {"huachen", "土"}};
        b.style = "体";
        for (const auto& [sid, st] : table)
            if (enemy.sect == sid) b.style = st;
    }
    b.log.push_back(
        "每回合抽5张神通卡。出牌不触发反击；结束回合才结算持续伤害、敌方行动与护盾。最多24回合。");

    a.battle = b;

    Battle& bb = *a.battle;
    if (g.dungeons.run && bb.dungeonSite == g.dungeons.run->siteId)
        for (const auto& id : g.dungeons.run->relics)
            for (const auto& relic : dungeonRelics()) if (relic.id == id)
                bb.shield = std::min(jsRound(maxHp * 0.3), bb.shield + jsRound(maxHp * relic.shield));

    battleEnemyInit(g, enemy);

    battleInitCards(g, false);

    a.combatPrep.clear();
    a.combatBuff = 0;

    logEvent(g, "self",
             "与" + enemy.name + "开始" + (b.lethal ? "生死斗法" : "斗法") +
                 "，月历暂停。",
             true);
    if (!a.battle->playerFirst) {
        a.battle->log.push_back("对方遁速更快，抢先施放首轮神通。");
        if (a.battle->enemyCards.enabled) battleEnemyAct(g);
        else {
            int blocked = 0;
            const int loss = battleIncomingLoss(p, *a.battle, blocked);
            a.battle->hp -= loss;
            a.battle->shield -= blocked;
            a.battle->log.push_back("对方先手" + a.battle->intentName + "：气血−" +
                                   std::to_string(loss) + "。");
        }
        if (!a.battle) return;
        if (a.battle->hp <= 0) {
            battleFinish(g, false);
            return;
        }
        if (a.battle->enemyHp <= 0) {
            battleFinish(g, true);
            return;
        }
        battlePlan(g);
    } else if (a.battle) {
        a.battle->log.push_back("你的遁速取得先手，先行出牌。");
    }
}

// ------------------------------------------------------------
// 结算
// ------------------------------------------------------------
void battleFinish(Game& g, bool win, bool escape) {
    Player& p = g.player;
    Arts& a = p.arts;
    if (!a.battle.has_value()) return;
    Battle b = *a.battle;

    b.retreat = escape;
    b.won = win;
    const bool wildLoss = b.lethal || !b.dungeonSite.empty() || !b.advKind.empty() ||
        b.kind == "wild" || b.kind == "hunt" || b.kind == "tide" || b.kind == "ambush" ||
        b.kind == "kill" || b.kind == "rob";
    if (!win && !escape && wildLoss && g.checkpoint.enabled) {
        p.health=0;
        finish(g,"野外战败：" + b.enemy);
        return;
    }
    // 生死斗战败即身死；其余情况保留 1 点气血
    p.health = std::max((b.lethal && !win && !escape) ? 0 : 1,
                        std::min(100, jsRound(b.hp * 100.0 / b.max)));

    if (win && b.kind != "sectTrial") {
        a.progress.wins++;
        if (b.kind == "spar" && b.advKind.empty())
            p.stones += 20 + (p.stage / 3) * 25;
        artGain(p, a.focus, 20);
        // 机缘斗法（门派大比 / 仙与鸳鸯）胜利要发奖，
        // 否则机缘永远完不成，而且能反复花灵石重开。
        if (!b.quest.empty()) {
            for (const auto& q : data::QuestDef_TABLE()) {
                if (q.id != b.quest) continue;
                artRewardQuest(g, q);
                break;
            }
        }
    }
    if (!b.npcId.empty() && b.kind == "spar")
        addRelation(g, b.npcId, 4);

    // 兽潮迎战：赏赐比普通野外悬赏厚得多，这是「成长动力」的来源
    if (b.kind == "tide" && g.tide.active && !escape)
        tide::tideReward(g, win);

    const string outcome = escape ? "撤离"
                                   : win ? "获胜"
                                         : (b.turn >= 24 ? "回合耗尽" : "落败");
    logEvent(g, "self",
             b.enemy + "之战：" + outcome + "。" +
                 (b.lethal ? "此战有陨落风险。" : "本场不致死。"),
             true);


    // ---- 奖励结算 ----
    // 放在这里而不是各个界面里：battleFinish 是唯一的收口，
    // 控制台版和网页版都会经过它。之前把结算放在界面层，
    // 结果网页版赢了悬赏、猎妖、游历夺宝全都零收益。
    adventureBattleEnd(g, b);   // 游历遇事的战利品
    societyWildEnd(g, b);       // 野外悬赏的赏金
    ecoHuntEnd(g, win);         // 猎妖委托的赏金与材料
    conflict::battleEnd(g, b);  // 野外冲突胜利后的掠夺与生死结果

    a.battle.reset();
    if (b.kind == "sectTrial") sectTrialBattleComplete(g, b, win, escape);
    if (!b.dungeonSite.empty()) dungeonBattleComplete(g, b, win);

    // 生死斗落败 = 这一世到此为止。
    // 之前只把气血设成 0 就收尾了，玩家其实不会死，
    // 要等下一次工作结算才以「游历遇险」草草结束。
    if (b.lethal && !win && !escape) {
        finish(g, "陨落于" + b.enemy + "之手");
    } else if (b.kind == "ambush" && g.job && g.job->actionType == "travel") {
        logEvent(g, "self", "摆脱途中遭遇，可以继续未完成的行程。", true);
    }
}

// ------------------------------------------------------------
// 单张卡的效果（对应 JS 的 battleEffect）
//
// 预览与真实出牌共用这一个函数：预览作用在 Battle 的副本上，
// 所以预览显示的伤害与实际结算必然一致。
static int battleAddShield(const Player& p, Battle& b, int amount) {
    const int before = b.shield;
    b.shield = std::min(jsRound(b.max * 0.3), b.shield + std::max(0, amount));
    const int gain = std::max(0, b.shield - before);
    if (gain > 0)
        b.charge = std::min(4, b.charge + jsFloor(talentMods(p).shieldCharge));
    return gain;
}

static void battleDiscard(const Player& p, Battle& b, int count) {
    if (count <= 0) return;
    const ArtMods m = talentMods(p);
    battleAddShield(p, b, jsRound(b.max * m.discardShield * count));
    b.discardPower = std::min(0.30, b.discardPower + m.discardPower * count);
}

static string battleEffect(const Player& p, Battle& b, const SpellDef& spell,
                          double targetReduction = 0, double targetCounter = 0) {
    const Arts& a = p.arts;
    const ArtMods m = talentMods(p);
    const int base = BATTLE_POWER[clampInt(p.stage, 0, 12)];
    const string id = spell.id;
    const int cost = battleCost(p, spell);
    const int manaBefore = b.mana;
    const int manaCap = battleManaCap(p);
    b.mana -= cost;

    string text;

    if (id == "defend") {
        const int shield = jsRound(b.max * std::min(0.3, 0.12 * m.shield));
        const int gained = battleAddShield(p, b, shield);
        text = spell.name + "：本回合护盾+" + std::to_string(gained) + "。";
        b.swordSequence = 0;
    } else {
        const string kind = spell.kind;
        const string dao = spell.dao;
        const string previous = b.last;

        // 五行相生连击
        static const std::pair<const char*, const char*> gen[5] = {
            {"木", "火"}, {"火", "土"}, {"土", "金"},
            {"金", "水"}, {"水", "木"}};
        bool combo = false;
        for (const auto& [f, t] : gen)
            if (previous == f && dao == t) combo = true;
        if (previous == "木火" && dao == "火") combo = true;

        const int ei = daoIndex(dao);
        const double elementMul = (ei >= 0) ? m.elements[ei] : 1.0;

        double damage = jsRound(
            (base + 12) * spell.scale * m.attack * elementMul *
            (1 + b.buff) *
            (combo ? 1.15 + std::min(0.15, m.combo) : 1.0) *
            (dao == "火" ? m.fire : 1.0) *
            (dao == "神" ? m.drain : 1.0) *
            ((artHas(a, dao, 1) && (dao == "金" || dao == "火")) ? 1.1 : 1.0) *
            ((dao == "剑" && artHas(a, "剑", 1)) ? 1.12 : 1.0));

        bool direct = false;
        string statusText;
        const bool swordCard = kind == "sword" || kind == "human";
        if (!swordCard) b.swordSequence = 0;

        if (kind == "mana") {
            const int gain = std::max(1, jsRound(spell.scale / 2.0));
            const int before = b.mana;
            b.mana = std::min(manaCap, b.mana + gain);
            text = spell.name + "：恢复" + std::to_string(b.mana - before) + "灵力。";
        } else if (kind == "store") {
            // 蓄灵：把本回合剩下的灵力留到下回合。
            // 代价写在这张牌自己身上：本回合灵力清零、不再回满，
            // 而且封存有上限（scale 决定），不能靠它无限滚雪球。
            const int cap = std::min(manaCap, std::max(1, jsRound(spell.scale * 4.0)) + jsFloor(m.carryBonus));
            b.carryCap = cap;
            const int keep = std::min({b.mana, cap, manaCap});
            b.carryMana = keep;
            b.mana = 0;
            text = spell.name + "：将 " + std::to_string(keep) +
                   " 点灵力封存，下回合可用（上限 " + std::to_string(cap) +
                   "；本回合灵力清零）。";
        } else if (kind == "flow") {
            // 聚气：每回合灵力上限 +1，代价是气血上限下降（本场）
            b.manaBonus += 1;
            b.hpCostPct += 8;
            const int cut = jsRound(b.max * 0.08);
            b.max = std::max(1, b.max - cut);
            b.hp = std::min(b.hp, b.max);
            b.shield = std::min(b.shield, jsRound(b.max * 0.3));
            text = spell.name + "：每回合灵力 +1，代价是气血上限 −" +
                   std::to_string(cut) + "。";
        } else if (kind == "burst") {
            // 夺灵：立刻拿灵力，代价是本回合之后每回合少 1 点（本场）
            const int gain = std::max(2, jsRound(spell.scale / 1.5));
            b.mana = std::min(manaCap, b.mana + gain);
            b.manaPenalty += 1;
            text = spell.name + "：立刻获得 " + std::to_string(gain) +
                   " 灵力，此后每回合灵力 −1。";
        } else if (kind == "draw") {
            // 抽牌：本回合多抽。实际抽牌要等 battleTurn 拿到 g 再做
            // （battleEffect 也被 battlePreview 用来试算，那里没有 Game）。
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(m.drawBonus);
            b.pendingDraw += n;
            text = spell.name + "：抽 " + std::to_string(n) + " 张。";
        } else if (kind == "foresee") {
            // 预谋：下回合多抽
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(m.drawBonus);
            b.drawNext += n;
            text = spell.name + "：下回合多抽 " + std::to_string(n) + " 张。";
        } else if (kind == "redraw") {
            const int count = static_cast<int>(b.hand.size());
            b.discard.insert(b.discard.end(), b.hand.begin(), b.hand.end());
            b.hand.clear();
            battleDiscard(p, b, count);
            const int n = count + std::max(1, jsRound(spell.scale)) + jsFloor(m.drawBonus);
            b.pendingDraw += n;
            text = spell.name + "：弃置" + std::to_string(count) + "张手牌，抽" + std::to_string(n) + "张。";
        } else if (kind == "exhaust") {
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(m.drawBonus);
            b.pendingDraw += n;
            text = spell.name + "：将所选神通的全部副本移出本场牌组，抽" + std::to_string(n) + "张。";
        } else if (kind == "empower") {
            const double before = b.buff;
            b.buff = std::max(before, std::min(0.30, before + spell.scale));
            text = spell.name + "：本场伤害加成+" + std::to_string(jsRound((b.buff - before) * 100)) + "%（累计" + std::to_string(jsRound(b.buff * 100)) + "%）。";
        } else if (kind == "ward") {
            const double before = b.reduction;
            b.reduction = std::max(before, std::min(0.20, before + spell.scale));
            text = spell.name + "：本场额外减伤+" + std::to_string(jsRound((b.reduction - before) * 100)) + "%（累计" + std::to_string(jsRound(b.reduction * 100)) + "%）。";
        } else if (kind == "float") {
            b.mana = std::min(manaCap, b.mana + 2);
            b.lock = 2;
            text = "悬灵术：恢复2灵力，随后两回合灵力−1。";
        } else if (kind == "heal") {
            const int before = b.hp;
            const int heal = jsRound(
                b.max * std::min(0.14, spell.scale * m.heal *
                                           (artHas(a, "木", 1) ? 1.15 : 1.0)));
            b.hp = std::min(b.max, b.hp + heal);
            b.playerPoison = std::max(0, b.playerPoison - jsFloor(m.poisonCleanse));
            if (b.playerPoison == 0) b.playerPoisonTurns = 0;
            if (b.enemyCards.enabled && b.playerPoison == 0) {
                b.enemyCards.poison = 0;
                b.enemyCards.poisonTurns = 0;
            }
            text = spell.name + "：气血+" + std::to_string(b.hp - before) + "。";
        } else if (kind == "shield" || kind == "guard") {
            const int before = b.shield;
            const int shield = jsRound(
                b.max * std::min(0.3, spell.scale * m.shield));
            battleAddShield(p, b, shield);
            b.guard = std::max(b.guard, (kind == "guard") ? 0.1 : 0.0);
            if (spell.id == "array") b.mana = std::min(manaCap, b.mana + 1);
            text = spell.name + "：本回合护盾+" +
                   std::to_string(b.shield - before) + "。";
        } else {
            direct = true;
            // 扰灵只削弱下一次直接攻击，治疗、抽牌不会消耗它。
            if (b.enemyWeaken > 0) {
                damage = jsRound(damage * std::max(0.5, 1.0 - b.enemyWeaken * 0.15));
                b.enemyWeaken = 0;
            }
            damage = jsRound(damage * (1 + b.discardPower));
            b.discardPower = 0;

            if (kind == "sword" || kind == "human") {
                damage = jsRound(damage * m.sword * (1 + b.swordQi * 0.07));
                b.swordSequence++;
                damage = jsRound(damage * (1 + std::min(0.50, (b.swordSequence - 1) * m.swordCombo)));
                if (b.swordSequence % 3 == 0)
                    b.mana = std::min(manaCap, b.mana + jsFloor(m.swordMana));
                if (kind == "human") {
                    // 人剑消耗全部剑气与当前 8% 气血（不自伤致死）
                    b.hp = std::max(1, b.hp - std::max(1, jsRound(b.hp * 0.08)));
                    b.swordQi = 0;
                } else {
                    const int before = b.swordQi;
                    b.swordQi = std::min(8, b.swordQi + jsRound((1 + jsFloor(m.qiGain)) * spell.statusScale));
                    statusText = "剑气+" + std::to_string(b.swordQi - before) + "层（共" +
                                 std::to_string(b.swordQi) + "/8层）。";
                }
            }
            if (kind == "heavy") {
                damage = jsRound(damage * (1 + b.charge * 0.12));
                damage += jsRound(b.shield * m.shieldStrike);
                b.charge = 0;
            }
            if (kind == "wave") {
                const int stacks = b.water;
                damage = jsRound(damage *
                                 (1 + b.water * (0.15 + std::min(0.1, m.water * 0.025))));
                const double baseWater = artModsOf(p.arts).waterMana;
                const int returned = std::min(2, jsFloor(stacks * baseWater)) +
                                     (hasTalent(p, "water") ? stacks : 0);
                b.mana = std::min(manaCap, b.mana + returned);
                b.water = jsFloor(stacks * std::min(0.50, m.waterKeep));
            }
            if (kind == "poisonBurst") {
                damage += jsRound((base + 12) * b.poison * 0.18);
                b.poison = 0;
                b.poisonTurns = 0;
            }
            if (kind == "soulBurst") {
                damage = jsRound(damage * (1 + b.soul * 0.15));
                b.soul = 0;
            }
            if (kind == "fireBurst") {
                damage += jsRound(b.burn * b.burnTurns * (1 + m.burnExplosion));
                b.burn = 0;
                b.burnTurns = 0;
            }
            if (kind == "bodyBurst") {
                // Demo07：门槛从「灵力≥6」改为「灵力≥3」
                damage = jsRound(damage *
                                 (1 + std::min(0.18, p.body * 0.02)) *
                                 (manaBefore >= 3 ? 1.15 : 1.0) *
                                 (1 + b.charge * 0.1));
                b.charge = 0;
            }
            if (kind == "drain" || dao == "神")
                damage = jsRound(damage *
                                 (1 + std::min(0.15, a.mind * 0.005)) *
                                 (artHas(a, "神", 2) ? 1.2 : 1.0));
            if (kind == "steal")
                b.mana = std::min(manaCap, b.mana + 1 + std::min(1, jsFloor(m.steal)));

            // 穿盾与吸收
            damage = jsRound(damage * (1 - clamp(targetReduction, -0.10, 0.35)));
            const int pierce = (kind == "drain") ? b.enemyShield
                               : (id == "goldblade")
                                   ? jsRound(b.enemyShield * 0.5)
                                   : 0;
            const int absorb =
                std::min(std::max(0, b.enemyShield - pierce), jsRound(damage));
            b.enemyShield -= absorb;
            b.enemyHp -= jsRound(damage) - absorb;
            if (absorb > 0 && targetCounter > 0)
                b.hp -= jsRound(absorb * targetCounter);
            text = spell.name + "：伤害" +
                   std::to_string(jsRound(damage) - absorb) +
                   (combo ? "，相生连击" : "") + "。";
            text += statusText;

            if (kind == "charge") {
                const int before = b.charge;
                b.charge = std::min(4, b.charge + jsRound((1 + jsFloor(m.charge)) * spell.statusScale));
                text += "蓄势+" + std::to_string(b.charge - before) + "层（共" + std::to_string(b.charge) + "/4层）。";
            }
            if (kind == "water") {
                const int before = b.water;
                b.water = std::min(6, b.water + jsRound(2 * spell.statusScale));
                text += "水势+" + std::to_string(b.water - before) + "层（共" + std::to_string(b.water) + "/6层）。";
            }
            if (kind == "poison") {
                const int add = (id == "corrode" || id == "toxinRain") ? 3 : 2;
                const int before = b.poison;
                b.poison = std::min(BATTLE_POISON_CAP, b.poison + jsRound((add + jsFloor(m.poison)) * spell.statusScale));
                b.poisonTurns = 3;
                if (b.hp > 0)
                    b.hp = std::min(b.max, b.hp + jsRound(b.max * (b.poison - before) * m.poisonHeal));
                text += "毒+" + std::to_string(b.poison - before) + "层（共" + std::to_string(b.poison) +
                        "/" + std::to_string(BATTLE_POISON_CAP) + "层，剩余3回合）。";
            }
            if (kind == "soul") {
                const int before = b.soul;
                b.soul = std::min(6, b.soul + jsRound((2 + jsFloor(m.soul)) * spell.statusScale));
                text += "震荡+" + std::to_string(b.soul - before) + "层（共" + std::to_string(b.soul) + "/6层）。";
            }
            if (kind == "fire") {
                const double added = (base + 12) * 0.1 * (1 + m.burnBonus) *
                                     (combo ? 1.5 : 1.0) *
                                     (artHas(a, "火", 2) ? 2.0 : 1.0) *
                                     (1 + std::max(0.0, m.burnStack)) * spell.statusScale;
                const int burn = jsRound(std::min<double>(BATTLE_BURN_CAP, added));
                const int before = b.burn;
                b.burn = std::min(BATTLE_BURN_CAP,
                                  clampInt(b.burn, 0, BATTLE_BURN_CAP) + burn);
                b.burnTurns = 3;
                text += "灼烧+" + std::to_string(b.burn - before) + "（每回合当前总伤害" +
                        std::to_string(b.burn) + "，剩余3回合）。";
            }
        }

        if (combo) {
            b.mana = std::min(manaCap, b.mana + jsFloor(m.comboMana));
            const int draws = std::min(2 - b.comboDraws, jsFloor(m.comboDraw));
            b.pendingDraw += std::max(0, draws);
            b.comboDraws += std::max(0, draws);
        }

        b.attacked = b.attacked || direct;
    }

    if (b.hp > 0 && (b.played + 1) % 3 == 0 && m.thirdHeal > 0) {
        const int before = b.hp;
        b.hp = std::min(b.max, b.hp + jsRound(b.max * std::min(0.12, m.thirdHeal)));
        text += "三牌调息：气血+" + std::to_string(b.hp - before) + "。";
    }

    // 冷却按完整回合结算，所以这里 +1 补偿本回合
    if (spell.cooldown) b.cooldowns[id] = spell.cooldown + 1;
    b.last = (id == "fireSeal") ? "木火" : spell.dao;
    return text;
}

// 敌方使用独立牌堆，但每张牌共用玩家的费用、状态与连携结算。
#define ENEMY_FIELDS(X) \
    X(deck) X(hand) X(draw) X(discard) X(exhausted) X(cooldowns) \
    X(mana) X(maxMana) X(shuffles) X(played) X(attacked) \
    X(swordQi) X(water) X(soul) X(charge) X(lock) X(carryMana) \
    X(manaBonus) X(manaPenalty) X(drawNext) X(carryCap) X(hpCostPct) \
    X(poison) X(poisonTurns) X(burn) X(burnTurns) X(swordSequence) \
    X(comboDraws) X(buff) X(reduction) X(guard) X(discardPower) X(last)

static Player battleEnemyPlayer(const Game& g, const Battle& b) {
    const EnemyCombat& e = b.enemyCards;
    Player p;
    p.name = b.enemy; p.stage = e.stage; p.sect = e.sect; p.body = e.body;
    p.arts.main = e.main; p.arts.aux = e.aux; p.arts.manuals = e.manuals;
    p.arts.nodes = e.nodes; p.arts.mind = e.mind;
    for (const auto& npc : g.npcs) {
        if (npc.id != b.npcId) continue;
        p.arts.gear = npc.gear; p.arts.equipped = npc.equipped;
        break;
    }
    return p;
}

static ArtMods battleEnemyMods(const Battle& b) {
    Arts a;
    a.main = b.enemyCards.main; a.aux = b.enemyCards.aux;
    a.manuals = b.enemyCards.manuals; a.nodes = b.enemyCards.nodes;
    a.mind = b.enemyCards.mind;
    return artModsOf(a);
}

// 每个完整回合只结算一次；敌方意图也在副本上执行这一步，避免引爆
// 使用尚未扣除的持续回合，或让已经到期的毒继续参与敌方神通。
static void battleTickStatuses(const Player& p, Battle& b, bool log) {
    const ArtMods m = talentMods(p);
    const int base = BATTLE_POWER[clampInt(p.stage, 0, 12)];
    const int dot =
        (b.poisonTurns > 0 ? jsRound((base + 12) * 0.035 * b.poison) : 0) +
        (b.burnTurns > 0 ? b.burn : 0);
    b.enemyHp -= dot;
    if (log && dot) b.log.push_back("回合结束：持续伤害" + std::to_string(dot) + "。");
    if (b.poisonTurns > 0 && --b.poisonTurns == 0) b.poison = 0;
    if (b.poisonTurns > 0)
        b.poison = std::min(BATTLE_POISON_CAP, b.poison + jsFloor(m.poisonGrowth));
    if (b.burnTurns > 0 && --b.burnTurns == 0) b.burn = 0;
    if (b.enemyHp <= 0) return;

    // 敌方施加的毒与灼烧独立结算，不会反过来伤害施术者。
    const int burning = b.enemyCards.burnTurns > 0 ? b.enemyCards.burn : 0;
    if (b.playerPoisonTurns <= 0 && burning <= 0) return;
    const int poisonLoss = b.playerPoisonTurns > 0 ? b.playerPoison : 0;
    b.hp -= poisonLoss + burning;
    if (log) b.log.push_back("毒与灼烧：气血−" + std::to_string(poisonLoss + burning) + "。");
    if (b.enemyCards.burnTurns > 0 && --b.enemyCards.burnTurns == 0)
        b.enemyCards.burn = 0;
    if (b.playerPoisonTurns > 0 && --b.playerPoisonTurns == 0) b.playerPoison = 0;
    if (!b.enemyCards.enabled) return;
    b.enemyCards.poisonTurns = b.playerPoisonTurns;
    if (b.playerPoisonTurns == 0) b.enemyCards.poison = 0;
    else {
        const int growth = jsFloor(battleEnemyMods(b).poisonGrowth);
        if (growth > 0) {
            b.enemyCards.poison = std::min(BATTLE_POISON_CAP, b.enemyCards.poison + growth);
            b.playerPoison = jsRound((b.enemyPower + 12) * 0.035 * b.enemyCards.poison);
        }
    }
}

static Battle battleEnemyView(const Battle& b, int playerStage) {
    Battle v;
#define ENEMY_READ(field) v.field = b.enemyCards.field;
    ENEMY_FIELDS(ENEMY_READ)
#undef ENEMY_READ
    v.hp = b.enemyHp; v.max = b.enemyMax; v.shield = b.enemyShield;
    v.enemyHp = b.hp; v.enemyMax = b.max; v.enemyShield = b.shield;
    v.playerPoison = b.poisonTurns > 0
        ? jsRound((BATTLE_POWER[clampInt(playerStage, 0, 12)] + 12) * 0.035 * b.poison) : 0;
    v.playerPoisonTurns = b.poisonTurns;
    return v;
}

static void battleSaveEnemyView(Battle& b, const Battle& v, int playerStage) {
    const int oldPoison = b.enemyCards.poison;
    const int oldPoisonTurns = b.enemyCards.poisonTurns;
#define ENEMY_WRITE(field) b.enemyCards.field = v.field;
    ENEMY_FIELDS(ENEMY_WRITE)
#undef ENEMY_WRITE
    b.enemyHp = v.hp; b.enemyMax = v.max; b.enemyShield = v.shield;
    b.hp = v.enemyHp; b.shield = v.enemyShield;
    if (v.poison <= 0 || v.poisonTurns <= 0) b.playerPoison = 0;
    else if (oldPoison != v.poison || oldPoisonTurns != v.poisonTurns)
        b.playerPoison = jsRound((b.enemyPower + 12) * 0.035 * v.poison);
    b.playerPoisonTurns = v.poisonTurns;
    b.enemyCd = v.cooldowns;
    if (b.poisonTurns > 0) {
        const double quantum = (BATTLE_POWER[clampInt(playerStage, 0, 12)] + 12) * 0.035;
        if (v.playerPoison < jsRound(quantum * b.poison))
            b.poison = clampInt(jsFloor(v.playerPoison / quantum), 0, b.poison);
        if (b.poison == 0) b.poisonTurns = 0;
    }
}
#undef ENEMY_FIELDS

static void battleEnemyDraw(Game& g, Battle& v, int count) {
    while (count-- > 0 && v.hand.size() < 8) {
        if (v.draw.empty()) {
            if (v.discard.empty()) break;
            v.draw = v.discard; v.discard.clear();
            battleShuffle(g, v.draw); ++v.shuffles;
        }
        v.hand.push_back(v.draw.back()); v.draw.pop_back();
    }
}

static string enemyRemoveTarget(const Battle& v, const SpellDef& spell) {
    if (!battleNeedsTarget(spell)) return "";
    for (const auto& wanted : {string("defend"), string("strike")})
        for (const auto* pile : {&v.hand, &v.draw, &v.discard})
            for (const auto& uid : *pile) {
                const Card* card = findCard(v, uid);
                if (card && card->spell == wanted) return wanted;
            }
    return "";
}

static string battleEnemyPlay(Game* rng, const Player& p, Battle& v,
                              const string& uid, double reduction, double counter) {
    auto held = std::find(v.hand.begin(), v.hand.end(), uid);
    if (held == v.hand.end()) return "";
    const Card* card = findCard(v, uid);
    const SpellDef* sp = card ? battleSpell(card->spell) : nullptr;
    if (!sp || sp->stage > p.stage || battleCooldown(v, sp->id) > 0 ||
        v.mana < battleCost(p, *sp)) return "";
    const string target = enemyRemoveTarget(v, *sp);
    if (battleNeedsTarget(*sp) && target.empty()) return "";
    v.hand.erase(held);
    if (!target.empty()) {
        auto removed = [&](const string& id) {
            const Card* c = findCard(v, id); return c && c->spell == target;
        };
        for (auto* pile : {&v.hand, &v.draw, &v.discard}) {
            for (const auto& id : *pile) if (removed(id)) v.exhausted.push_back(id);
            pile->erase(std::remove_if(pile->begin(), pile->end(), removed), pile->end());
        }
    }
    v.pendingDraw = 0;
    const string text = battleEffect(p, v, *sp, reduction, counter);
    if (rng && v.pendingDraw > 0) battleEnemyDraw(*rng, v, v.pendingDraw);
    else {
        // 未洗牌的抽牌堆顺序确定；连招可以衔接这些牌，不预测重新洗牌的结果。
        int count = v.pendingDraw;
        while (count-- > 0 && v.hand.size() < 8 && !v.draw.empty()) {
            v.hand.push_back(v.draw.back()); v.draw.pop_back();
        }
    }
    v.pendingDraw = 0;
    v.discard.push_back(uid); ++v.played;
    return text;
}

static int enemyCardPriority(const Battle& v, const SpellDef& sp) {
    const string& k = sp.kind;
    if (k == "heal") return v.hp < v.max * 0.75 ? 95 : -1;
    if (k == "shield" || k == "guard") return v.shield < v.max * 0.15 ? 62 : -1;
    if (k == "mana" || k == "burst" || k == "float") return v.mana <= 2 ? 94 : -1;
    if (k == "draw" || k == "redraw" || k == "exhaust") return v.hand.size() <= 3 ? 78 : 35;
    if (k == "foresee") return 32;
    if (k == "store") return v.mana >= 2 ? 20 : -1;
    if (k == "flow") return v.hp > v.max * 0.6 ? 30 : -1;
    if (k == "empower") return v.buff < 0.20 ? 80 : -1;
    if (k == "ward") return v.reduction < 0.10 ? 65 : -1;
    if (k == "water") return v.water < 4 ? 86 : 69;
    if (k == "wave") return v.water > 0 ? 85 : 45;
    if (k == "poison") return v.poison < 4 ? 86 : 70;
    if (k == "poisonBurst") return v.poison > 0 ? 85 : 45;
    if (k == "fire") return v.burn == 0 ? 86 : 77;
    if (k == "fireBurst") return v.burn > 0 ? 85 : 45;
    if (k == "charge" || k == "soul") return 84;
    if (k == "heavy" || k == "bodyBurst") return v.charge > 0 ? 85 : 68;
    if (k == "soulBurst") return v.soul > 0 ? 85 : 45;
    if (k == "sword" || k == "human") return 79 + std::min(3, v.swordSequence);
    return sp.id == "strike" ? 50 : 72;
}

static void battleEnemyInit(Game& g, const EnemySpec& spec, bool opening) {
    Battle& b = *g.player.arts.battle;
    Npc npc;
    npc.name = spec.name; npc.stage = clampInt(spec.stage, 0, 12);
    npc.sect = spec.sect; npc.gender = spec.gender;
    npc.dao = spec.style.empty() ? "体" : spec.style;
    npc.beast = spec.beast || (spec.id.empty() && spec.sect.empty() &&
        (spec.kind == "hunt" || spec.kind == "tide"));
    bool actualNpc = false;
    for (const auto& actual : g.npcs) {
        if (actual.id != spec.id) continue;
        npc = actual; actualNpc = true; break;
    }
    npc.stage = clampInt(npc.stage, 0, 12);
    b.enemyStage = npc.stage; b.enemySect = npc.sect; b.enemyGender = npc.gender;
    b.enemyPower = BATTLE_POWER[npc.stage];
    b.enemyBeast = npc.beast; b.enemyDao = npc.dao;
    EnemyCombat e;
    e.stage = npc.stage; e.sect = npc.sect; e.dao = npc.dao; e.body = npc.body;
    const Arts loadout = artNpcLoadout(npc);
    e.main = loadout.main; e.aux = loadout.aux;
    e.manuals = loadout.manuals; e.nodes = loadout.nodes; e.mind = loadout.mind;
    b.enemyCards = e;
    Player foe = battleEnemyPlayer(g, b);
    foe.arts = loadout;
    b.enemySpeed = spec.speed >= 0 ? spec.speed : battleSpeed(foe);
    b.enemyCards.speed = b.enemySpeed;
    b.playerSpeed = std::max(1, battleSpeed(g.player) + b.speed);
    b.playerFirst = b.playerSpeed >= b.enemySpeed;
    b.enemySpells.clear(); b.enemyCd.clear();
    if (npc.beast) return;
    b.enemyCards.enabled = true;
    const ArtMods mods = artModsOf(loadout);
    if (opening) {
        const double baseMax = actualNpc ? (110 + b.enemyPower * 2.4) * 1.45 : b.enemyMax;
        b.enemyMax = jsRound(baseMax * mods.hp * (1 + cultivate::bodyHpBonus(npc.body)));
        b.enemyHp = actualNpc ? std::max(1, jsRound(b.enemyMax * clampInt(npc.health, 0, 100) / 100.0))
                                 : b.enemyMax;
    }
    Battle v = battleEnemyView(b, g.player.stage);
    v.maxMana = battleEnergy(foe);
    v.mana = std::min(battleManaCap(foe), v.maxMana + static_cast<int>(std::ceil(mods.openingMana / 2.0)) +
                                         (artHas(foe.arts, "气", 2) ? 1 : 0));
    v.shield = jsRound(v.max * std::min(0.30, mods.openingShield + mods.roundShield +
                                                (artHas(foe.arts, "阵", 1) ? 0.12 : 0.0)));
    v.swordQi = std::min(8, jsFloor(mods.openingQi) + (artHas(foe.arts, "剑", 2) ? 2 : 0));
    if (!opening && b.playerPoisonTurns > 0) {
        v.poisonTurns = b.playerPoisonTurns;
        v.poison = clampInt(jsRound(b.playerPoison /
                                   ((b.enemyPower + 12) * 0.035)), 1, BATTLE_POISON_CAP);
    }
    for (const auto& id : loadout.spells) {
        const SpellDef* sp = battleSpell(id);
        if (!sp || sp->stage > foe.stage || id == "strike" || id == "defend" ||
            std::find(b.enemySpells.begin(), b.enemySpells.end(), id) != b.enemySpells.end()) continue;
        b.enemySpells.push_back(id);
        for (int copy = 0; copy < 2; ++copy)
            v.deck.push_back({"enemy-card-" + std::to_string(v.deck.size()), id});
    }
    for (int i = 0; i < battleBasicCopies(); ++i)
        for (const auto& id : {string("strike"), string("defend")})
            v.deck.push_back({"enemy-card-" + std::to_string(v.deck.size()), id});
    for (const auto& card : v.deck) v.draw.push_back(card.uid);
    battleShuffle(g, v.draw); battleEnemyDraw(g, v, 5);
    battleSaveEnemyView(b, v, g.player.stage);
}

EnemyPreview battleEnemyPreview(const Game& g) {
    EnemyPreview out;
    if (!g.player.arts.battle) return out;
    const Battle& b = *g.player.arts.battle;
    Battle projected = b;
    battleDiscard(g.player, projected, static_cast<int>(projected.hand.size()));
    battleTickStatuses(g.player, projected, false);
    if (projected.enemyHp <= 0 || projected.hp <= 0) {
        out.damage = std::max(0, b.hp - projected.hp);
        out.description = projected.enemyHp <= 0
            ? "持续伤害将在敌方行动前击败对手。" : "毒与灼烧将在敌方行动前耗尽气血。";
        return out;
    }
    if (!b.enemyCards.enabled) {
        out.description = b.intentName;
        out.damage = std::max(0, b.hp - projected.hp);
        if (b.intentKind == "guard") out.shield = jsRound(b.enemyMax * 0.1);
        else {
            int blocked = 0;
            out.damage += battleIncomingLoss(g.player, projected, blocked);
            if (b.intentKind == "heal") out.heal = std::min(projected.enemyMax - projected.enemyHp, jsRound(b.enemyMax * 0.035));
        }
        if (b.hp > projected.hp)
            out.description = "毒与灼烧：伤害" + std::to_string(b.hp - projected.hp) + "。 " + out.description;
        return out;
    }
    const Player foe = battleEnemyPlayer(g, b);
    const ArtMods playerMods = talentMods(g.player);
    const ArtMods mods = artModsOf(foe.arts);
    Battle v = battleEnemyView(projected, g.player.stage);
    v.shield = jsRound(v.max * std::min(0.30, mods.roundShield)); v.guard = 0;
    const int before = v.hp;
    for (const auto& uid : b.enemyCards.planned) {
        const Card* card = findCard(v, uid);
        const SpellDef* sp = card ? battleSpell(card->spell) : nullptr;
        const string text = battleEnemyPlay(nullptr, foe, v, uid,
                                            battleReduction(g.player, b), playerMods.bodyCounter);
        if (text.empty() || !sp) continue;
        out.spells.push_back(sp->id);
        if (!out.description.empty()) out.description += " ";
        out.description += text;
        if (v.hp <= 0 || v.enemyHp <= 0) break;
    }
    if (v.hp > 0 && v.enemyHp > 0) {
        battleDiscard(foe, v, static_cast<int>(v.hand.size()));
        if (!v.attacked && mods.quietHeal > 0)
            v.hp = std::min(v.max, v.hp + jsRound(v.max * std::min(0.05, mods.quietHeal)));
        if (artHas(foe.arts, "木", 2))
            v.hp = std::min(v.max, v.hp + jsRound(v.max * 0.01));
    }
    out.damage = std::max(0, b.hp - v.enemyHp);
    out.shield = v.shield;
    out.heal = std::max(0, v.hp - before);
    const int ongoing = std::max(0, b.hp - projected.hp);
    if (ongoing > 0)
        out.description = "毒与灼烧：伤害" + std::to_string(ongoing) + "。 " + out.description;
    if (out.description.empty()) out.description = "灵力不足或手牌不合，调息等待。";
    return out;
}

static void battleEnemyPlan(Game& g) {
    Battle& b = *g.player.arts.battle;
    EnemyCombat& e = b.enemyCards;
    const Player foe = battleEnemyPlayer(g, b);
    const ArtMods playerMods = talentMods(g.player);
    const ArtMods mods = artModsOf(foe.arts);
    Battle v = battleEnemyView(b, g.player.stage);
    v.shield = jsRound(v.max * std::min(0.30, mods.roundShield)); v.guard = 0;
    e.planned.clear();
    const int limit = e.sect.empty() ? 2 + e.stage / 6 : std::min(8, 3 + e.stage / 3);
    for (int played = 0; played < limit; ++played) {
        string next;
        int priority = -1;
        for (const auto& uid : v.hand) {
            const Card* card = findCard(v, uid);
            const SpellDef* sp = card ? battleSpell(card->spell) : nullptr;
            if (!sp || sp->stage > foe.stage || battleCooldown(v, sp->id) > 0 ||
                v.mana < battleCost(foe, *sp)) continue;
            if (battleNeedsTarget(*sp) && enemyRemoveTarget(v, *sp).empty()) continue;
            const int candidate = enemyCardPriority(v, *sp);
            if (candidate > priority) { next = uid; priority = candidate; }
        }
        if (next.empty()) break;
        if (battleEnemyPlay(nullptr, foe, v, next, battleReduction(g.player, b), playerMods.bodyCounter).empty()) break;
        e.planned.push_back(next);
        if (v.hp <= 0 || v.enemyHp <= 0) break;
    }
    const EnemyPreview preview = battleEnemyPreview(g);
    b.intentKind = "cards"; b.intentName.clear(); b.intentSpell.clear();
    for (const auto& id : preview.spells) {
        if (!b.intentName.empty()) b.intentName += " → ";
        b.intentName += battleSpell(id)->name;
        if (b.intentSpell.empty()) b.intentSpell = id;
    }
    if (b.intentName.empty()) b.intentName = "调息";
    b.intentScale = 0; b.intentRaw = preview.damage;
}

static void battleEnemyAct(Game& g) {
    Battle& b = *g.player.arts.battle;
    if (!b.enemyCards.enabled) return;
    const Player foe = battleEnemyPlayer(g, b);
    const ArtMods mods = artModsOf(foe.arts);
    const ArtMods playerMods = talentMods(g.player);
    Battle v = battleEnemyView(b, g.player.stage);
    v.shield = jsRound(v.max * std::min(0.30, mods.roundShield)); v.guard = 0;
    const vector<string> planned = b.enemyCards.planned;
    for (const auto& uid : planned) {
        const int hp = v.enemyHp, shield = v.enemyShield;
        const string text = battleEnemyPlay(&g, foe, v, uid,
                                            battleReduction(g.player, b), playerMods.bodyCounter);
        if (text.empty()) continue;
        b.log.push_back("对方施放" + text);
        if (v.enemyHp < hp)
            b.log.push_back("你受到" + std::to_string(hp - v.enemyHp) + "伤害，护盾格挡" +
                            std::to_string(std::max(0, shield - v.enemyShield)) + "。");
        if (v.hp <= 0 || v.enemyHp <= 0) break;
    }
    if (v.hp <= 0 || v.enemyHp <= 0) {
        battleSaveEnemyView(b, v, g.player.stage);
        return;
    }
    battleDiscard(foe, v, static_cast<int>(v.hand.size()));
    v.discard.insert(v.discard.end(), v.hand.begin(), v.hand.end()); v.hand.clear();
    for (auto& [id, cd] : v.cooldowns) cd = std::max(0, cd - 1);
    v.mana = battleNextMana(foe, v); v.carryMana = 0;
    if (v.lock > 0) --v.lock;
    if (!v.attacked && mods.quietHeal > 0)
        v.hp = std::min(v.max, v.hp + jsRound(v.max * std::min(0.05, mods.quietHeal)));
    if (artHas(foe.arts, "木", 2))
        v.hp = std::min(v.max, v.hp + jsRound(v.max * 0.01));
    const int draws = 5 + v.drawNext; v.drawNext = 0;
    v.played = 0; v.attacked = false; v.swordSequence = 0; v.comboDraws = 0;
    battleEnemyDraw(g, v, draws);
    battleSaveEnemyView(b, v, g.player.stage);
}

// ------------------------------------------------------------
// 出牌预览（对应 battlePreview）
// ------------------------------------------------------------
static Preview battlePreviewSpell(const Player& p, const Battle& b, const SpellDef& spell) {
    Preview out;
    const string& id = spell.id;
    // 在副本上试算，真实状态不受影响
    Battle copy = b;
    for (auto it = copy.hand.begin(); it != copy.hand.end(); ++it) {
        const Card* card = findCard(copy, *it);
        if (card && card->spell == id) { copy.hand.erase(it); break; }
    }
    copy.pendingDraw = 0;   // 试算不抽牌
    const ArtMods targetMods = b.enemyCards.enabled ? battleEnemyMods(b) : ArtMods{};
    const double reduction = b.enemyCards.enabled
        ? targetMods.reduction + b.enemyCards.reduction + b.enemyCards.guard : 0;
    const string text = battleEffect(p, copy, spell, reduction, targetMods.bodyCounter);

    out.valid = true;
    out.spellName = spell.name;
    out.cost = battleCost(p, spell);
    out.damage = std::max(0, b.enemyHp - copy.enemyHp);
    out.shield = copy.shield - b.shield;
    out.heal = copy.hp - b.hp;
    out.energy = copy.mana - b.mana;
    out.qi = copy.swordQi - b.swordQi;
    out.manaGain = std::max(0, out.energy + out.cost);
    out.draw = copy.pendingDraw;
    out.drawNext = copy.drawNext - b.drawNext;
    out.hpMaxChange = copy.max - b.max;
    out.manaRegenDelta = (copy.manaBonus - copy.manaPenalty) - (b.manaBonus - b.manaPenalty);
    out.storedMana = copy.carryMana;
    out.damageBonusPct = (copy.buff - b.buff) * 100;
    out.reductionPct = (copy.reduction - b.reduction) * 100;
    out.guardPct = (copy.guard - b.guard) * 100;
    out.healPotential = std::max(0, out.heal);
    if (spell.kind == "heal") {
        Battle wounded = b;
        wounded.hp = 1;
        battleEffect(p, wounded, spell, reduction, targetMods.bodyCounter);
        out.healPotential = wounded.hp - 1;
    }
    const auto status = [&](const char* key, const char* name, const char* target,
                            int before, int total, int turns, int cap, int tick = 0) {
        if (total != before || turns > 0)
            out.statuses.push_back({key, name, target, total - before, total, turns, cap, tick});
    };
    status("qi", "剑气", "self", b.swordQi, copy.swordQi, 0, 8);
    status("charge", "蓄势", "self", b.charge, copy.charge, 0, 4);
    status("water", "水势", "self", b.water, copy.water, 0, 6);
    status("soul", "震荡", "self", b.soul, copy.soul, 0, 6);
    const int base = BATTLE_POWER[clampInt(p.stage, 0, 12)];
    status("poison", "毒", "enemy", b.poison, copy.poison, copy.poisonTurns,
           BATTLE_POISON_CAP, copy.poisonTurns > 0 ? jsRound((base + 12) * 0.035 * copy.poison) : 0);
    status("burn", "灼烧", "enemy", b.burn, copy.burn, copy.burnTurns,
           BATTLE_BURN_CAP, copy.burnTurns > 0 ? copy.burn : 0);
    out.text = text;
    return out;
}

Preview battlePreview(const Player& p, const Battle& b, const string& id) {
    optional<ArtifactAbility> ability;
    const SpellDef* spell = battleResolveSpell(p, id, ability);
    if (!spell || p.stage < spell->stage) return {};
    return battlePreviewSpell(p, b, *spell);
}

Preview battleReferencePreview(const Player& p, const SpellDef& spell) {
    Battle b = battleReferenceState(p);
    // 示例出牌后其余四张手牌；不假定真实抽到的卡牌类型，不触发 RNG。
    for (int i = 0; i < 4; ++i) b.hand.push_back("reference-" + std::to_string(i));
    Preview out = battlePreviewSpell(p, b, spell);
    // 实战 preview.text 保持与 battleEffect 日志一致。目录额外说明取自
    // 同一试算的结构化结果，避免满血治疗量或持续伤害在场外不可见。
    if (spell.kind == "heal")
        out.text += "单次治疗量" + std::to_string(out.healPotential) + "，当前实际可回复" + std::to_string(std::max(0, out.heal)) + "。";
    if (out.guardPct > 0) out.text += "本回合额外减伤+" + std::to_string(jsRound(out.guardPct)) + "% 。";
    if (spell.kind == "poison")
        for (const auto& status : out.statuses) if (status.id == "poison")
            out.text += "当前毒每回合造成" + std::to_string(status.perTurn) + "伤害。";
    return out;
}

string battlePreviewConditions(const Player& p, const SpellDef& sp) {
    const ArtMods m = talentMods(p);
    string text;
    if (sp.kind == "sword" || sp.kind == "human")
        text = "每层剑气伤害+7%，连续剑招每次额外+" + std::to_string(jsRound(m.swordCombo * 100)) + "%（最多50%）；每第三剑回复" + std::to_string(jsFloor(m.swordMana)) + "灵力。";
    if (sp.kind == "heavy")
        text = "每层蓄势伤害+12%，消耗所有蓄势；额外伤害为当前护盾×" + std::to_string(jsRound(m.shieldStrike * 100)) + "% 。";
    if (sp.kind == "wave")
        text = "每层水势伤害+" + std::to_string(jsRound((0.15 + std::min(0.1, m.water * 0.025)) * 100)) + "%；保留" + std::to_string(jsRound(std::min(0.50, m.waterKeep) * 100)) + "%水势（向下取整），返灵取决于消耗水势。";
    if (sp.kind == "poisonBurst")
        text = "毒爆追加伤害=(当前基础战力" + std::to_string(BATTLE_POWER[clampInt(p.stage, 0, 12)]) + "+12)×18%×毒层；所有毒层合计后四舍五入，消耗全部毒层。";
    if (sp.kind == "fireBurst")
        text = "引爆追加伤害＝现有灼烧每回合伤害×剩余回合×" + std::to_string(jsRound((1 + m.burnExplosion) * 100)) + "%；消耗全部灼烧。";
    if (sp.kind == "soulBurst") text = "每层震荡伤害+15%，消耗全部震荡。";
    if (sp.kind == "bodyBurst") text = "出牌前灵力至少3时伤害+15%，每层蓄势伤害+10%，消耗全部蓄势。";
    if (sp.kind == "human") text += "消耗全部剑气与当前8%气血，气血最低保留1。";
    if (sp.kind == "redraw") text = "数字以出牌后剩余4张手牌为示例；实际弃置和抽牌数随手牌变化。";
    if (sp.kind == "draw" || sp.kind == "foresee" || sp.kind == "redraw" || sp.kind == "exhaust")
        text += "抽牌还受手牌8张上限与本场牌组剩余数量限制。";
    if (sp.kind == "store") text = "封存量随扣费后剩余灵力变化，本回合灵力清零。";
    if (sp.kind == "heal") text = "单次治疗量受气血上限、治疗功法和悟道影响；实际回复不超过缺失气血。";
    if (sp.dao == "金" || sp.dao == "水" || sp.dao == "木" || sp.dao == "火" || sp.dao == "土")
        text += "符合五行相生时直伤倍率为" + std::to_string(jsRound((1.15 + std::min(0.15, m.combo)) * 100)) + "%；此处按未触发相生显示。";
    return text;
}

// ------------------------------------------------------------
// 敌方来伤（对应 battleIncoming）
// ------------------------------------------------------------
static int battleIncomingLoss(const Player& p, const Battle& b, int& blocked) {
    const int damage = jsRound(b.intentRaw * (1 - battleReduction(p, b)));
    const int pierce = (b.intentKind == "pierce") ? jsRound(damage * 0.4) : 0;
    blocked = std::min(b.shield, damage - pierce);
    return damage - blocked;
}

// ------------------------------------------------------------
// 结束回合（对应 battleEndTurn）
// ------------------------------------------------------------
static void battleEndTurn(Game& g) {
    Player& p = g.player;
    Battle& b = *p.arts.battle;
    const ArtMods m = talentMods(p);

    // 1) 弃掉所有手牌
    battleDiscard(p, b, static_cast<int>(b.hand.size()));
    b.discard.insert(b.discard.end(), b.hand.begin(), b.hand.end());
    b.hand.clear();

    // 2) 毒与灼烧按完整回合结算
    battleTickStatuses(p, b, true);
    if (b.enemyHp <= 0) {
        battleFinish(g, true);
        return;
    }

    if (b.hp <= 0) {
        battleFinish(g, false);
        return;
    }

    // 3) 敌方按已展示的意图行动
    if (b.enemyCards.enabled) {
        battleEnemyAct(g);
    } else {
      b.enemyShield = 0;
      if (b.intentKind == "guard") {
        b.enemyShield = jsRound(b.enemyMax * 0.1);
        b.log.push_back("对方凝盾" + std::to_string(b.enemyShield) +
                        "，本回合不攻击；护盾保留至下次敌方行动。");
    } else {
        if (b.intentKind == "heal")
            b.enemyHp = std::min(b.enemyMax,
                                 b.enemyHp + jsRound(b.enemyMax * 0.035));
        int blocked = 0;
        const int loss = battleIncomingLoss(p, b, blocked);
        b.hp -= loss;
        const int counter = jsRound(blocked * m.bodyCounter);
        if (counter > 0) {
            b.enemyHp -= counter;
            b.log.push_back("盾挡反震：伤害" + std::to_string(counter) + "。");
        }
        // 说清楚对方用的是什么神通 —— 不然玩家只看到掉血，看不出差别
        const string via = b.intentSpell.empty()
                               ? string("")
                               : string("（") + b.intentSpell + "）";
        b.log.push_back("对方" + b.intentName + via + "：气血−" +
                        std::to_string(loss) + "（盾挡" +
                        std::to_string(blocked) + "）。");

        // 神通的附加效果
        if (b.intentKind == "drain") {
            const int back = jsRound(loss * 0.35);
            b.enemyHp = std::min(b.enemyMax, b.enemyHp + back);
            b.log.push_back("对方汲取生机，回复 " + std::to_string(back) + "。");
        } else if (b.intentKind == "poison") {
            const int tick = jsRound((b.enemyPower + 12) * 0.05);
            b.playerPoison = std::max(b.playerPoison, tick);
            b.playerPoisonTurns = 3;
            b.log.push_back("你中了毒，每回合将损失 " + std::to_string(tick) +
                            "（3 回合）。");
        } else if (b.intentKind == "weaken") {
            b.enemyWeaken += 1;
            b.log.push_back("对方扰乱了你的灵力，下一手威力会降低。");
        }
      }
    }

    // 4) 护盾清零、回合推进
    b.shield = 0;
    b.turn++;
    if (b.enemyHp <= 0 && b.hp > 0) {
        battleFinish(g, true);
        return;
    }
    if (b.hp <= 0 || b.turn >= 24) {
        battleFinish(g, false);
        return;
    }

    // 5) 本回合没直接攻击时的静养回复
    if (!b.attacked && m.quietHeal)
        b.hp = std::min(b.max, b.hp + jsRound(b.max * std::min(0.05, m.quietHeal)));
    if (artHas(p.arts, "木", 2))
        b.hp = std::min(b.max, b.hp + jsRound(b.max * 0.01));
    if (hasTalent(p, "dragonBlood")) {
        const int recovered = std::min(b.max - b.hp, jsRound(b.max * 0.03));
        b.hp += recovered;
        if (recovered > 0) b.log.push_back("龙族血脉：恢复" + std::to_string(recovered) + "气血。");
    }

    // 6) 冷却递减（玩家与对方各自算）
    for (auto& [k, v] : b.cooldowns) v = std::max(0, v - 1);
    if (!b.enemyCards.enabled)
        for (auto& [k, v] : b.enemyCd) v = std::max(0, v - 1);

    // 旧在途战斗保留已展示的那次行动，随后为真实修士补建牌组。
    if (!b.enemyCards.enabled && !b.enemyBeast) {
        EnemySpec spec;
        spec.name = b.enemy; spec.id = b.npcId; spec.kind = b.kind;
        spec.style = b.style; spec.sect = b.enemySect; spec.gender = b.enemyGender;
        spec.stage = b.enemyStage;
        if (spec.stage == 0 && b.enemyPower > BATTLE_POWER[0])
            while (spec.stage < 12 && BATTLE_POWER[spec.stage] < b.enemyPower) ++spec.stage;
        battleEnemyInit(g, spec, false);
    }

    // 7) 回满灵力（悬灵术锁定时少 1）。
    //    灵力经营：基数 + 聚气加成 − 夺灵代价，再加上「蓄灵」结转的部分。
    //    容量至少12，功法容量更高时沿用该容量。
    const int baseMana = std::max(
        1, b.maxMana + b.manaBonus - b.manaPenalty - ((b.lock > 0) ? 1 : 0));
    const int carried = b.carryMana;
    const int quietMana = b.played <= 2 ? jsFloor(m.quietMana) : 0;
    b.mana = battleNextMana(p, b);
    b.carryMana = 0;                       // 结转只用一次
    if (carried > 0 || quietMana > 0)
        b.log.push_back("灵力回复：基础" + std::to_string(baseMana) + " + 封存" +
                        std::to_string(carried) + " + 静息" + std::to_string(quietMana) +
                        "，容量限制后共" + std::to_string(b.mana) + "。");
    if (b.lock > 0) b.lock--;

    b.guard = 0.0;
    b.attacked = false;
    b.played = 0;
    b.swordSequence = 0;
    b.comboDraws = 0;
    // 回合护盾功法在下一回合重新产生
    b.shield = std::min(jsRound(b.max * 0.3), jsRound(b.max * m.roundShield));

    // 8) 抽牌：基础 5 张 + 上回合「预谋」攒下的额外张数
    const int extra = b.drawNext;
    b.drawNext = 0;
    battleDraw(g, 5 + extra + dungeonRelicValue(g, true));
    battlePlan(g);

    if (b.log.size() > 12)
        b.log.erase(b.log.begin(), b.log.begin() + (b.log.size() - 12));
}

// ------------------------------------------------------------
// 行动入口（对应 battleTurn）
// ------------------------------------------------------------
TurnResult battleTurn(Game& g, const string& id, const string& target) {
    TurnResult out;
    Player& p = g.player;
    if (!p.arts.battle.has_value()) {
        out.ok = false;
        out.error = "当前没有斗法。";
        return out;
    }
    Battle& b = *p.arts.battle;

    auto fail = [&](const string& msg) {
        TurnResult r;
        r.ok = false;
        r.error = msg;
        return r;
    };

    // ---- 撤离 ----
    if (id == "escape") {
        if (b.lethal || !b.dungeonSite.empty()) {
            const double chance = battleEscapeChance(p, b);
            if (randomOf(g) >= chance) {
                b.log.push_back("撤离未成，对方按当前意图追击。");
                battleEndTurn(g);
                return out;
            }
        }
        battleFinish(g, false, true);
        return out;
    }

    // ---- 结束回合 ----
    if (id == "end") {
        battleEndTurn(g);
        return out;
    }

    // ---- 找手牌或已装备自炼法宝的常驻主动能力 ----
    // id 可能是手牌 uid，也可能是神通 id（界面简化调用）
    const bool artifact = id.rfind("artifact:", 0) == 0;
    string uid;
    if (!artifact && std::find(b.hand.begin(), b.hand.end(), id) != b.hand.end()) {
        uid = id;
    } else if (!artifact) {
        for (const auto& h : b.hand) {
            const Card* c = findCard(b, h);
            if (c && c->spell == id) { uid = h; break; }
        }
    }
    const Card* card = uid.empty() ? nullptr : findCard(b, uid);
    if (!artifact && !card) return fail("这张神通卡不在手牌中。");

    optional<ArtifactAbility> ability;
    const SpellDef* spell = battleResolveSpell(p, artifact ? id : card->spell, ability);
    if (!spell) return fail(artifact ? "这件法宝未装备、品阶不足或没有主动能力。" : "这张神通卡不在手牌中。");
    if (p.stage < spell->stage) return fail("境界不足，无法施放这门神通。");

    string targetSpell;
    if (battleNeedsTarget(*spell)) {
        if (target.empty()) return fail("请选择本场要移除的神通。");
        const Card* targetCard = findCard(b, target);
        targetSpell = targetCard ? targetCard->spell : target;
        if (targetSpell == spell->id) return fail("不能移除正在施放的这门神通。");
        auto contains = [&](const vector<string>& pile) {
            return std::any_of(pile.begin(), pile.end(), [&](const string& cardUid) {
                const Card* candidate = findCard(b, cardUid);
                return candidate && candidate->spell == targetSpell;
            });
        };
        if (!battleSpell(targetSpell) ||
            (!contains(b.hand) && !contains(b.draw) && !contains(b.discard)))
            return fail("这门神通已移除或不在本场牌组中。");
    }

    auto cd = b.cooldowns.find(spell->id);
    if (cd != b.cooldowns.end() && cd->second > 0)
        return fail("神通冷却中，需等待回合结束。");
    if (b.mana < battleCost(p, *spell))
        return fail("灵力不足，请选择其他手牌或结束回合。");
    if (b.played >= 24) return fail("本回合已施术24次，请结束回合。");

    // ---- 出牌 ----
    const int qiBefore = b.swordQi;
    // 先移出手牌，抽牌时释放手牌位置；施放卡直到抽牌完成才进入弃牌堆。
    if (!artifact) b.hand.erase(std::find(b.hand.begin(), b.hand.end(), uid));
    if (!targetSpell.empty()) {
        auto isTarget = [&](const string& cardUid) {
            const Card* candidate = findCard(b, cardUid);
            return candidate && candidate->spell == targetSpell;
        };
        for (auto* pile : {&b.hand, &b.draw, &b.discard})
            pile->erase(std::remove_if(pile->begin(), pile->end(), isTarget), pile->end());
        for (const auto& candidate : b.deck)
            if (candidate.spell == targetSpell &&
                std::find(b.exhausted.begin(), b.exhausted.end(), candidate.uid) == b.exhausted.end())
                b.exhausted.push_back(candidate.uid);
    }
    b.pendingDraw = 0;
    const ArtMods targetMods = b.enemyCards.enabled ? battleEnemyMods(b) : ArtMods{};
    const double reduction = b.enemyCards.enabled
        ? targetMods.reduction + b.enemyCards.reduction + b.enemyCards.guard : 0;
    string text = battleEffect(p, b, *spell, reduction, targetMods.bodyCounter);
    if (!targetSpell.empty()) text += "移除目标：" + battleSpell(targetSpell)->name + "。";

    // 抽牌类神通：这里才真正抽（battleEffect 也可能被试算调用，没有 Game）
    if (b.pendingDraw > 0) {
        const int n = b.pendingDraw;
        b.pendingDraw = 0;
        battleDraw(g, n);
    }

    // 施放卡进入弃牌堆；它不会在自己的抽牌效果中被重新抽到。
    if (!artifact) b.discard.push_back(uid);
    b.played++;

    string extra;
    if (spell->kind == "human") {
        extra = " 剑气" + std::to_string(qiBefore) + "（伤害+" +
                std::to_string(qiBefore * 7) + "%）→" +
                std::to_string(b.swordQi) +
                (spell->kind == "human" ? "，消耗全部剑气与当前8%气血" : "");
    }
    b.log.push_back(text + extra);
    if (b.log.size() > 12)
        b.log.erase(b.log.begin(), b.log.begin() + (b.log.size() - 12));

    if (b.hp <= 0) battleFinish(g, false);
    else if (b.enemyHp <= 0) battleFinish(g, true);
    return out;
}

vector<string> handSpells(const Battle& b) {
    vector<string> out;
    for (const auto& uid : b.hand) {
        const Card* c = findCard(b, uid);
        out.push_back(c ? c->spell : string());
    }
    return out;
}

} // namespace nz

