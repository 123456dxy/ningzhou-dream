// 宁州一梦 · C++ 版 — 神通卡牌斗法实现（对应 Demo 07 的 battle.mjs）
//
// Demo05 的「直接选招 + 立即反击」已被 Demo07 的卡牌循环取代，
// 所以本文件是重写而非修补。旧的逐回合数值对照测试也随之更新。
#include "core/battle.h"
#include "core/combat/battle_scaling.h"
#include "core/combat/elemental_sword.h"
#include "core/combat/fire_growth.h"
#include "core/combat/discard_rules.h"
#include "core/social/npc_consumables.h"
#include "core/content/game_ids.h"
#include "core/content/text_table.h"
#include "core/content/npc_combat_profiles.h"
#include "core/artifact.h"
#include "core/dungeon.h"
#include "core/checkpoint.h"
#include "core/talents.h"
#include "core/sect_trial.h"

#include <algorithm>
#include <sstream>

#include "core/adventure.h"
#include "core/arts.h"
#include "core/cultivate.h"
#include "core/conflict.h"
#include "core/economy.h"
#include "core/sim.h"
#include "core/society.h"
#include "core/tide.h"
#include "core/calendar.h"
#include "data/reference_tables.h"

namespace nz {
using content::DaoId;
using content::SectId;
using content::SpellKind;
using content::BattleKind;
using content::TextId;

// 用途：为真实修士建立合法构筑、牌堆和资源。
// 参与流程：普通开局及旧修士战斗补建。
// 使用方式：传游戏、敌人规格和是否初始化开局属性。
// 返回值：无；修改 battle.enemyCards、敌人属性、牌堆及 RNG。
static void battleEnemyInit(Game& g, const EnemySpec& spec, bool opening = true);
// 用途：按宗门与完整传承身份提供连招次数上限；流程：规划与只读界面说明。
// 使用方式：传当前游戏和战斗，身份沿用已登记的NPC模板；返回：上限整数，无状态或随机副作用。
int mfBattleEnemyActionLimit(const Game& GameState, const Battle& BattleState) {
    if (BattleState.shadowPlayer) return 24;
    const auto& EnemyState = BattleState.enemyCards;
    const int EnemyStage = clampInt(EnemyState.stage, 0, 12);
    if (EnemyState.sect.empty()) return 2 + EnemyStage / 6;
    bool HasCompleteProfile = false;
    if (!BattleState.npcId.empty()) {
        const auto Opponent = std::find_if(GameState.npcs.begin(), GameState.npcs.end(),
            [&](const Npc& Entry) { return Entry.id == BattleState.npcId; });
        HasCompleteProfile = Opponent != GameState.npcs.end() && content::isChampion(*Opponent);
    }
    const int BaseActionCount = HasCompleteProfile ? ChampionSectActionBase : CommonSectActionBase;
    return std::min(NpcSectActionLimit, BaseActionCount + EnemyStage / 3);
}

// 用途：从敌方已抽到的牌和可用法宝规划本轮连招。
// 参与流程：真实修士开局及每轮敌方计划。
// 使用方式：传游戏，以优先级和实际费用在副本上选择行动。
// 返回值：无；保存敌方计划及对玩家可见的意图。
static void battleEnemyPlan(Game& g);
// 用途：执行已经规划的敌方连招并推进敌方资源。
// 参与流程：玩家结束回合或敌方先手。
// 使用方式：传游戏，逐张执行并检查死亡，最后弃牌回灵和抽牌。
// 返回值：无；修改双方战斗状态、敌方下一轮资源及 RNG。
static void battleEnemyAct(Game& g);
// 用途：计算简化意图对玩家的真实气血损失。
// 参与流程：妖兽和旧敌方回合行动。
// 使用方式：传双方战斗状态，blocked 输出护盾吸收量。
// 返回值：减伤、穿盾和护盾之后的气血损失。
static int battleIncomingLoss(const Player& p, const Battle& b, int& blocked);

const int BATTLE_POWER[13] = {14, 24, 39, 65, 95, 140, 210, 310, 440,
                              660, 900, 1250, 1800};

// 用途：由毒层计算新施加或增长后的精确每跳伤害；流程：双方施毒、增长及预览。
// 使用方式：传统一伤害基底和毒层；返回：合计后取整的毒伤，不读写状态。
static int mfPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount) {
    return jsRound(DamageBasis * PoisonTickBasisCoefficient * std::max(0, PoisonLayerCount));
}

// 用途：解释缺少精确缓存的旧在途毒；流程：只读预演及下一动作的视角转换。
// 使用方式：传当场基底和保存毒层；返回：历史每跳伤害，避免更新系数后重算旧伤。
static int mfLegacyPoisonTickFromLayers(double DamageBasis, int PoisonLayerCount) {
    return jsRound(DamageBasis * LegacyPoisonTickBasisCoefficient * std::max(0, PoisonLayerCount));
}

// ------------------------------------------------------------
// 神通表（基础 12 种 + 拓展 28 种）
// ------------------------------------------------------------
// 用途：汇总基础及扩展神通定义。
// 参与流程：斗法牌组和目录查询的数据入口。
// 使用方式：读取一次初始化的静态列表。
// 返回值：神通定义常量引用。
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
        // 金道减1后再折半仍为2灵力，避免原始3在御金后成为1费重击；无御金的土修仍为原2费。
        add("goldslash", "金罡斩", "金", 4, "heavy", 0.95, 0);
        // 历史定义费用会折半；6对应实际3灵力，不能误填3变成实战2费。
        add("fireSiphon", "噬焰回生", "火", 6, "fireSiphon", 0, 3, 2);
        // 补牌仍支付真实费用与冷却；剑系补牌属于经营牌，应在连剑前使用。
        add("metalRedraw", "回锋换念", "金", 2, "redraw", 1.0, 3, 1);
        add("waterDraw", "回澜诀", "水", 3, "draw", 2.0, 3, 1);
        add("fireDraw", "照焰诀", "火", 2, "draw", 2.0, 3, 1);
        add("woodDraw", "探叶诀", "木", 2, "draw", 2.0, 3, 1);
        add("earthDraw", "静岳诀", "土", 2, "draw", 2.0, 3, 1);
        add("swordDraw", "观剑诀", "剑", 2, "draw", 2.0, 3, 1);
        add("earthSword", "土元剑", "土", 2, "sword", 0.65, 0);
        add("metalSword", "金元剑", "金", 2, "sword", 0.65, 0);
        add("waterSword", "水元剑", "水", 2, "sword", 0.35, 0);
        add("woodSword", "木元剑", "木", 3, "sword", 0.55, 0);
        add("fireSword", "火元剑", "火", 3, "sword", 0.70, 0);
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
            if (content::parseSpellKind(s.kind) == SpellKind::Heal && s.cooldown == 0) s.cooldown = 2;
        return v;
    }();
    return data;
}

// 用途：按 ID 查神通，守势使用内置定义。
// 参与流程：出牌解析、构筑与界面展示。
// 使用方式：传普通神通 ID，法宝由 battleResolveSpell 解析。
// 返回值：匹配定义指针；未知 ID 返回空。
const SpellDef* battleSpell(const string& id) {
    // 守势是内置卡，不在神通表里
    static const SpellDef defend{"defend", "普通防御", "气", 1, "shield", 0.12, 0,
                                 0};
    if (id == "defend") return &defend;
    for (const auto& s : allSpells())
        if (s.id == id) return &s;
    return nullptr;
}

// 用途：识别需要指定移除目标的神通。
// 参与流程：忘机咒出牌校验及界面选牌。
// 使用方式：传神通定义，检查其种类是否为 Exhaust。
// 返回值：需目标时为真。
bool battleNeedsTarget(const SpellDef& spell) { return content::parseSpellKind(spell.kind) == SpellKind::Exhaust; }

// 用途：统一解析普通神通和已装备法宝的施术定义。
// 参与流程：玩家/敌方出牌与预览。
// 使用方式：传角色、ID 和用于持有法宝结果的 optional。
// 返回值：有效定义指针；法宝指针依赖输出 optional 生命周期。
static const SpellDef* battleResolveSpell(const Player& p, const string& id,
                                         optional<ArtifactAbility>& ability) {
    if (id.rfind("artifact:", 0) == 0) {
        ability = equippedArtifactAbility(p, id);
        return ability ? &ability->spell : nullptr;
    }
    return battleSpell(id);
}

// 用途：按真实神通类型拼接规则说明。
// 参与流程：神通目录与手牌说明。
// 使用方式：传定义，参数代入集中内容文案。
// 返回值：含类型效果、限制和冷却的说明。
string battleSpellDescription(const SpellDef& spell) {
    const auto spellKind = content::parseSpellKind(spell.kind);
    if (!spellKind) return "";
    const auto plain = [](TextId id) { return string(content::text(id)); };
    const auto count = [](TextId id, int value) {
        return content::formatText(id, {{"count", std::to_string(value)}});
    };
    const auto percent = [](TextId id, int value) {
        return content::formatText(id, {{"percent", std::to_string(value)}});
    };
    string description;
    if (spell.id == "strike") description = plain(TextId::StrikeDescription);
    else if (spell.id == "defend") description = plain(TextId::DefendDescription);
    else switch (*spellKind) {
    case SpellKind::Draw: description = count(TextId::DrawDescription, std::max(1, jsRound(spell.scale))); break;
    case SpellKind::Foresee: description = count(TextId::ForeseeDescription, std::max(1, jsRound(spell.scale))); break;
    case SpellKind::Redraw: description = count(TextId::RedrawDescription, std::max(1, jsRound(spell.scale))); break;
    case SpellKind::Exhaust: description = count(TextId::ExhaustDescription, std::max(1, jsRound(spell.scale))); break;
    case SpellKind::Empower: description = percent(TextId::EmpowerDescription, jsRound(spell.scale * 100)); break;
    case SpellKind::Ward: description = percent(TextId::WardDescription, jsRound(spell.scale * 100)); break;
    case SpellKind::Mana: description = count(TextId::ManaDescription, std::max(1, jsRound(spell.scale / 2.0))); break;
    case SpellKind::Store: description = count(TextId::StoreDescription, std::max(1, jsRound(spell.scale * 4))); break;
    case SpellKind::Flow: description = plain(TextId::FlowDescription); break;
    case SpellKind::Burst: description = count(TextId::BurstDescription, std::max(2, jsRound(spell.scale / 1.5))); break;
    case SpellKind::Float: description = plain(TextId::FloatDescription); break;
    case SpellKind::Heal: description = plain(TextId::HealDescription); break;
    case SpellKind::Shield:
    case SpellKind::Guard:
        description = percent(TextId::ShieldDescription, jsRound(spell.scale * 100));
        if (*spellKind == SpellKind::Guard) description += plain(TextId::GuardExtra);
        if (spell.id == "array") description += plain(TextId::ArrayExtra);
        break;
    case SpellKind::Sword: description = plain(TextId::SwordDescription); break;
    case SpellKind::Human: description = plain(TextId::HumanDescription); break;
    case SpellKind::Heavy: description = plain(TextId::HeavyDescription); break;
    case SpellKind::Charge: description = plain(TextId::ChargeDescription); break;
    case SpellKind::Water: description = plain(TextId::WaterDescription); break;
    case SpellKind::Wave: description = plain(TextId::WaveDescription); break;
    case SpellKind::Poison:
        description = content::formatText(TextId::PoisonDescription,
            {{"count", std::to_string(spell.id == "corrode" || spell.id == "toxinRain" ? 3 : 2)},
             {"cap", std::to_string(BATTLE_POISON_CAP)}});
        break;
    case SpellKind::PoisonBurst: description = plain(TextId::PoisonBurstDescription); break;
    case SpellKind::Fire: description = plain(TextId::FireDescription); break;
    case SpellKind::FireBurst: description = plain(TextId::FireBurstDescription); break;
    case SpellKind::FireSiphon: description = plain(TextId::FireSiphonDescription); break;
    case SpellKind::Soul: description = plain(TextId::SoulDescription); break;
    case SpellKind::SoulBurst: description = content::formatText(TextId::SoulBurstDescription,
        {{"bonus_percent", std::to_string(jsRound(SoulBurstPerLayerBonus * 100))},
         {"pierce_percent", std::to_string(jsRound(SoulBurstPiercePerLayer * 100))},
         {"max_pierce_percent", std::to_string(jsRound(SoulBurstMaximumPierceFraction * 100))}}); break;
    case SpellKind::BodyBurst: description = plain(TextId::BodyBurstDescription); break;
    case SpellKind::Drain: description = plain(TextId::DrainDescription); break;
    case SpellKind::Steal: description = plain(TextId::StealDescription); break;
    case SpellKind::Attack: description = plain(TextId::AttackDescription); break;
    }
    if (spell.cooldown > 0) description += count(TextId::CooldownDescription, spell.cooldown);
    return description;
}

// ------------------------------------------------------------
// 基础公式
// ------------------------------------------------------------
// 用途：保留旧永久剑意加成入口并关闭其收益。
// 参与流程：旧接口兼容，战斗公式不再读取永久剑意。
// 使用方式：传角色只读状态。
// 返回值：恒为零，表示没有永久剑意额外加成。
double battleSwordBonus(const Player& p) {
    (void)p;
    return 0;
}

// 用途：提供普通攻击和守势的固定副本数。
// 参与流程：玩家和影身牌组构建。
// 使用方式：无参数，每种基础牌使用同一数量。
// 返回值：每种基础牌五张。
int battleBasicCopies() { return 5; }

// 用途：获取装备、功法与天赋汇总后的遁速。
// 参与流程：先手与撤离判定。
// 使用方式：传角色，委托统一遁速规则。
// 返回值：实际战斗遁速整数。
int battleSpeed(const Player& p) { return artEscapeSpeed(p); }

// 用途：按双方遁速、战力和临时准备计算撤离概率。
// 参与流程：撤离操作与战况预览。
// 使用方式：传当前角色和战斗状态。
// 返回值：非生死对局为一；生死对局限制在 0.10..0.95。
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

// 用途：按大境界计算额外神通携带槽数。
// 参与流程：配装与牌组合法性过滤。
// 使用方式：传角色，固定基础攻防不计入此数。
// 返回值：4 至 12 个额外槽位。
int battleSpellSlots(const Player& p) {
    return 4 + 2 * clampInt(p.stage / 3, 0, 4);
}

// 用途：汇总斗法灵力容量。
// 参与流程：即时回灵和回合恢复的容量限制。
// 使用方式：传角色，容量至少十二并叠加有效天赋收益。
// 返回值：本场可持有的最大灵力。
int battleManaCap(const Player& p) {
    return std::max(12, talentMods(p).mana) + talentManaBonus(p);
}

// 用途：计算当前角色每轮的基础灵力总量。
// 参与流程：开局及参考战斗初始化。
// 使用方式：传角色，计入大境界、容量收益和气道节点。
// 返回值：受真实容量限制的基础回灵总量。
int battleEnergy(const Player& p) {
    const int manaCap = talentMods(p).mana;
    const int v = 3 + clampInt(p.stage / 3, 0, 4) +
                  std::max(0, (manaCap - 8) / 4) +
                  (artHas(p.arts, "气", 1) ? 1 : 0);
    return std::min(battleManaCap(p), v);
}

// 用途：预测下一轮会恢复到的灵力总量。
// 参与流程：结束回合赋值及 nextMana 界面预览。
// 使用方式：传回合结束前状态，计入锁定、封存、静息和容量。
// 返回值：下一轮最终灵力值；不是在当前灵力上追加的增量。
int battleNextMana(const Player& p, const Battle& b) {
    const int quiet = b.played <= 2 ? jsFloor(talentMods(p).quietMana) : 0;
    const int base = std::max(1, b.maxMana + b.manaBonus - b.manaPenalty -
                                 (b.lock > 0 ? 1 : 0));
    return std::min(battleManaCap(p), base + b.carryMana + quiet);
}

// 用途：汇总被动、场内与守势的来伤减免。
// 参与流程：真实受伤与敌方伤害预览。
// 使用方式：传角色和对应视角的战斗状态。
// 返回值：夹在 -10% 至 35% 的总减伤比例。
double battleReduction(const Player& p, const Battle& b) {
    return clamp(talentMods(p).reduction + b.reduction + b.guard, -0.10, 0.35);
}

// 用途：计算含悟道与抽牌减费的实际施术费用。
// 参与流程：双方出牌校验与预览。
// 使用方式：传角色和神通，普通攻防固定一费。
// 返回值：不低于零的实际灵力消耗。
int battleCost(const Player& player, const SpellDef& spell) {
    // 守势与灵击固定 1 点
    if (spell.id == "defend" || spell.id == "strike") return 1;
    int base = spell.cost;
    const auto daoId = content::parseDaoId(spell.dao);
    const auto spellKind = content::parseSpellKind(spell.kind);
    // 气道二层对金/水两系减 1
    if (artHas(player.arts, spell.dao, 2) && (daoId == DaoId::Metal || daoId == DaoId::Water)) base -= 1;
    if (base < 0) base = 0;
    int cost = (base + 1) / 2;   // 折半向上取整
    if (spellKind == SpellKind::Draw || spellKind == SpellKind::Foresee ||
        spellKind == SpellKind::Redraw || spellKind == SpellKind::Exhaust)
        cost -= jsFloor(talentMods(player).drawCost);
    return std::max(0, cost);
}

// ------------------------------------------------------------
// 敌方意图
// ------------------------------------------------------------
// 用途：把旧存档神通映射为简化敌方意图。
// 参与流程：旧敌方行动兼容。
// 使用方式：传定义；新修士战斗执行完整卡牌效果。
// 返回值：简化种类、名称、倍率和神通 ID。
Intent spellToIntent(const SpellDef& spell) {
    Intent intent;
    intent.spell = spell.id;
    intent.name = spell.name;
    intent.scale = spell.scale;
    const auto spellKind = content::parseSpellKind(spell.kind);
    if (!spellKind) return {};
    // 神通种类很多，归到执行端认识的几种效果上
    if (spellKind == SpellKind::Guard || spellKind == SpellKind::Shield || spellKind == SpellKind::Float) {
        intent.kind = "guard";
        intent.scale = 0;
    } else if (spellKind == SpellKind::Heal || spellKind == SpellKind::FireSiphon) {
        intent.kind = "heal";
    } else if (spellKind == SpellKind::Poison) {
        intent.kind = "poison";
        intent.scale = std::max(0.5, spell.scale);
    } else if (spellKind == SpellKind::Drain || spellKind == SpellKind::Soul || spellKind == SpellKind::SoulBurst) {
        intent.kind = "drain";
        intent.scale = std::max(0.6, spell.scale);
    } else if (spellKind == SpellKind::Steal || spellKind == SpellKind::Mana) {
        intent.kind = "weaken";
        intent.scale = std::max(0.4, spell.scale);
    } else if (spellKind == SpellKind::Store || spellKind == SpellKind::Flow || spellKind == SpellKind::Burst ||
               spellKind == SpellKind::Draw || spellKind == SpellKind::Foresee || spellKind == SpellKind::Redraw ||
               spellKind == SpellKind::Exhaust || spellKind == SpellKind::Empower || spellKind == SpellKind::Ward) {
        // 灵力经营类：对方用出来就是「凝神蓄势」—— 归到凝盾，
        // 相当于它在为下一手做准备，玩家能看出来。
        intent.kind = "guard";
        intent.scale = 0;
    } else if (spellKind == SpellKind::Heavy || spellKind == SpellKind::BodyBurst || spellKind == SpellKind::Fire || spellKind == SpellKind::Wave) {
        intent.kind = "heavy";
        intent.scale = std::max(0.9, spell.scale);
    } else {
        intent.kind = "attack";
    }
    return intent;
}

// 用途：按敌方类型与回合选择简化行动。
// 参与流程：妖兽与旧档首轮意图规划。
// 使用方式：传当前战斗，使用已有风格和冷却信息。
// 返回值：本轮意图定义。
Intent battleIntent(const Battle& battle) {
    Intent intent;
    const int next = battle.turn + 1;

    // 每 4 回合必有一次凝盾回气（无论人兽，都留一口气）
    if (next % 4 == 0) {
        intent.kind = "guard"; intent.name = "凝盾回气"; intent.scale = 0;
        return intent;
    }

    // 人类修士：优先用本命神通（「神通 + 平A」配合）
    // 妖兽 enemyBeast=true，enemySpells 为空，直接落到下面的力大砖飞分支。
    if (!battle.enemySpells.empty()) {
        // 奇数回合才动神通，偶数回合平A —— 这才是「配合」而不是狂甩神通。
        // 加上冷却下限 2，同一门神通不会连着两回合出现。
        if (next % 2 == 1) {
            vector<const SpellDef*> ready;
            for (const auto& id : battle.enemySpells) {
                const SpellDef* spell = battleSpell(id);
                if (!spell) continue;
                auto cd = battle.enemyCd.find(id);
                if (cd != battle.enemyCd.end() && cd->second > 0) continue;
                ready.push_back(spell);
            }
            if (!ready.empty()) {
                // 用回合数做稳定挑选（战斗确定性，便于复现与测试）
                const SpellDef* spell = ready[static_cast<size_t>(battle.turn) % ready.size()];
                return spellToIntent(*spell);
            }
        }
        // 平A 回合 / 神通都在冷却
        intent.kind = "attack";
        intent.name = (battle.enemyDao.empty() ? string("正面攻击")
                                      : battle.enemyDao + "系平击");
        intent.scale = 0.85;
        return intent;
    }

    // 妖兽 / 无神通的对手：力大砖飞，只会砸
    const string mode = battle.style.empty() ? "剑" : battle.style;
    if (content::parseDaoId(mode) == DaoId::Mind && next % 3 == 0) {
        intent.kind = "pierce"; intent.name = "神识穿透"; intent.scale = 1.0;
        return intent;
    }
    if (content::parseDaoId(mode) == DaoId::Fire && next % 3 == 0) {
        intent.kind = "heavy"; intent.name = "烈焰爆发"; intent.scale = 1.3;
        return intent;
    }
    if (content::parseDaoId(mode) == DaoId::Wood && next % 3 == 0) {
        intent.kind = "heal"; intent.name = "回春反击"; intent.scale = 0.65;
        return intent;
    }
    intent.kind = "attack";
    intent.name = (content::parseDaoId(mode) == DaoId::Body) ? "近身重拳"
              : (content::parseDaoId(mode) == DaoId::Water) ? "水剑连斩"
                               : "正面攻击";
    intent.scale = 0.85;
    return intent;
}

// 用途：判断是否有仍可行动的战斗。
// 参与流程：API 动作阻塞及斗法生命周期。
// 使用方式：传游戏；影身已结算但保留战报时返回假。
// 返回值：战斗存在且没有影身结局时为真。
bool battleActive(const Game& g) {
    return g.player.arts.battle && g.player.arts.battle->shadowOutcome.empty();
}

// 用途：读取保留战报中的影身结局。
// 参与流程：影身对局结束状态查询。
// 使用方式：传独立遭遇游戏。
// 返回值：结局字符串引用；没有战斗时为空引用。
const string& battleShadowOutcome(const Game& g) {
    static const string none;
    return g.player.arts.battle ? g.player.arts.battle->shadowOutcome : none;
}

// ------------------------------------------------------------
// 洗牌与抽牌
// ------------------------------------------------------------
// 用途：用游戏随机状态原地洗牌。
// 参与流程：双方牌组初始化与弃牌重洗。
// 使用方式：传游戏和需洗的 UID 牌堆。
// 返回值：无；修改牌堆顺序和游戏 RNG。
static void battleShuffle(Game& g, vector<string>& pile) {
    // 与 JS 的 Fisher-Yates 一致（从后往前，j 取 [0, i]）
    for (int i = static_cast<int>(pile.size()) - 1; i > 0; --i) {
        const int j = static_cast<int>(randomOf(g) * (i + 1));
        std::swap(pile[static_cast<size_t>(i)], pile[static_cast<size_t>(j)]);
    }
}

// 用途：为玩家抽牌并在必要时重洗弃牌。
// 参与流程：开局、回合开始与抽牌神通。
// 使用方式：传游戏与数量；过滤已移除牌，手牌最多八张。
// 返回值：无；修改玩家战斗牌堆、手牌、洗牌次数与 RNG。
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

// 用途：通过卡牌 UID 查本场对应神通。
// 参与流程：手牌解析与移除牌筛选。
// 使用方式：传战斗和卡牌 UID。
// 返回值：牌组内卡牌常量指针；未知 UID 返回空。
static const Card* findCard(const Battle& b, const string& uid) {
    for (const auto& c : b.deck)
        if (c.uid == uid) return &c;
    return nullptr;
}

// 用途：解析忘机咒的单张目标；流程：玩家移除校验；使用：优先UID，旧神通ID兼容选首个仍在场副本。
// 返回：合法目标UID或空；只接受手牌/抽牌/弃牌中的非忘机卡，不读取随机数或改变状态。
static string mfResolveExhaustTargetUid(const Battle& State, const SpellDef& ExhaustSpell, const string& RequestedTarget) {
    const auto IsActiveUid = [&](const string& Uid) {
        if (std::find(State.exhausted.begin(), State.exhausted.end(), Uid) != State.exhausted.end()) return false;
        for (const auto* Pile : {&State.hand, &State.draw, &State.discard})
            if (std::find(Pile->begin(), Pile->end(), Uid) != Pile->end()) return true;
        return false;
    };
    if (const auto* TargetCard = findCard(State, RequestedTarget)) {
        if (TargetCard->spell == ExhaustSpell.id || !IsActiveUid(TargetCard->uid)) return "";
        return TargetCard->uid;
    }
    if (!battleSpell(RequestedTarget) || RequestedTarget == ExhaustSpell.id) return "";
    for (const auto* Pile : {&State.hand, &State.draw, &State.discard})
        for (const auto& Uid : *Pile) {
            const auto* TargetCard = findCard(State, Uid);
            if (TargetCard && TargetCard->spell == RequestedTarget && IsActiveUid(Uid)) return Uid;
        }
    return "";
}

// 用途：将已校验的一张卡移入本场移除堆；流程：玩家与NPC忘机咒共用。
// 使用：目标必须先经校验且尚未移动；返回：无，仅移除这一UID，保留同神通其他副本和下场牌组。
static void mfExhaustOneCard(Battle& State, const string& TargetUid) {
    // 先完成可能分配内存的追加，再改原牌堆；失败不会半途删除目标。
    State.exhausted.push_back(TargetUid);
    for (auto* Pile : {&State.hand, &State.draw, &State.discard}) {
        const auto Position = std::find(Pile->begin(), Pile->end(), TargetUid);
        if (Position != Pile->end()) { Pile->erase(Position); return; }
    }
}

// 确定并存下敌方本回合意图（含已算好的原始伤害）
// 用途：规划并登记下一次敌方行动。
// 参与流程：开局及下一轮抽牌后的意图展示。
// 使用方式：传游戏，真实修士转到独立牌堆规划。
// 返回值：无；更新意图、旧冷却和必要 RNG。
static void battlePlan(Game& g) {
    Battle& b = *g.player.arts.battle;
    if (b.enemyCards.enabled) {
        battleEnemyPlan(g);
        return;
    }
    if (b.enemyBeast) {
        BeastCombat::mfPlanActions(g);
        return;
    }
    const Intent it = battleIntent(b);
    b.intentKind = it.kind;
    b.intentName = it.name;
    b.intentScale = it.scale;
    b.intentSpell = it.spell;
    b.intentRaw = jsRound(mfBattleDamageBasisFromPower(b.enemyPower) * it.scale *
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
// 用途：汇总当前秘境遗物的抽牌或回灵收益。
// 参与流程：秘境开局及每轮抽牌初始化。
// 使用方式：传游戏和是否取抽牌收益的标记。
// 返回值：本次秘境对应收益；不在同一秘境时为零。
static int dungeonRelicValue(const Game& g, bool draw) {
    if (!g.dungeons.run || !g.player.arts.battle ||
        g.player.arts.battle->dungeonSite != g.dungeons.run->siteId) return 0;
    int amount = 0;
    for (const auto& id : g.dungeons.run->relics)
        for (const auto& relic : dungeonRelics()) if (relic.id == id)
            amount += draw ? relic.draw : relic.mana;
    return amount;
}

// 用途：构建合法玩家牌组并分配卡牌 UID。
// 参与流程：普通斗法与影身开局。
// 使用方式：传角色及 UID 前缀，过滤重复、超槽和境界不足神通。
// 返回值：基础攻防和额外神通副本的牌组。
static vector<Card> battlePlayerDeck(const Player& p, const string& prefix) {
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

    vector<Card> deck;
    for (size_t i = 0; i < spells.size(); ++i)
        deck.push_back({prefix + std::to_string(i), spells[i]});
    return deck;
}

// 用途：初始化或迁移玩家战斗牌组与开局资源。
// 参与流程：battleStart、影身开局及旧战斗迁移。
// 使用方式：已有 battle 时调用；migrate 指定旧灵力折半迁移。
// 返回值：无；修改战斗牌堆、灵力、意图及游戏 RNG。
static void battleInitCards(Game& g, bool migrate) {
    Player& p = g.player;
    Battle& b = *p.arts.battle;
    b.deck = battlePlayerDeck(p, "card-");
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
// 用途：生成替换一项丹药准备后的只读开局环境。
// 参与流程：服药计划与目录数值预览。
// 使用方式：传角色、准备键和新值，不抽牌或生成敌人。
// 返回值：开局属性 Battle 值，不改变玩家或 RNG。
Battle battleReferenceWithPreparation(const Player& p, const string& preparationKey, double preparationValue) {
    const Arts& a = p.arts;
    const ArtMods m = talentMods(p);
    const auto prep = [&](const char* key) {
        if (preparationKey == key) return preparationValue;
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
    b.buff = preparationKey == "attack" ? preparationValue : a.combatBuff;
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

// 用途：生成角色当前准备下的参考开局状态。
// 参与流程：场外神通和法宝预览。
// 使用方式：只读传角色，保留现有准备值。
// 返回值：与真实开局属性规则一致的 Battle 值。
Battle battleReferenceState(const Player& p) {
    return battleReferenceWithPreparation(p, "", 0);
}

// 用途：登记敌人、奖励上下文并开始普通斗法。
// 参与流程：游历、悬赏、切磋及野外冲突入口。
// 使用方式：传游戏与完整敌人规格，先手可在入口内结算。
// 返回值：无；写入玩家战斗、消费准备，更新 RNG 与日志。
void battleStart(Game& game, const EnemySpec& enemy) {
    const auto battleKind = content::parseBattleKind(enemy.kind);
    if (!battleKind) return;
    Player& player = game.player;
    Arts& arts = player.arts;
    const int enemyBase = BATTLE_POWER[clampInt(enemy.stage, 0, 12)];
    Battle battle = battleReferenceState(player);
    battle.poisonDamage = 0;
    const int maxHp = battle.max;

    // Demo07：对手气血额外 ×1.45
    const int enemyMaxHp = jsRound(
        (110 + enemyBase * 2.4) * 1.45 *
        (enemy.health ? std::max(0.5, enemy.health / 100.0) : 1.0));

    battle.enemy = enemy.name;
    battle.npcId = enemy.id;
    battle.quest = enemy.quest;
    battle.kind = content::legacyValue(*battleKind);
    battle.tideEventId = enemy.tideEventId;
    battle.tideBattlefield = enemy.tideBattlefield;
    battle.lethal = enemy.lethal;
    battle.huntFee = enemy.huntFee;
    battle.wildTask = enemy.wildTask;
    battle.dungeonSite = enemy.dungeonSite;
    battle.dungeonNode = enemy.dungeonNode;
    battle.advFee = enemy.wildFee;
    if (enemy.adventure) {
        const AdvContext& context = *enemy.adventure;
        battle.advKind = context.kind;
        battle.advSeq = context.seq;
        battle.advChoice = enemy.adventureChoice;
        battle.advFee = context.fee;
        battle.advGrade = context.grade;
        battle.advBook = context.book;
        battle.advNpcId = context.npcId;
        battle.advStyle = context.style;
    }
    battle.turn = 0;
    battle.max = maxHp;
    battle.hp = std::max(1, jsRound(maxHp * player.health / 100.0));
    battle.enemyMax = enemyMaxHp;
    battle.enemyHp = enemyMaxHp;
    battle.enemyPower = enemyBase;
    battle.enemySword = (enemy.sword >= 0) ? enemy.sword
                                      : std::min(500, 15 + enemy.stage * 20);
    battle.enemySwordCult = (content::parseDaoId(enemy.style) == DaoId::Sword) || (content::parseSectId(enemy.sect) == SectId::Jinhong);

    // 对方的功法：人类修士带本命神通（「神通 + 平A」配合），
    // 妖兽不带（enemySpells 空着，battleIntent 会走「力大砖飞」）。
    battle.enemyDao.clear();
    battle.enemySpells.clear();
    battle.enemyBeast = false;
    battle.enemyCd.clear();
    battle.enemyWeaken = 0;
    if (!enemy.id.empty()) {
        for (const auto& n : game.npcs) {
            if (n.id != enemy.id) continue;
            battle.enemyDao = n.dao;
            battle.enemyBeast = n.beast;
            if (!n.beast) battle.enemySpells = n.spells;
            break;
        }
    }

    battle.enemyShield = 0;
    battle.burn = 0;
    battle.guard = 0.0;
    battle.attacked = false;
    battle.played = 0;

    if (!enemy.style.empty()) {
        battle.style = enemy.style;
    } else {
        DaoId style = DaoId::Body;
        if (const auto sectId = content::parseSectId(enemy.sect))
            style = *sectId == SectId::Jinhong ? DaoId::Sword : content::sectDao(*sectId);
        battle.style = content::legacyDaoKey(style);
    }
    battle.log.push_back(
        string(content::text(TextId::BattleRules)));

    arts.battle = battle;

    Battle& activeBattle = *arts.battle;
    if (game.dungeons.run && activeBattle.dungeonSite == game.dungeons.run->siteId)
        for (const auto& id : game.dungeons.run->relics)
            for (const auto& relic : dungeonRelics()) if (relic.id == id)
                activeBattle.shield = std::min(jsRound(maxHp * 0.3), activeBattle.shield + jsRound(maxHp * relic.shield));

    battleEnemyInit(game, enemy);

    battleInitCards(game, false);

    arts.combatPrep.clear();
    arts.combatBuff = 0;

    logEvent(game, "self",
             "与" + enemy.name + "开始" + (battleDefeatEndsLife(battle) ? "生死斗法" : "斗法") +
                 (content::parseBattleKind(battle.kind) == BattleKind::Tide
                     ? "，兽潮中每回合推进一天。" : "，月历暂停。"),
             true);
    if (!arts.battle->playerFirst) {
        arts.battle->log.push_back("对方遁速更快，抢先施放首轮神通。");
        if (arts.battle->enemyCards.enabled) battleEnemyAct(game);
        else if (!arts.battle->BeastActions.empty()) BeastCombat::mfExecuteActions(player, *arts.battle, true);
        else {
            int blocked = 0;
            const int loss = battleIncomingLoss(player, *arts.battle, blocked);
            arts.battle->hp -= loss;
            arts.battle->shield -= blocked;
            const int counter = jsRound(blocked * talentMods(player).bodyCounter);
            if (counter > 0) {
                arts.battle->enemyHp -= counter;
                arts.battle->log.push_back("盾挡反震：伤害" + std::to_string(counter) + "。");
            }
            arts.battle->log.push_back("对方先手" + arts.battle->intentName + "：气血−" +
                                   std::to_string(loss) + "。");
        }
        if (!arts.battle) return;
        if (arts.battle->hp <= 0) {
            battleFinish(game, false);
            return;
        }
        if (arts.battle->enemyHp <= 0) {
            battleFinish(game, true);
            return;
        }
        battlePlan(game);
    } else if (arts.battle) {
        arts.battle->log.push_back("你的遁速取得先手，先行出牌。");
    }
}

// ------------------------------------------------------------
// 结算
// ------------------------------------------------------------
// 用途：判断这场战斗失败是否结束角色一世。
// 参与流程：战败结算和界面风险提示。
// 使用方式：传战斗种类；恢复点不改变该风险。
// 返回值：该种类失败会结束一世时为真。
bool battleDefeatEndsLife(const Battle& battle) {
    const auto battleKind = content::parseBattleKind(battle.kind);
    return !battle.shadowPlayer && (battle.lethal || !battle.dungeonSite.empty() || !battle.advKind.empty() ||
        battleKind == BattleKind::Wild || battleKind == BattleKind::Hunt || battleKind == BattleKind::Tide || battleKind == BattleKind::Ambush ||
        battleKind == BattleKind::Kill || content::parseBattleKind(battle.kind) == BattleKind::Rob);
}

// 用途：按胜负或撤离结算本场战斗。
// 参与流程：先手、出牌斩杀、回合末及撤离收尾。
// 使用方式：传游戏、胜负和撤离标记；影身只记独立结局。
// 返回值：无；更新战斗/健康、成长、奖励、任务和日志。
void battleFinish(Game& game, bool win, bool escape) {
    Player& player = game.player;
    Arts& arts = player.arts;
    if (!arts.battle.has_value()) return;
    if (arts.battle->shadowPlayer) {
        Battle& result = *arts.battle;
        if (!result.shadowOutcome.empty()) return;
        const bool draw = (result.turn >= 24 && result.hp > 0 && result.enemyHp > 0) ||
                          (result.hp <= 0 && result.enemyHp <= 0);
        result.shadowOutcome = escape ? "retreat" : draw ? "draw" : win ? "win" : "loss";
        result.retreat = escape;
        result.won = result.shadowOutcome == "win";
        result.log.push_back(result.shadowOutcome == "draw" ? "天机切磋：平局。" :
            escape ? "天机切磋：认输。" : win ? "天机切磋：获胜。" : "天机切磋：落败。");
        return;
    }
    Battle battle = *arts.battle;
    const auto battleKind = content::parseBattleKind(battle.kind);

    battle.retreat = escape;
    battle.won = win;
    const bool fatalDefeat = battleDefeatEndsLife(battle);
    if (!win && !escape && fatalDefeat) {
        player.health=0;
        if (battleKind==BattleKind::Tide) {
            // 致命回合先记日，再写结局和纪事；跨年时年龄、年份保持一致。
            calendarBattleDay(game,"野外战败：" + battle.enemy);
            tide::settleIfDue(game);
            return;
        }
        finish(game,"野外战败：" + battle.enemy);
        return;
    }
    // 生死斗战败已结束该世；胜利、撤离与非致命斗法保留 1 点气血。
    player.health = std::max(1,
                        std::min(100, jsRound(battle.hp * 100.0 / battle.max)));

    if (win && battleKind != BattleKind::SectTrial) {
        arts.progress.wins++;
        if (battleKind == BattleKind::Spar && battle.advKind.empty())
            player.stones += 20 + (player.stage / 3) * 25;
        artGain(player, arts.focus, 20);
        // 机缘斗法（门派大比 / 仙与鸳鸯）胜利要发奖，
        // 否则机缘永远完不成，而且能反复花灵石重开。
        if (!battle.quest.empty()) {
            for (const auto& q : data::QuestDef_TABLE()) {
                if (q.id != battle.quest) continue;
                artRewardQuest(game, q);
                break;
            }
        }
    }
    if (!battle.npcId.empty() && battleKind == BattleKind::Spar)
        addRelation(game, battle.npcId, 4 + (!escape ? talentAffinityBonus(game.player) : 0));

    // 兽潮迎战：赏赐比普通野外悬赏厚得多，这是「成长动力」的来源
    if (battleKind == BattleKind::Tide && !escape)
        tide::tideReward(game, win, &battle);

    const string outcome = escape ? "撤离"
                                   : win ? "获胜"
                                         : (battle.turn >= 24 ? "回合耗尽" : "落败");
    logEvent(game, "self",
             battle.enemy + "之战：" + outcome + "。" +
                 (fatalDefeat ? "此战有陨落风险。" : "本场不致死。"),
             true);


    // ---- 奖励结算 ----
    // 放在这里而不是各个界面里：battleFinish 是唯一的收口，
    // 控制台版和网页版都会经过它。之前把结算放在界面层，
    // 结果网页版赢了悬赏、猎妖、游历夺宝全都零收益。
    adventureBattleEnd(game, battle);   // 游历遇事的战利品
    societyWildEnd(game, battle);       // 野外悬赏的赏金
    ecoHuntEnd(game, win);         // 猎妖委托的赏金与材料
    conflict::battleEnd(game, battle);  // 野外冲突胜利后的掠夺与生死结果

    arts.battle.reset();
    if (battleKind == BattleKind::SectTrial) sectTrialBattleComplete(game, battle, win, escape);
    if (!battle.dungeonSite.empty()) dungeonBattleComplete(game, battle, win);

    if (battleKind == BattleKind::Ambush && game.job && game.job->actionType == "travel") {
        logEvent(game, "self", "摆脱途中遭遇，可以继续未完成的行程。", true);
    }
}

// ------------------------------------------------------------
// 单张卡的效果（对应 JS 的 battleEffect）
//
// 预览与真实出牌共用这一个函数：预览作用在 Battle 的副本上，
// 所以预览显示的伤害与实际结算必然一致。
// 用途：施加受气血比例上限限制的护盾。
// 参与流程：守势、护盾技能及弃牌凝盾。
// 使用方式：传角色、当前视角战斗和申请护盾量。
// 返回值：实际增加的护盾；同时在有效获得护盾时增加蓄势。
static int battleAddShield(const Player& p, Battle& b, int amount) {
    const int before = b.shield;
    b.shield = std::min(jsRound(b.max * 0.3), b.shield + std::max(0, amount));
    const int gain = std::max(0, b.shield - before);
    if (gain > 0)
        b.charge = std::min(4, b.charge + jsFloor(talentMods(p).shieldCharge));
    return gain;
}

// 用途：结算每张弃牌带来的护盾和锐势。
// 参与流程：换念诀、双方回合末弃牌。
// 使用方式：传角色、对应战斗视角与弃牌数；不负责移动牌堆。
// 返回值：无；修改护盾、蓄势及下一击锐势。
static void battleDiscard(const Player& p, Battle& b, int count, DiscardRules::Timing Timing = DiscardRules::Timing::Active) {
    if (count <= 0) return;
    const ArtMods m = talentMods(p);
    const double ShieldScale = Timing == DiscardRules::Timing::RoundEnd ? DiscardRules::RoundEndShieldPercent / 100.0 : 1.0;
    battleAddShield(p, b, jsRound(b.max * m.discardShield * count * ShieldScale));
    b.discardPower = std::min(0.30, b.discardPower + m.discardPower * count);
}

// 用途：按同一结算路径执行单张神通效果。
// 参与流程：玩家、敌方真实出牌和副本预览共用。
// 使用方式：传施术者、可写战斗、神通及目标减伤/反震。
// 返回值：效果战报；修改灵力、攻防、状态和抽牌请求，不直接抽牌。
static string battleEffect(const Player& player, Battle& battle, const SpellDef& spell,
                          double targetReduction = 0, double targetCounter = 0, int* RawDamageOutput = nullptr,
                          bool* ElementalGenerationOutput = nullptr) {
    if (RawDamageOutput) *RawDamageOutput = 0;
    if (ElementalGenerationOutput) *ElementalGenerationOutput = false;
    if (!content::parseSpellKind(spell.kind) || !content::parseDaoId(spell.dao)) return "";
    const Arts& arts = player.arts;
    const ArtMods modifiers = talentMods(player);
    const double DamageBasis = mfBattleDamageBasis(player.stage);
    const string id = spell.id;
    const int cost = battleCost(player, spell);
    const int manaBefore = battle.mana;
    const int manaCap = battleManaCap(player);
    battle.mana -= cost;
    const bool HasElementalSwordCombo = ElementalSword::mfAdvanceSwordChain(battle, spell);

    string text;

    if (id == "defend") {
        const int shield = jsRound(battle.max * std::min(0.3, 0.12 * modifiers.shield));
        const int gained = battleAddShield(player, battle, shield);
        text = spell.name + "：本回合护盾+" + std::to_string(gained) + "。";
        battle.swordSequence = 0;
    } else {
        const auto spellKind = content::parseSpellKind(spell.kind);
        const string dao = spell.dao;
        const auto daoId = content::parseDaoId(dao);
        // 相生只比较最近有效五行牌，非五行经营牌和回合边界不改写轨迹。
        const bool combo = ElementalSword::mfHasGeneratingCombo(battle, spell);

        const int ei = daoIndex(dao);
        const double elementMul = (ei >= 0) ? modifiers.elements[ei] : 1.0;

        double damage = jsRound(
            DamageBasis * spell.scale * modifiers.attack * elementMul *
            (1 + battle.buff) *
            (combo ? 1.15 + std::min(0.15, modifiers.combo) : 1.0) *
            (daoId == DaoId::Fire ? modifiers.fire : 1.0) *
            (daoId == DaoId::Mind ? modifiers.drain : 1.0) *
            ((artHas(arts, dao, 1) && (daoId == DaoId::Metal || daoId == DaoId::Fire)) ? 1.1 : 1.0) *
            ((daoId == DaoId::Sword && artHas(arts, "剑", 1)) ? 1.12 : 1.0));

        bool direct = false;
        string statusText;
        const bool swordCard = spellKind == SpellKind::Sword || spellKind == SpellKind::Human;
        const bool IsElementalSword = ElementalSword::mfIsElementalSword(spell);
        if (!swordCard) battle.swordSequence = 0;

        if (spellKind == SpellKind::Mana) {
            const int gain = std::max(1, jsRound(spell.scale / 2.0));
            const int before = battle.mana;
            battle.mana = std::min(manaCap, battle.mana + gain);
            text = spell.name + "：恢复" + std::to_string(battle.mana - before) + "灵力。";
        } else if (spellKind == SpellKind::Store) {
            // 蓄灵：把本回合剩下的灵力留到下回合。
            // 代价写在这张牌自己身上：本回合灵力清零、不再回满，
            // 而且封存有上限（scale 决定），不能靠它无限滚雪球。
            const int cap = std::min(manaCap, std::max(1, jsRound(spell.scale * 4.0)) + jsFloor(modifiers.carryBonus));
            battle.carryCap = cap;
            const int keep = std::min({battle.mana, cap, manaCap});
            battle.carryMana = keep;
            battle.mana = 0;
            text = spell.name + "：将 " + std::to_string(keep) +
                   " 点灵力封存，下回合可用（上限 " + std::to_string(cap) +
                   "；本回合灵力清零）。";
        } else if (spellKind == SpellKind::Flow) {
            // 聚气：每回合灵力上限 +1，代价是气血上限下降（本场）
            battle.manaBonus += 1;
            battle.hpCostPct += 8;
            const int cut = jsRound(battle.max * 0.08);
            battle.max = std::max(1, battle.max - cut);
            battle.hp = std::min(battle.hp, battle.max);
            battle.shield = std::min(battle.shield, jsRound(battle.max * 0.3));
            text = spell.name + "：每回合灵力 +1，代价是气血上限 −" +
                   std::to_string(cut) + "。";
        } else if (spellKind == SpellKind::Burst) {
            // 夺灵：立刻拿灵力，代价是本回合之后每回合少 1 点（本场）
            const int gain = std::max(2, jsRound(spell.scale / 1.5));
            battle.mana = std::min(manaCap, battle.mana + gain);
            battle.manaPenalty += 1;
            text = spell.name + "：立刻获得 " + std::to_string(gain) +
                   " 灵力，此后每回合灵力 −1。";
        } else if (spellKind == SpellKind::Draw) {
            // 抽牌：本回合多抽。实际抽牌要等 battleTurn 拿到 g 再做
            // （battleEffect 也被 battlePreview 用来试算，那里没有 Game）。
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(modifiers.drawBonus);
            battle.pendingDraw += n;
            text = spell.name + "：抽 " + std::to_string(n) + " 张。";
        } else if (spellKind == SpellKind::Foresee) {
            // 预谋：下回合多抽
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(modifiers.drawBonus);
            battle.drawNext += n;
            text = spell.name + "：下回合多抽 " + std::to_string(n) + " 张。";
        } else if (spellKind == SpellKind::Redraw) {
            const int count = static_cast<int>(battle.hand.size());
            battle.discard.insert(battle.discard.end(), battle.hand.begin(), battle.hand.end());
            battle.hand.clear();
            battleDiscard(player, battle, count);
            const int n = count + std::max(1, jsRound(spell.scale)) + jsFloor(modifiers.drawBonus);
            battle.pendingDraw += n;
            text = spell.name + "：弃置" + std::to_string(count) + "张手牌，抽" + std::to_string(n) + "张。";
        } else if (spellKind == SpellKind::Exhaust) {
            const int n = std::max(1, jsRound(spell.scale)) + jsFloor(modifiers.drawBonus);
            battle.pendingDraw += n;
            text = spell.name + "：将所选的一张卡移出本场牌组，抽" + std::to_string(n) + "张。";
        } else if (spellKind == SpellKind::Empower) {
            const double before = battle.buff;
            battle.buff = std::max(before, std::min(0.30, before + spell.scale));
            text = spell.name + "：本场伤害加成+" + std::to_string(jsRound((battle.buff - before) * 100)) + "%（累计" + std::to_string(jsRound(battle.buff * 100)) + "%）。";
        } else if (spellKind == SpellKind::Ward) {
            const double before = battle.reduction;
            battle.reduction = std::max(before, std::min(0.20, before + spell.scale));
            text = spell.name + "：本场额外减伤+" + std::to_string(jsRound((battle.reduction - before) * 100)) + "%（累计" + std::to_string(jsRound(battle.reduction * 100)) + "%）。";
        } else if (spellKind == SpellKind::Float) {
            battle.mana = std::min(manaCap, battle.mana + 2);
            battle.lock = 2;
            text = "悬灵术：恢复2灵力，随后两回合灵力−1。";
        } else if (spellKind == SpellKind::FireSiphon) {
            const int Converted = FireGrowth::mfExplosionAmount(modifiers, battle.FireGrowthRounds, battle.burn, battle.burnTurns);
            const int Restored = std::min(std::max(0, battle.max - battle.hp), Converted);
            battle.hp += Restored;
            battle.burn = 0; battle.burnTurns = 0;
            text = spell.name + "：消耗全部灼烧，转化治疗" + std::to_string(Converted) +
                "，实际气血+" + std::to_string(Restored) + "，不造成伤害。";
        } else if (spellKind == SpellKind::Heal) {
            const int before = battle.hp;
            const int heal = jsRound(
                battle.max * std::min(0.14, spell.scale * modifiers.heal *
                                           (artHas(arts, "木", 1) ? 1.15 : 1.0)));
            battle.hp = std::min(battle.max, battle.hp + heal);
            battle.playerPoison = std::max(0, battle.playerPoison - jsFloor(modifiers.poisonCleanse));
            if (battle.playerPoison == 0) battle.playerPoisonTurns = 0;
            if (battle.enemyCards.poisonDamage >= 0) battle.enemyCards.poisonDamage = battle.playerPoison;
            if (battle.enemyCards.enabled && battle.playerPoison == 0) {
                battle.enemyCards.poison = 0;
                battle.enemyCards.poisonTurns = 0;
            }
            text = spell.name + "：气血+" + std::to_string(battle.hp - before) + "。";
        } else if (spellKind == SpellKind::Shield || spellKind == SpellKind::Guard) {
            const int before = battle.shield;
            const int shield = jsRound(
                battle.max * std::min(0.3, spell.scale * modifiers.shield));
            battleAddShield(player, battle, shield);
            battle.guard = std::max(battle.guard, (spellKind == SpellKind::Guard) ? 0.1 : 0.0);
            if (spell.id == "array") battle.mana = std::min(manaCap, battle.mana + 1);
            text = spell.name + "：本回合护盾+" +
                   std::to_string(battle.shield - before) + "。";
        } else {
            direct = true;
            int ConsumedSoulLayers = 0;
            // 扰灵只削弱下一次直接攻击，治疗、抽牌不会消耗它。
            if (battle.enemyWeaken > 0) {
                damage = jsRound(damage * std::max(0.5, 1.0 - battle.enemyWeaken * 0.15));
                battle.enemyWeaken = 0;
            }
            damage = jsRound(damage * (1 + battle.discardPower));
            battle.discardPower = 0;

            if (spellKind == SpellKind::Sword || spellKind == SpellKind::Human) {
                damage = jsRound(damage * modifiers.sword * (1 + battle.swordQi * 0.07));
                battle.swordSequence++;
                damage = jsRound(damage * (1 + std::min(0.50, (battle.swordSequence - 1) * modifiers.swordCombo)));
                if (battle.swordSequence % 3 == 0)
                    battle.mana = std::min(manaCap, battle.mana + jsFloor(modifiers.swordMana));
                if (spellKind == SpellKind::Human) {
                    // 人剑消耗全部剑气与当前 8% 气血（不自伤致死）
                    battle.hp = std::max(1, battle.hp - std::max(1, jsRound(battle.hp * 0.08)));
                    battle.swordQi = 0;
                } else {
                    const int before = battle.swordQi;
                    battle.swordQi = std::min(8, battle.swordQi + jsRound((1 + jsFloor(modifiers.qiGain)) * spell.statusScale));
                    statusText = "剑气+" + std::to_string(battle.swordQi - before) + "层（共" +
                                 std::to_string(battle.swordQi) + "/8层）。";
                }
            }
            if (spellKind == SpellKind::Heavy) {
                damage = jsRound(damage * (1 + battle.charge * 0.12));
                damage += jsRound(battle.shield * modifiers.shieldStrike);
                battle.charge = 0;
            }
            if (spellKind == SpellKind::Wave) {
                const int stacks = battle.water;
                damage = jsRound(damage *
                                 (1 + battle.water * (0.15 + std::min(0.1, modifiers.water * 0.025))));
                const double baseWater = artModsOf(player.arts).waterMana;
                const int returned = std::min(2, jsFloor(stacks * baseWater)) +
                                     (hasTalent(player, "water") ? stacks : 0);
                battle.mana = std::min(manaCap, battle.mana + returned);
                battle.water = jsFloor(stacks * std::min(0.50, modifiers.waterKeep));
            }
            if (spellKind == SpellKind::PoisonBurst) {
                damage += jsRound(DamageBasis * battle.poison * 0.18);
                battle.poison = 0;
                battle.poisonDamage = 0;
                battle.poisonTurns = 0;
            }
            if (spellKind == SpellKind::SoulBurst) {
                ConsumedSoulLayers = clampInt(battle.soul, 0, 6);
                damage = jsRound(damage * (1 + ConsumedSoulLayers * SoulBurstPerLayerBonus));
                battle.soul = 0;
            }
            if (spellKind == SpellKind::FireBurst) {
                damage += FireGrowth::mfExplosionAmount(modifiers, battle.FireGrowthRounds, battle.burn, battle.burnTurns);
                battle.burn = 0;
                battle.burnTurns = 0;
            }
            if (spellKind == SpellKind::BodyBurst) {
                // Demo07：门槛从「灵力≥6」改为「灵力≥3」
                damage = jsRound(damage *
                                 (1 + std::min(0.18, player.body * 0.02)) *
                                 (manaBefore >= 3 ? 1.15 : 1.0) *
                                 (1 + battle.charge * 0.1));
                battle.charge = 0;
            }
            if (spellKind == SpellKind::Drain || daoId == DaoId::Mind)
                damage = jsRound(damage *
                                 (1 + std::min(0.15, arts.mind * 0.005)) *
                                 (artHas(arts, "神", 2) ? 1.2 : 1.0));
            if (spellKind == SpellKind::Steal)
                battle.mana = std::min(manaCap, battle.mana + 1 + std::min(1, jsFloor(modifiers.steal)));
            if (IsElementalSword) {
                damage = jsRound(damage * ElementalSword::mfDamageMultiplier(spell, combo));
                const int QiBeforeExtra = battle.swordQi;
                const int ManaBeforeExtra = battle.mana;
                ElementalSword::mfApplyResources(player, modifiers, battle, spell, HasElementalSwordCombo);
                if (battle.swordQi > QiBeforeExtra || battle.mana > ManaBeforeExtra)
                    statusText += "五剑衔接：额外剑气+" + std::to_string(battle.swordQi - QiBeforeExtra) +
                        "，回灵+" + std::to_string(battle.mana - ManaBeforeExtra) + "。";
            }

            // 穿盾与吸收
            const int RawDamageValue = std::max(0, jsRound(damage));
            if (RawDamageOutput) *RawDamageOutput = RawDamageValue;
            damage = jsRound(damage * (1 - clamp(targetReduction, -0.10, 0.35)));
            const int pierce = (spellKind == SpellKind::Drain) ? battle.enemyShield
                               : (id == "goldblade")
                                   ? jsRound(battle.enemyShield * 0.5)
                                   : 0;
            const int SoulPiercingDamage = spellKind == SpellKind::SoulBurst
                ? jsRound(damage * std::min(SoulBurstMaximumPierceFraction, ConsumedSoulLayers * SoulBurstPiercePerLayer)) : 0;
            const int absorb =
                std::min(std::max(0, battle.enemyShield - pierce), std::max(0, jsRound(damage) - SoulPiercingDamage));
            const int TargetHealthBefore = battle.enemyHp;
            battle.enemyShield -= absorb;
            battle.enemyHp -= jsRound(damage) - absorb;
            if (absorb > 0 && targetCounter > 0)
                battle.hp -= jsRound(absorb * targetCounter);
            if (IsElementalSword)
                ElementalSword::mfApplyHitBenefits(battle, spell, combo,
                    std::min(std::max(0, TargetHealthBefore), std::max(0, jsRound(damage) - absorb)));
            text = spell.name + "：打出伤害" + std::to_string(RawDamageValue) + "，实际气血伤害" +
                   std::to_string(jsRound(damage) - absorb) +
                   (combo ? "，相生连击" : "") + "。";
            text += statusText;

            if (spellKind == SpellKind::Charge) {
                const int before = battle.charge;
                battle.charge = std::min(4, battle.charge + jsRound((1 + jsFloor(modifiers.charge)) * spell.statusScale));
                text += "蓄势+" + std::to_string(battle.charge - before) + "层（共" + std::to_string(battle.charge) + "/4层）。";
            }
            if (spellKind == SpellKind::Water) {
                const int before = battle.water;
                battle.water = std::min(6, battle.water + jsRound(2 * spell.statusScale));
                text += "水势+" + std::to_string(battle.water - before) + "层（共" + std::to_string(battle.water) + "/6层）。";
            }
            if (spellKind == SpellKind::Poison) {
                const int add = (id == "corrode" || id == "toxinRain") ? 3 : 2;
                const int before = battle.poison;
                battle.poison = std::min(BATTLE_POISON_CAP, battle.poison + jsRound((add + jsFloor(modifiers.poison)) * spell.statusScale));
                battle.poisonTurns = 3;
                battle.poisonDamage = mfPoisonTickFromLayers(DamageBasis, battle.poison);
                if (battle.hp > 0)
                    battle.hp = std::min(battle.max, battle.hp + jsRound(battle.max * (battle.poison - before) * modifiers.poisonHeal));
                text += "毒+" + std::to_string(battle.poison - before) + "层（共" + std::to_string(battle.poison) +
                        "/" + std::to_string(BATTLE_POISON_CAP) + "层，剩余3回合）。";
            }
            if (spellKind == SpellKind::Soul) {
                const int before = battle.soul;
                battle.soul = std::min(6, battle.soul + jsRound((2 + jsFloor(modifiers.soul)) * spell.statusScale));
                text += "震荡+" + std::to_string(battle.soul - before) + "层（共" + std::to_string(battle.soul) + "/6层）。";
            }
            if (spellKind == SpellKind::Fire) {
                const double added = DamageBasis * BurnTickBasisCoefficient * (1 + modifiers.burnBonus) *
                                     (combo ? 1.5 : 1.0) *
                                     (1 + FireGrowth::mfBurnStackBonus(modifiers, battle.FireGrowthRounds)) * spell.statusScale;
                const int burn = jsRound(std::min<double>(BATTLE_BURN_CAP, added));
                const int before = battle.burn;
                battle.burn = std::min(BATTLE_BURN_CAP,
                                  clampInt(battle.burn, 0, BATTLE_BURN_CAP) + burn);
                battle.burnTurns = 3;
                text += "灼烧+" + std::to_string(battle.burn - before) + "（每回合当前总伤害" +
                        std::to_string(battle.burn) + "，剩余3回合）。";
            }
        }

        if (combo) {
            const int ManaBeforeGeneration = battle.mana;
            const int DrawBeforeGeneration = battle.pendingDraw;
            battle.mana = std::min(manaCap, battle.mana + jsFloor(modifiers.comboMana));
            const int draws = std::min(2 - battle.comboDraws, jsFloor(modifiers.comboDraw));
            battle.pendingDraw += std::max(0, draws);
            battle.comboDraws += std::max(0, draws);
            // 高亮直接读取真实生效事实：直伤相生或实际获得返灵/补牌；不让前端复制能力、容量和次数条件。
            if (ElementalGenerationOutput)
                *ElementalGenerationOutput = direct || battle.mana > ManaBeforeGeneration || battle.pendingDraw > DrawBeforeGeneration;
        }

        battle.attacked = battle.attacked || direct;
    }

    if (battle.hp > 0 && (battle.played + 1) % 3 == 0 && modifiers.thirdHeal > 0) {
        const int before = battle.hp;
        battle.hp = std::min(battle.max, battle.hp + jsRound(battle.max * std::min(0.12, modifiers.thirdHeal)));
        text += "三牌调息：气血+" + std::to_string(battle.hp - before) + "。";
    }

    // 冷却按完整回合结算，所以这里 +1 补偿本回合
    if (spell.cooldown) battle.cooldowns[id] = spell.cooldown + 1;
    if (ElementalSword::mfIsElementalSpell(spell))
        battle.last = (id == "fireSeal") ? "木火" : string(content::legacyDaoKey(*content::parseDaoId(spell.dao)));
    return text;
}

// 敌方使用独立牌堆，但每张牌共用玩家的费用、状态与连携结算。
#define ENEMY_FIELDS(X) \
    X(deck) X(hand) X(draw) X(discard) X(exhausted) X(cooldowns) \
    X(mana) X(maxMana) X(shuffles) X(played) X(attacked) \
    X(swordQi) X(water) X(soul) X(charge) X(lock) X(carryMana) \
    X(manaBonus) X(manaPenalty) X(drawNext) X(carryCap) X(hpCostPct) \
    X(poison) X(poisonTurns) X(poisonDamage) X(burn) X(burnTurns) X(swordSequence) \
    X(comboDraws) X(ElementalSwordManaReturns) X(ElementalSwordSequence) X(FireGrowthRounds) X(buff) X(reduction) X(guard) X(discardPower) X(last)

// 用途：构造当前敌方施术者的角色视图。
// 参与流程：敌方费用、属性与法宝查询。
// 使用方式：传游戏和战斗，优先使用影身固定快照。
// 返回值：敌方角色值，包含真实 NPC 的有效装备。
static Player battleEnemyPlayer(const Game& g, const Battle& b) {
    if (b.shadowPlayer) return *b.shadowPlayer;
    const EnemyCombat& e = b.enemyCards;
    Player p;
    p.talent.clear();
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

// 用途：汇总敌方固定构筑的战斗系数。
// 参与流程：玩家攻击目标减伤和敌方持续状态增长。
// 使用方式：传战斗，影身使用完整天赋快照。
// 返回值：敌方功法、悟道及适用天赋加成。
static ArtMods battleEnemyMods(const Battle& b) {
    if (b.shadowPlayer) return talentMods(*b.shadowPlayer);
    Arts a;
    a.main = b.enemyCards.main; a.aux = b.enemyCards.aux;
    a.manuals = b.enemyCards.manuals; a.nodes = b.enemyCards.nodes;
    a.mind = b.enemyCards.mind;
    return artModsOf(a);
}

// 每个完整回合只结算一次；敌方意图也在副本上执行这一步，避免引爆
// 使用尚未扣除的持续回合，或让已经到期的毒继续参与敌方神通。
// 用途：结算双方持续毒伤和灼烧并推进持续回合。
// 参与流程：真实回合结束和敌方预览副本。
// 使用方式：传角色、战斗和是否写日志，每完整回合调用一次。
// 返回值：无；修改双方气血、毒/灼烧状态和可选战报。
static void battleTickStatuses(const Player& p, Battle& b, bool log) {
    const ArtMods m = talentMods(p);
    const double DamageBasis = mfBattleDamageBasis(p.stage);
    const int PlayerBurnTick = b.burnTurns > 0 ? b.burn : 0;
    const int dot =
        (b.poisonTurns > 0 ? (b.poisonDamage >= 0 ? b.poisonDamage : mfLegacyPoisonTickFromLayers(DamageBasis, b.poison)) : 0) +
        (b.burnTurns > 0 ? b.burn : 0);
    b.enemyHp -= dot;
    FireGrowth::mfAdvanceAfterBurnTick(m, PlayerBurnTick, b.FireGrowthRounds);
    if (log && dot) b.log.push_back("回合结束：持续伤害" + std::to_string(dot) + "。");
    if (b.poisonTurns > 0 && --b.poisonTurns == 0) {
        b.poison = 0;
        if (b.poisonDamage >= 0) b.poisonDamage = 0;
    }
    if (b.poisonTurns > 0) {
        const int growth = jsFloor(m.poisonGrowth);
        b.poison = std::min(BATTLE_POISON_CAP, b.poison + growth);
        if (growth > 0)
            b.poisonDamage = mfPoisonTickFromLayers(DamageBasis, b.poison);
    }
    if (b.burnTurns > 0 && --b.burnTurns == 0) b.burn = 0;
    if (b.enemyHp <= 0 && !b.shadowPlayer) return;

    // 敌方施加的毒与灼烧独立结算，不会反过来伤害施术者。
    const int burning = b.enemyCards.burnTurns > 0 ? b.enemyCards.burn : 0;
    if (b.playerPoisonTurns <= 0 && burning <= 0) return;
    const int poisonLoss = b.playerPoisonTurns > 0 ? b.playerPoison : 0;
    b.hp -= poisonLoss + burning;
    FireGrowth::mfAdvanceAfterBurnTick(battleEnemyMods(b), burning, b.enemyCards.FireGrowthRounds);
    if (log) b.log.push_back("毒与灼烧：气血−" + std::to_string(poisonLoss + burning) + "。");
    if (b.enemyCards.burnTurns > 0 && --b.enemyCards.burnTurns == 0)
        b.enemyCards.burn = 0;
    if (b.playerPoisonTurns > 0 && --b.playerPoisonTurns == 0) b.playerPoison = 0;
    if (!b.enemyCards.enabled) return;
    b.enemyCards.poisonTurns = b.playerPoisonTurns;
    if (b.enemyCards.poisonDamage >= 0) b.enemyCards.poisonDamage = b.playerPoison;
    if (b.playerPoisonTurns == 0) b.enemyCards.poison = 0;
    else {
        const int growth = jsFloor(battleEnemyMods(b).poisonGrowth);
        if (growth > 0) {
            b.enemyCards.poison = std::min(BATTLE_POISON_CAP, b.enemyCards.poison + growth);
            b.playerPoison = mfPoisonTickFromLayers(mfBattleDamageBasisFromPower(b.enemyPower), b.enemyCards.poison);
            b.enemyCards.poisonDamage = b.playerPoison;
        }
    }
}

// 用途：把敌方资源翻转为施术者视角的 Battle。
// 参与流程：敌方真实行动与只读预览。
// 使用方式：传完整战斗和玩家境界，用于转换毒伤量。
// 返回值：独立敌方视角值，攻守目标已互换。
static Battle battleEnemyView(const Battle& b, int playerStage) {
    Battle v;
#define ENEMY_READ(field) v.field = b.enemyCards.field;
    ENEMY_FIELDS(ENEMY_READ)
#undef ENEMY_READ
    v.hp = b.enemyHp; v.max = b.enemyMax; v.shield = b.enemyShield;
    v.enemyHp = b.hp; v.enemyMax = b.max; v.enemyShield = b.shield;
    v.playerPoison = b.poisonTurns > 0
        ? (b.poisonDamage >= 0 ? b.poisonDamage :
           mfLegacyPoisonTickFromLayers(mfBattleDamageBasis(playerStage), b.poison)) : 0;
    v.playerPoisonTurns = b.poisonTurns;
    // 旧敌方状态已有玩家每跳伤害；复制该冻结值，不能再从整层反推或遗漏清毒余量。
    if (v.poisonDamage < 0)
        v.poisonDamage = b.playerPoisonTurns > 0 ? b.playerPoison : 0;
    return v;
}

// 用途：把敌方施术后的视角同步回主战斗。
// 参与流程：敌方出牌、回灵与抽牌后保存。
// 使用方式：传主战斗、已执行的敌方视角及玩家境界。
// 返回值：无；更新双方气血护盾和敌方牌组、状态、冷却。
static void battleSaveEnemyView(Battle& b, const Battle& v, [[maybe_unused]] int playerStage) {
    const int oldPoison = b.enemyCards.poison;
    const int oldPoisonTurns = b.enemyCards.poisonTurns;
#define ENEMY_WRITE(field) b.enemyCards.field = v.field;
    ENEMY_FIELDS(ENEMY_WRITE)
#undef ENEMY_WRITE
    b.enemyHp = v.hp; b.enemyMax = v.max; b.enemyShield = v.shield;
    b.hp = v.enemyHp; b.shield = v.enemyShield;
    if (v.poison <= 0 || v.poisonTurns <= 0) b.playerPoison = 0;
    else if (v.poisonDamage >= 0) b.playerPoison = v.poisonDamage;
    else if (oldPoison != v.poison || oldPoisonTurns != v.poisonTurns)
        b.playerPoison = mfLegacyPoisonTickFromLayers(mfBattleDamageBasisFromPower(b.enemyPower), v.poison);
    b.playerPoisonTurns = v.poisonTurns;
    b.enemyCd = v.cooldowns;
    // 治疗平减的是每跳伤害点数，毒层仍供引毒咒使用。把旧层数模式在动作边界
    // 转成精确缓存，禁止floor反推层数将清两点放大为清一整层。
    b.poisonDamage = v.playerPoison;
    if (b.poisonDamage == 0 || v.playerPoisonTurns == 0) {
        b.poisonDamage = 0; b.poison = 0; b.poisonTurns = 0;
    }
}
#undef ENEMY_FIELDS

// 用途：为敌方视角抽牌并重洗弃牌。
// 参与流程：敌方开局、抽牌效果及轮次推进。
// 使用方式：传游戏、可写敌方视角和请求数量，手牌最多八张。
// 返回值：无；修改敌方视角牌堆、洗牌次数与游戏 RNG。
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

// 用途：为敌方移除牌神通选择一张基础卡。
// 参与流程：敌方忘机咒合法性与实际执行。
// 使用方式：传敌方视角和待用神通，先选守势再选攻击。
// 返回值：可移除卡UID；不需目标或无目标时为空。
static string enemyRemoveTarget(const Battle& v, const SpellDef& spell) {
    if (!battleNeedsTarget(spell)) return "";
    for (const auto& wanted : {string("defend"), string("strike")})
        for (const auto* pile : {&v.hand, &v.draw, &v.discard})
            for (const auto& uid : *pile) {
                const Card* card = findCard(v, uid);
                if (card && card->spell == wanted) return uid;
            }
    return "";
}

// 用途：校验并执行敌方一张手牌或法宝。
// 参与流程：敌方计划预演与实际连招共用。
// 使用方式：传可选 RNG 游戏、敌方角色/视角、UID及目标攻防。
// 返回值：成功效果战报；拒绝时为空，成功修改敌方视角。
static string battleEnemyPlay(Game* rng, const Player& p, Battle& v,
                              const string& uid, double reduction, double counter) {
    const bool artifact = uid.rfind("artifact:", 0) == 0;
    auto held = std::find(v.hand.begin(), v.hand.end(), uid);
    if (!artifact && held == v.hand.end()) return "";
    const Card* card = artifact ? nullptr : findCard(v, uid);
    optional<ArtifactAbility> ability;
    const SpellDef* sp = battleResolveSpell(p, artifact ? uid : card ? card->spell : "", ability);
    if (v.played >= 24 || !sp || sp->stage > p.stage || battleCooldown(v, sp->id) > 0 ||
        v.mana < battleCost(p, *sp)) return "";
    const string target = enemyRemoveTarget(v, *sp);
    if (battleNeedsTarget(*sp) && target.empty()) return "";
    if (!target.empty()) mfExhaustOneCard(v, target);
    if (!artifact) v.hand.erase(std::find(v.hand.begin(), v.hand.end(), uid));
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
    if (!artifact) v.discard.push_back(uid);
    ++v.played;
    return text;
}

// 用途：只看当前手牌估计从一剑起能接多少种五行剑；流程：敌方起手选择；返回：1..5，不读抽牌堆、洗牌或RNG。
static int mfHeldElementalSwordPathLength(const Battle& State, const SpellDef& First) {
    const SpellDef* Current = &First;
    vector<string> Visited{First.id};
    for (int Step = 1; Step < 5; ++Step) {
        const SpellDef* Next = nullptr;
        for (const auto& Uid : State.hand) {
            const auto* Card = findCard(State, Uid);
            const auto* Candidate = Card ? battleSpell(Card->spell) : nullptr;
            if (!Candidate || !ElementalSword::mfIsElementalSword(*Candidate) ||
                std::find(Visited.begin(), Visited.end(), Candidate->id) != Visited.end()) continue;
            const auto From = content::parseDaoId(Current->dao), To = content::parseDaoId(Candidate->dao);
            if (From && To && content::generates(*From, *To)) { Next = Candidate; break; }
        }
        if (!Next) break;
        Visited.push_back(Next->id); Current = Next;
    }
    return static_cast<int>(Visited.size());
}
// 用途：按当前资源和状态为敌方候选神通评分。
// 参与流程：敌方连招顺序规划。
// 使用方式：传敌方视角与已验证可用的神通。
// 返回值：优先级整数；负数表示此刻不值得施放。
int mfBattleNpcCardPriority(const Player& Foe, const Battle& v, const SpellDef& sp) {
    const auto spellKind = content::parseSpellKind(sp.kind);
    if (spellKind == SpellKind::Heal) return v.hp < v.max * 0.75 ? 95 : -1;
    if (spellKind == SpellKind::FireSiphon) {
        if (v.burn <= 0 || v.burnTurns <= 0 || v.hp >= v.max * 0.75) return -1;
        return 96; // 仅在确有灼烧且明显受伤时以伤害机会换生存，满血不浪费火池。
    }
    if (spellKind == SpellKind::Shield || spellKind == SpellKind::Guard) return v.shield < v.max * 0.15 ? 62 : -1;
    if (spellKind == SpellKind::Mana || spellKind == SpellKind::Burst || spellKind == SpellKind::Float) return v.mana <= 2 ? 94 : -1;
    if (spellKind == SpellKind::Draw || spellKind == SpellKind::Redraw || spellKind == SpellKind::Exhaust) return v.hand.size() <= 3 ? 78 : 35;
    if (spellKind == SpellKind::Foresee) return 32;
    if (spellKind == SpellKind::Store) return v.mana >= 2 ? 20 : -1;
    if (spellKind == SpellKind::Flow) return v.hp > v.max * 0.6 ? 30 : -1;
    if (spellKind == SpellKind::Empower) return v.buff < 0.20 ? 80 : -1;
    if (spellKind == SpellKind::Ward) return v.reduction < 0.10 ? 65 : -1;
    if (spellKind == SpellKind::Water) return v.water < 4 ? 86 : 69;
    if (spellKind == SpellKind::Wave) return v.water > 0 ? 85 : 45;
    if (spellKind == SpellKind::Poison) return v.poison < 4 ? 86 : 70;
    if (spellKind == SpellKind::PoisonBurst) return v.poison > 0 ? 85 : 45;
    if (spellKind == SpellKind::Fire) return v.burn == 0 ? 86 : 77;
    if (spellKind == SpellKind::FireBurst) return v.burn > 0 ? 85 : 45;
    if (spellKind == SpellKind::Charge || spellKind == SpellKind::Soul) return 84;
    if (spellKind == SpellKind::Heavy || spellKind == SpellKind::BodyBurst) return v.charge > 0 ? 85 : 68;
    if (spellKind == SpellKind::SoulBurst) return v.soul > 0 ? 85 : 45;
    if (ElementalSword::mfIsElementalSword(sp)) {
        if (ElementalSword::mfHasGeneratingCombo(v, sp))
            return 90 + (talentMods(Foe).ElementalSwordMastery > 0 ? 2 : 0);
        const int VisiblePath = mfHeldElementalSwordPathLength(v, sp);
        // 有链但暂缺正确剑时先经营中性手牌/基础攻击；仍保留正分回退，不能无限空等。
        if (v.ElementalSwordSequence > 0) return 45 + VisiblePath;
        return 79 + VisiblePath;
    }
    if (spellKind == SpellKind::Sword || spellKind == SpellKind::Human) return 79 + std::min(3, v.swordSequence);
    return sp.id == "strike" ? 50 : 72;
}

// 用途：从此刻已在手中的合法神通选择下一步；流程：冻结前缀规划与重洗后的真实续招。
// 使用：传施术者视角，不查看未抽牌；返回：UID或空，不改变状态或RNG。
static string mfNextEnemyCard(const Player& Actor, const Battle& State, bool AllowArtifacts) {
    string Next;
    int Priority = -1;
    vector<string> Candidates = State.hand;
    if (AllowArtifacts)
        for (const auto& Active : equippedArtifactAbilities(Actor)) Candidates.push_back(Active.spell.id);
    for (const auto& Uid : Candidates) {
        const Card* Card = findCard(State, Uid);
        optional<ArtifactAbility> Ability;
        const SpellDef* Spell = battleResolveSpell(Actor, Card ? Card->spell : Uid, Ability);
        if (!Spell || Spell->stage > Actor.stage || battleCooldown(State, Spell->id) > 0 ||
            State.mana < battleCost(Actor, *Spell)) continue;
        if (battleNeedsTarget(*Spell) && enemyRemoveTarget(State, *Spell).empty()) continue;
        const int Score = mfBattleNpcCardPriority(Actor, State, *Spell);
        if (Score > Priority) { Next = Uid; Priority = Score; }
    }
    return Next;
}

// 敌方施术事实仅用于当前执行/只读副本，既不持久化也不提前承诺未抽到的牌。
struct EnemyActionFact {
    string SpellId, Text;
    int TargetDamage = 0, Blocked = 0;
};

// 用途：统一执行冻结前缀与普通修士跨重洗后的已知手牌续招；流程：真实敌方行动与意图副本。
// 使用：RNG必须属于真实动作或独立副本；仅当前缀真实触发重洗，普通修士才续至合法cap。
// 返回：逐步施术事实；Shadow沿用原固定前缀，不增加尾动作，不把未来弃牌UID写回计划。
static vector<EnemyActionFact> mfExecuteEnemyPrefix(Game& RandomState, const Player& Actor, Battle& State,
        const vector<string>& Prefix, int ActionLimit, bool AllowShuffleContinuation, double TargetReduction, double TargetCounter) {
    vector<EnemyActionFact> Facts;
    Facts.reserve(static_cast<std::size_t>(std::max(0, ActionLimit)));
    const int ShufflesBefore = State.shuffles;
    const auto Play = [&](const string& Uid) {
        const Card* Card = findCard(State, Uid);
        optional<ArtifactAbility> Ability;
        const SpellDef* Spell = battleResolveSpell(Actor, Card ? Card->spell : Uid, Ability);
        if (!Spell) return false;
        const string SpellId = Spell->id;
        const int HealthBefore = State.enemyHp, ShieldBefore = State.enemyShield;
        string Text = battleEnemyPlay(&RandomState, Actor, State, Uid, TargetReduction, TargetCounter);
        if (Text.empty()) return false;
        Facts.push_back({SpellId, std::move(Text), std::max(0, HealthBefore - State.enemyHp),
            std::max(0, ShieldBefore - State.enemyShield)});
        return true;
    };
    for (const auto& Uid : Prefix) {
        if (State.played >= ActionLimit || State.hp <= 0 || State.enemyHp <= 0) break;
        Play(Uid);
    }
    if (AllowShuffleContinuation && State.shuffles > ShufflesBefore) {
        while (State.played < ActionLimit && State.hp > 0 && State.enemyHp > 0) {
            const string Next = mfNextEnemyCard(Actor, State, false);
            if (Next.empty() || !Play(Next)) break;
        }
    }
    return Facts;
}

// 用途：为真实修士建立合法构筑、牌堆和资源。
// 参与流程：普通开局及旧修士战斗补建。
// 使用方式：传游戏、敌人规格和是否初始化开局属性。
// 返回值：无；修改 battle.enemyCards、敌人属性、牌堆及 RNG。
static void battleEnemyInit(Game& g, const EnemySpec& spec, bool opening) {
    Battle& b = *g.player.arts.battle;
    Npc npc;
    npc.name = spec.name; npc.stage = clampInt(spec.stage, 0, 12);
    npc.sect = spec.sect; npc.gender = spec.gender;
    npc.dao = spec.style.empty() ? "体" : spec.style;
    npc.beast = spec.beast || (spec.id.empty() && spec.sect.empty() &&
        (content::parseBattleKind(spec.kind) == BattleKind::Hunt || content::parseBattleKind(spec.kind) == BattleKind::Tide));
    bool actualNpc = false;
    for (auto& actual : g.npcs) {
        if (actual.id != spec.id) continue;
        if (opening) {
            const int used = npcConsumeUsefulPills(actual, true);
            if (used > 0) b.log.push_back(actual.name + "战前服用适用丹药 " + std::to_string(used) + " 枚。");
        }
        npc = actual; actualNpc = true;
        if (opening) {
            actual.pillState.combatPrep.clear();
            actual.pillState.combatBuff = 0;
        }
        break;
    }
    npc.stage = clampInt(npc.stage, 0, 12);
    b.enemyStage = npc.stage; b.enemySect = npc.sect; b.enemyGender = npc.gender;
    b.enemyPower = BATTLE_POWER[npc.stage];
    b.enemyBeast = npc.beast; b.enemyDao = npc.dao;
    EnemyCombat e;
    e.poisonDamage = 0;
    e.stage = npc.stage; e.sect = npc.sect; e.dao = npc.dao; e.body = npc.body;
    const Arts loadout = artNpcLoadout(npc);
    e.main = loadout.main; e.aux = loadout.aux;
    e.manuals = loadout.manuals; e.nodes = loadout.nodes; e.mind = loadout.mind;
    b.enemyCards = e;
    Player foe = battleEnemyPlayer(g, b);
    foe.arts = loadout;
    const Battle preparation = battleReferenceState(foe);
    b.enemySpeed = (spec.speed >= 0 ? spec.speed : battleSpeed(foe)) + preparation.speed;
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
    v.maxMana = preparation.maxMana;
    v.mana = preparation.mana;
    const auto shieldPill = loadout.combatPrep.find("shield");
    const double pillShield = shieldPill == loadout.combatPrep.end() ? 0.0 : shieldPill->second;
    // A fast NPC historically starts with its round shield. Keep that baseline,
    // but let a consumed shield pill survive until the player can respond.
    const double openingShield = b.playerFirst
        ? mods.openingShield + (artHas(foe.arts, "阵", 1) ? 0.12 : 0.0) : 0.0;
    v.shield = std::min(jsRound(v.max * 0.30),
        jsRound(v.max * (openingShield + mods.roundShield)) + jsRound(v.max * pillShield));
    v.swordQi = preparation.swordQi;
    if (opening) {
        v.buff = preparation.buff;
        v.reduction = preparation.reduction;
    }
    if (!opening && b.playerPoisonTurns > 0) {
        v.poisonTurns = b.playerPoisonTurns;
        v.poison = clampInt(jsRound(b.playerPoison /
                                   (mfBattleDamageBasisFromPower(b.enemyPower) * LegacyPoisonTickBasisCoefficient)), 1, BATTLE_POISON_CAP);
        v.poisonDamage = b.playerPoison;
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

// 用途：由双方不可变快照创建独立影身对局。
// 参与流程：天机影身遭遇初始化。
// 使用方式：传独立游戏、双方快照和种子，不触碰原账号状态。
// 返回值：无；初始化 encounter 的角色、敌方牌组和战报。
void battleStartShadow(Game& encounter, const Player& challengerSnapshot,
                       const Player& opponentSnapshot, uint32_t seed) {
    // Copy before clearing the encounter, even if a caller passed its own player.
    Player challenger = challengerSnapshot, opponent = opponentSnapshot;
    challenger.arts.battle.reset(); opponent.arts.battle.reset();
    encounter = Game{};
    encounter.seed = encounter.rng = seed;
    encounter.player = std::move(challenger);
    encounter.clock.ageBase = encounter.player.age;
    Battle b = battleReferenceState(encounter.player);
    b.shadowPlayer = std::make_shared<const Player>(std::move(opponent));
    const Player& foe = *b.shadowPlayer;
    Battle v = battleReferenceState(foe);
    b.kind = "tianji";
    b.poisonDamage = v.poisonDamage = 0;
    b.enemy = foe.name; b.enemyStage = foe.stage;
    b.enemySect = foe.sect; b.enemyGender = foe.gender;
    b.enemyPower = BATTLE_POWER[clampInt(foe.stage, 0, 12)];
    b.enemySword = foe.sword; b.enemyDao = foe.arts.focus;
    b.style = foe.arts.focus;
    b.enemySwordCult = artHas(foe.arts, "剑", 1);
    b.enemyMax = v.max; b.enemyHp = v.hp; b.enemyShield = v.shield;
    b.enemySpeed = std::max(1, battleSpeed(foe) + v.speed);
    b.playerSpeed = std::max(1, battleSpeed(encounter.player) + b.speed);
    b.playerFirst = b.playerSpeed >= b.enemySpeed;
    EnemyCombat& e = b.enemyCards;
    e.enabled = true; e.stage = foe.stage; e.speed = b.enemySpeed;
    e.body = foe.body; e.mind = foe.arts.mind; e.sect = foe.sect;
    e.dao = foe.arts.focus; e.main = foe.arts.main; e.aux = foe.arts.aux;
    e.manuals = foe.arts.manuals; e.nodes = foe.arts.nodes;
    v.enemyHp = b.hp; v.enemyMax = b.max; v.enemyShield = b.shield;
    v.deck = battlePlayerDeck(foe, "enemy-card-");
    for (const auto& card : v.deck) {
        v.draw.push_back(card.uid);
        if (card.spell != "strike" && card.spell != "defend" &&
            std::find(b.enemySpells.begin(), b.enemySpells.end(), card.spell) == b.enemySpells.end())
            b.enemySpells.push_back(card.spell);
    }
    battleShuffle(encounter, v.draw); battleEnemyDraw(encounter, v, 5);
    battleSaveEnemyView(b, v, encounter.player.stage);
    b.log.push_back("天机大比留影切磋：独立气血与牌组，最多24回合，回合耗尽为平局。");
    encounter.player.arts.battle = std::move(b);
    battleInitCards(encounter, false);
    encounter.player.arts.combatPrep.clear(); encounter.player.arts.combatBuff = 0;
    Battle& started = *encounter.player.arts.battle;
    if (!started.playerFirst) {
        started.log.push_back("对方遁速更快，抢先施放首轮神通。");
        battleEnemyAct(encounter);
        if (started.hp <= 0 || started.enemyHp <= 0) {
            battleFinish(encounter, started.hp > 0);
            return;
        }
        battlePlan(encounter);
    } else started.log.push_back("你的遁速取得先手，先行出牌。");
}

// 用途：统一敌方每轮开始时的护盾与守势重置。
// 参与流程：只读预览、连招规划和真实行动。
// 使用方式：传主战斗、敌方加成和敌方视角；先手首轮沿用开局状态。
// 返回值：无；后续轮次只保留每轮再生护盾，不重复发放开局丹药。
static void battleEnemyBeginTurn(const Battle& b, const ArtMods& mods, Battle& v) {
    if (!b.playerFirst && b.enemyCards.turns == 0) return;
    v.shield = jsRound(v.max * std::min(0.30, mods.roundShield));
    v.guard = 0;
}

// 用途：只读试算回合末持续状态及敌方完整连招。
// 参与流程：战况意图展示。
// 使用方式：传游戏，在战斗与RNG副本上执行前缀；局部重洗后只根据已到手牌续招，不提交活动RNG。
// 返回值：预计伤害、护盾、治疗、招式与说明，不消费 RNG。
EnemyPreview battleEnemyPreview(const Game& g) {
    EnemyPreview out;
    if (!g.player.arts.battle) return out;
    const Battle& b = *g.player.arts.battle;
    if (!b.shadowOutcome.empty()) { out.description = "天机切磋已经结束。"; return out; }
    Battle projected = b;
    battleDiscard(g.player, projected, static_cast<int>(projected.hand.size()), DiscardRules::Timing::RoundEnd);
    battleTickStatuses(g.player, projected, false);
    if (projected.enemyHp <= 0 || projected.hp <= 0) {
        for (const auto& Action : b.BeastActions) out.BeastActions.push_back({Action});
        out.damage = std::max(0, b.hp - projected.hp);
        out.description = projected.enemyHp <= 0
            ? "持续伤害将在敌方行动前击败对手。" : "毒与灼烧将在敌方行动前耗尽气血。";
        return out;
    }
    if (b.shadowPlayer && !b.playerFirst && b.enemyCards.turns >= 24) {
        out.damage = std::max(0, b.hp - projected.hp);
        out.description = "双方各完成24回合后仍存活，切磋将判为平局。";
        return out;
    }
    if (!b.enemyCards.enabled) {
        if (!b.BeastActions.empty()) {
            const auto Result = BeastCombat::mfExecuteActions(g.player, projected, false);
            out.damage = std::max(0, b.hp - projected.hp);
            out.shield = Result.Shield; out.heal = Result.Heal;
            out.description = Result.Description; out.BeastActions = Result.Actions;
            if (b.hp > projected.hp + Result.Damage)
                out.description = "毒与灼烧：伤害" + std::to_string(b.hp - projected.hp - Result.Damage) + "。 " + out.description;
            return out;
        }
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
    const ArtMods mods = b.shadowPlayer ? talentMods(foe) : artModsOf(foe.arts);
    Battle v = battleEnemyView(projected, g.player.stage);
    battleEnemyBeginTurn(b, mods, v);
    const int before = v.hp;
    Game simulation; simulation.rng = g.rng;
    const auto Facts = mfExecuteEnemyPrefix(simulation, foe, v, b.enemyCards.planned,
        mfBattleEnemyActionLimit(g, b), !b.shadowPlayer, battleReduction(g.player, b), playerMods.bodyCounter);
    for (const auto& Fact : Facts) {
        out.spells.push_back(Fact.SpellId);
        if (!out.description.empty()) out.description += " ";
        out.description += Fact.Text;
    }
    if (v.hp > 0 && v.enemyHp > 0) {
        battleDiscard(foe, v, static_cast<int>(v.hand.size()), DiscardRules::Timing::RoundEnd);
        if (!v.attacked && mods.quietHeal > 0)
            v.hp = std::min(v.max, v.hp + jsRound(v.max * std::min(0.05, mods.quietHeal)));
        if (artHas(foe.arts, "木", 2))
            v.hp = std::min(v.max, v.hp + jsRound(v.max * 0.01));
        if (b.shadowPlayer && hasTalent(foe, "dragonBlood"))
            v.hp = std::min(v.max, v.hp + jsRound(v.max * 0.03));
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

// 用途：从敌方已抽到的牌和可用法宝规划本轮连招。
// 参与流程：真实修士开局及每轮敌方计划。
// 使用方式：传游戏，以优先级和实际费用在副本上选择行动。
// 返回值：无；保存敌方计划及对玩家可见的意图。
static void battleEnemyPlan(Game& g) {
    Battle& b = *g.player.arts.battle;
    EnemyCombat& e = b.enemyCards;
    const Player foe = battleEnemyPlayer(g, b);
    const ArtMods playerMods = talentMods(g.player);
    const ArtMods mods = b.shadowPlayer ? talentMods(foe) : artModsOf(foe.arts);
    Battle v = battleEnemyView(b, g.player.stage);
    battleEnemyBeginTurn(b, mods, v);
    e.planned.clear();
    Game simulation; simulation.rng = g.rng;
    const int limit = mfBattleEnemyActionLimit(g, b);
    for (int played = 0; played < limit; ++played) {
        const string next = mfNextEnemyCard(foe, v, static_cast<bool>(b.shadowPlayer));
        if (next.empty()) break;
        if (battleEnemyPlay(b.shadowPlayer ? &simulation : nullptr, foe, v, next, battleReduction(g.player, b), playerMods.bodyCounter).empty()) break;
        e.planned.push_back(next);
        if (v.hp <= 0 || v.enemyHp <= 0) break;
    }
    const EnemyPreview preview = battleEnemyPreview(g);
    b.intentKind = "cards"; b.intentName.clear(); b.intentSpell.clear();
    for (const auto& id : preview.spells) {
        if (!b.intentName.empty()) b.intentName += " → ";
        optional<ArtifactAbility> ability;
        const SpellDef* spell = battleResolveSpell(foe, id, ability);
        b.intentName += spell ? spell->name : id;
        if (b.intentSpell.empty()) b.intentSpell = id;
    }
    if (b.intentName.empty()) b.intentName = "调息";
    b.intentScale = 0; b.intentRaw = preview.damage;
}

// 用途：执行已经规划的敌方连招并推进敌方资源。
// 参与流程：玩家结束回合或敌方先手。
// 使用方式：传游戏，逐张执行并检查死亡，最后弃牌回灵和抽牌。
// 返回值：无；修改双方战斗状态、敌方下一轮资源及 RNG。
static void battleEnemyAct(Game& g) {
    Battle& b = *g.player.arts.battle;
    if (!b.enemyCards.enabled) return;
    const Player foe = battleEnemyPlayer(g, b);
    const ArtMods mods = b.shadowPlayer ? talentMods(foe) : artModsOf(foe.arts);
    const ArtMods playerMods = talentMods(g.player);
    Battle v = battleEnemyView(b, g.player.stage);
    battleEnemyBeginTurn(b, mods, v);
    const auto Facts = mfExecuteEnemyPrefix(g, foe, v, b.enemyCards.planned,
        mfBattleEnemyActionLimit(g, b), !b.shadowPlayer, battleReduction(g.player, b), playerMods.bodyCounter);
    for (const auto& Fact : Facts) {
        b.log.push_back("对方施放" + Fact.Text);
        if (Fact.TargetDamage > 0)
            b.log.push_back("你受到" + std::to_string(Fact.TargetDamage) + "伤害，护盾格挡" +
                            std::to_string(Fact.Blocked) + "。");
    }
    if (v.hp <= 0 || v.enemyHp <= 0) {
        battleSaveEnemyView(b, v, g.player.stage);
        return;
    }
    battleDiscard(foe, v, static_cast<int>(v.hand.size()), DiscardRules::Timing::RoundEnd);
    v.discard.insert(v.discard.end(), v.hand.begin(), v.hand.end()); v.hand.clear();
    for (auto& [id, cd] : v.cooldowns) cd = std::max(0, cd - 1);
    v.mana = battleNextMana(foe, v); v.carryMana = 0;
    if (v.lock > 0) --v.lock;
    if (!v.attacked && mods.quietHeal > 0)
        v.hp = std::min(v.max, v.hp + jsRound(v.max * std::min(0.05, mods.quietHeal)));
    if (artHas(foe.arts, "木", 2))
        v.hp = std::min(v.max, v.hp + jsRound(v.max * 0.01));
    if (b.shadowPlayer && hasTalent(foe, "dragonBlood")) {
        const int recovered = std::min(v.max - v.hp, jsRound(v.max * 0.03));
        v.hp += recovered;
        if (recovered > 0) b.log.push_back("对方龙族血脉：恢复" + std::to_string(recovered) + "气血。");
    }
    ++b.enemyCards.turns;
    const int draws = 5 + v.drawNext; v.drawNext = 0;
    v.played = 0; v.attacked = false; v.swordSequence = 0; v.comboDraws = 0; v.ElementalSwordManaReturns = 0;
    battleEnemyDraw(g, v, draws);
    battleSaveEnemyView(b, v, g.player.stage);
}

// ------------------------------------------------------------
// 出牌预览（对应 battlePreview）
// ------------------------------------------------------------
// 用途：在战斗副本上计算一张定义的实际效果差量。
// 参与流程：实战与场外神通预览底层。
// 使用方式：传角色、当前战斗与定义；直接共用 battleEffect。
// 返回值：伤害、费用、回灵增量、状态和文案等 Preview。
static Preview battlePreviewSpell(const Player& player, const Battle& battle, const SpellDef& spell) {
    Preview out;
    if (!content::parseSpellKind(spell.kind) || !content::parseDaoId(spell.dao)) return out;
    const string& id = spell.id;
    // 在副本上试算，真实状态不受影响
    Battle copy = battle;
    for (auto it = copy.hand.begin(); it != copy.hand.end(); ++it) {
        const Card* card = findCard(copy, *it);
        if (card && card->spell == id) { copy.hand.erase(it); break; }
    }
    copy.pendingDraw = 0;   // 试算不抽牌
    const ArtMods targetMods = battle.enemyCards.enabled ? battleEnemyMods(battle) : ArtMods{};
    const double reduction = battle.enemyCards.enabled
        ? targetMods.reduction + battle.enemyCards.reduction + battle.enemyCards.guard : 0;
    const string text = battleEffect(player, copy, spell, reduction, targetMods.bodyCounter, &out.RawDamage, &out.ElementalGenerationReady);

    out.valid = true;
    out.spellName = spell.name;
    out.cost = battleCost(player, spell);
    out.damage = std::max(0, battle.enemyHp - copy.enemyHp);
    out.shield = copy.shield - battle.shield;
    out.heal = copy.hp - battle.hp;
    out.energy = copy.mana - battle.mana;
    out.qi = copy.swordQi - battle.swordQi;
    out.manaGain = std::max(0, out.energy + out.cost);
    out.draw = copy.pendingDraw;
    out.drawNext = copy.drawNext - battle.drawNext;
    out.hpMaxChange = copy.max - battle.max;
    out.manaRegenDelta = (copy.manaBonus - copy.manaPenalty) - (battle.manaBonus - battle.manaPenalty);
    out.storedMana = copy.carryMana;
    out.damageBonusPct = (copy.buff - battle.buff) * 100;
    out.reductionPct = (copy.reduction - battle.reduction) * 100;
    out.guardPct = (copy.guard - battle.guard) * 100;
    out.healPotential = std::max(0, out.heal);
    if (content::parseSpellKind(spell.kind) == SpellKind::FireSiphon)
        out.healPotential = FireGrowth::mfExplosionAmount(talentMods(player), battle.FireGrowthRounds, battle.burn, battle.burnTurns);
    if (content::parseSpellKind(spell.kind) == SpellKind::Heal) {
        Battle wounded = battle;
        wounded.hp = 1;
        battleEffect(player, wounded, spell, reduction, targetMods.bodyCounter);
        out.healPotential = wounded.hp - 1;
    }
    const auto status = [&](const char* key, const char* name, const char* target,
                            int before, int total, int turns, int cap, int tick = 0) {
        if (total != before || turns > 0)
            out.statuses.push_back({key, name, target, total - before, total, turns, cap, tick});
    };
    status("qi", "剑气", "self", battle.swordQi, copy.swordQi, 0, 8);
    status("charge", "蓄势", "self", battle.charge, copy.charge, 0, 4);
    status("water", "水势", "self", battle.water, copy.water, 0, 6);
    status("soul", "震荡", "self", battle.soul, copy.soul, 0, 6);
    const double DamageBasis = mfBattleDamageBasis(player.stage);
    status("poison", "毒", "enemy", battle.poison, copy.poison, copy.poisonTurns,
           BATTLE_POISON_CAP, copy.poisonTurns > 0 ? (copy.poisonDamage >= 0 ? copy.poisonDamage :
               mfLegacyPoisonTickFromLayers(DamageBasis, copy.poison)) : 0);
    status("burn", "灼烧", "enemy", battle.burn, copy.burn, copy.burnTurns,
           BATTLE_BURN_CAP, copy.burnTurns > 0 ? copy.burn : 0);
    out.text = text;
    return out;
}

// 用途：解析已满足境界的神通或法宝并预览。
// 参与流程：实战手牌与法宝侧栏。
// 使用方式：传角色、战斗和施术 ID，不写真实状态。
// 返回值：实际效果预览；未知或境界不足时无效。
Preview battlePreview(const Player& p, const Battle& b, const string& id) {
    optional<ArtifactAbility> ability;
    const SpellDef* spell = battleResolveSpell(p, id, ability);
    if (!spell || p.stage < spell->stage) return {};
    return battlePreviewSpell(p, b, *spell);
}

// 用途：在统一开局环境中给出目录参考效果。
// 参与流程：神通目录、法宝目录与炼器预览。
// 使用方式：传角色和定义；示例使用其余四张手牌，不抽牌。
// 返回值：参考效果预览，附治疗潜力等场外说明。
Preview battleReferencePreview(const Player& player, const SpellDef& spell) {
    Battle referenceBattle = battleReferenceState(player);
    // 示例出牌后其余四张手牌；不假定真实抽到的卡牌类型，不触发 RNG。
    for (int i = 0; i < 4; ++i) referenceBattle.hand.push_back("reference-" + std::to_string(i));
    Preview out = battlePreviewSpell(player, referenceBattle, spell);
    // 实战 preview.text 保持与 battleEffect 日志一致。目录额外说明取自
    // 同一试算的结构化结果，避免满血治疗量或持续伤害在场外不可见。
    if (content::parseSpellKind(spell.kind) == SpellKind::Heal)
        out.text += "单次治疗量" + std::to_string(out.healPotential) + "，当前实际可回复" + std::to_string(std::max(0, out.heal)) + "。";
    if (out.guardPct > 0) out.text += "本回合额外减伤+" + std::to_string(jsRound(out.guardPct)) + "% 。";
    if (content::parseSpellKind(spell.kind) == SpellKind::Poison)
        for (const auto& status : out.statuses) if (status.id == "poison")
            out.text += "当前毒每回合造成" + std::to_string(status.perTurn) + "伤害。";
    return out;
}

// 用途：解释蓄积、相生和抽牌等影响预览的条件。
// 参与流程：场外参考数值的条件说明。
// 使用方式：传角色和定义，从当前功法系数产生说明。
// 返回值：对应条件文案；无特定条件时为空。
string battlePreviewConditions(const Player& player, const SpellDef& spell) {
    const ArtMods modifiers = talentMods(player);
    const auto spellKind = content::parseSpellKind(spell.kind);
    const auto daoId = content::parseDaoId(spell.dao);
    if (!spellKind || !daoId) return "";
    string text;
    if (spellKind == SpellKind::Sword || spellKind == SpellKind::Human)
        text = "每层剑气伤害+7%，连续剑招每次额外+" + std::to_string(jsRound(modifiers.swordCombo * 100)) + "%（最多50%）；每第三剑回复" + std::to_string(jsFloor(modifiers.swordMana)) + "灵力。";
    if (spellKind == SpellKind::Heavy)
        text = "每层蓄势伤害+12%，消耗所有蓄势；额外伤害为当前护盾×" + std::to_string(jsRound(modifiers.shieldStrike * 100)) + "% 。";
    if (spellKind == SpellKind::Wave)
        text = "每层水势伤害+" + std::to_string(jsRound((0.15 + std::min(0.1, modifiers.water * 0.025)) * 100)) + "%；保留" + std::to_string(jsRound(std::min(0.50, modifiers.waterKeep) * 100)) + "%水势（向下取整），返灵取决于消耗水势。";
    if (spellKind == SpellKind::PoisonBurst) {
        std::ostringstream Formula;
        Formula << "毒爆追加伤害=(当前基础战力" << BATTLE_POWER[clampInt(player.stage, 0, 12)]
                << "×" << BattleDamagePowerMultiplier << "+" << BattleDamageFlatBasis
                << ")×18%×毒层；所有毒层合计后四舍五入，消耗全部毒层。";
        text = Formula.str();
    }
    if (spellKind == SpellKind::FireBurst || spellKind == SpellKind::FireSiphon)
        text = string(spellKind == SpellKind::FireSiphon ? "转换治疗量" : "引爆追加伤害") + "＝现有灼烧每回合伤害×剩余回合×倍率；开局倍率" +
            std::to_string(jsRound(FireGrowth::mfExplosionMultiplier(modifiers, 0) * 100)) + "%" +
            (modifiers.BurnGrowthExplosionPerRound > 0 ? "，随本场实际灼烧结算逐轮成长，至多" + std::to_string(FireGrowth::MaximumGrowthRounds) + "轮" : "") + "；消耗全部灼烧。";
    if (spellKind == SpellKind::FireSiphon) text += "无灼烧时治疗为0；最多回复到气血上限，不吃直伤、相生或治疗乘区。";
    if (spellKind == SpellKind::SoulBurst)
        text = "消耗震荡，每层增伤" + std::to_string(jsRound(SoulBurstPerLayerBonus * 100)) + "%；每层让该次伤害的" +
            std::to_string(jsRound(SoulBurstPiercePerLayer * 100)) + "%穿盾，最多" + std::to_string(jsRound(SoulBurstMaximumPierceFraction * 100)) + "%；穿盾部分仍计目标减伤。";
    if (ElementalSword::mfIsElementalSword(spell))
        text += ElementalSword::mfRulesDescription(spell, modifiers);
    if (spellKind == SpellKind::BodyBurst) text = "出牌前灵力至少3时伤害+15%，每层蓄势伤害+10%，消耗全部蓄势。";
    if (spellKind == SpellKind::Human) text += "消耗全部剑气与当前8%气血，气血最低保留1。";
    if (spellKind == SpellKind::Redraw) text = "数字以出牌后剩余4张手牌为示例；实际弃置和抽牌数随手牌变化。";
    if (spellKind == SpellKind::Draw || spellKind == SpellKind::Foresee || spellKind == SpellKind::Redraw || spellKind == SpellKind::Exhaust)
        text += "抽牌还受手牌8张上限与本场牌组剩余数量限制。";
    if (spellKind == SpellKind::Store) text = "封存量随扣费后剩余灵力变化，本回合灵力清零。";
    if (spellKind == SpellKind::Heal) text = "单次治疗量受气血上限、治疗功法和悟道影响；实际回复不超过缺失气血。";
    if (daoId == DaoId::Metal || daoId == DaoId::Water || daoId == DaoId::Wood || daoId == DaoId::Fire || daoId == DaoId::Earth)
        text += "符合五行相生时直伤倍率为" + std::to_string(jsRound((1.15 + std::min(0.15, modifiers.combo)) * 100)) + "%；此处按未触发相生显示。";
    return text;
}

// ------------------------------------------------------------
// 敌方来伤（对应 battleIncoming）
// ------------------------------------------------------------
// 用途：计算简化意图对玩家的真实气血损失。
// 参与流程：妖兽和旧敌方回合行动。
// 使用方式：传双方战斗状态，blocked 输出护盾吸收量。
// 返回值：减伤、穿盾和护盾之后的气血损失。
static int battleIncomingLoss(const Player& p, const Battle& b, int& blocked) {
    const int damage = jsRound(b.intentRaw * (1 - battleReduction(p, b)));
    const int pierce = (b.intentKind == "pierce") ? jsRound(damage * 0.4) : 0;
    blocked = std::min(b.shield, damage - pierce);
    return damage - blocked;
}

// ------------------------------------------------------------
// 结束回合（对应 battleEndTurn）
// ------------------------------------------------------------
// 用途：执行完整回合末与下一轮初始化。
// 参与流程：battleTurn 的 end 分支。
// 使用方式：已有未结束战斗时调用，按持续伤害、敌人、资源顺序处理。
// 返回值：无；修改战斗牌堆、气血、回灵总量、状态、日志与 RNG。
static void battleEndTurn(Game& g) {
    Player& p = g.player;
    Battle& b = *p.arts.battle;
    const ArtMods m = talentMods(p);

    // 1) 弃掉所有手牌
    const int DiscardedCardCount = static_cast<int>(b.hand.size());
    const int ShieldBeforeDiscard = b.shield;
    battleDiscard(p, b, static_cast<int>(b.hand.size()), DiscardRules::Timing::RoundEnd);
    const int DiscardShieldGain = b.shield - ShieldBeforeDiscard;
    if (DiscardShieldGain > 0) {
        b.log.push_back("回合末弃置" + std::to_string(DiscardedCardCount) + "张手牌：弃牌凝盾+" +
                        std::to_string(DiscardShieldGain) + "，用于抵挡本回合敌方攻击，下一回合清零。");
    }
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

    // A faster shadow already took the first phase; never grant a 25th phase.
    if (b.shadowPlayer && !b.playerFirst && b.enemyCards.turns >= 24) {
        b.turn = 24;
        battleFinish(g, false);
        return;
    }

    // 3) 敌方按已展示的意图行动
    if (b.enemyCards.enabled) {
        battleEnemyAct(g);
    } else if (!b.BeastActions.empty()) {
        BeastCombat::mfExecuteActions(p, b, true);
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
            const int tick = jsRound(mfBattleDamageBasisFromPower(b.enemyPower) * 0.05);
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
        b.log.push_back("灵力恢复到" + std::to_string(b.mana) + "点：基础" + std::to_string(baseMana) + " + 封存" +
                        std::to_string(carried) + " + 静息" + std::to_string(quietMana) +
                        "（受容量限制）。");
    if (b.lock > 0) b.lock--;

    b.guard = 0.0;
    b.attacked = false;
    b.played = 0;
    b.swordSequence = 0;
    b.comboDraws = 0;
    b.ElementalSwordManaReturns = 0;
    // 回合护盾功法在下一回合重新产生
    b.shield = std::min(jsRound(b.max * 0.3), jsRound(b.max * m.roundShield));

    // 8) 抽牌：基础 5 张 + 上回合「预谋」攒下的额外张数
    const int extra = b.drawNext;
    b.drawNext = 0;
    battleDraw(g, 5 + extra + dungeonRelicValue(g, true));
    battlePlan(g);

    if (!b.shadowPlayer && b.log.size() > 12)
        b.log.erase(b.log.begin(), b.log.begin() + (b.log.size() - 12));
}

// ------------------------------------------------------------
// 行动入口（对应 battleTurn）
// ------------------------------------------------------------
// 用途：校验并执行出牌、结束回合或撤离。
// 参与流程：斗法动作公共入口。
// 使用方式：传游戏、手牌 UID/神通 ID/end/escape 与可选移除目标。
// 返回值：操作成功或拒绝原因；成功更新战斗并可能触发结算。
static TurnResult executeBattleTurn(Game& g, const string& id, const string& target) {
    TurnResult out;
    Player& p = g.player;
    if (!p.arts.battle.has_value()) {
        out.ok = false;
        out.error = "当前没有斗法。";
        return out;
    }
    Battle& b = *p.arts.battle;
    if (!b.shadowOutcome.empty()) return {false, "这场天机切磋已经结束。"};

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

    string TargetUid, TargetName;
    if (battleNeedsTarget(*spell)) {
        if (target.empty()) return fail("请选择本场要移除的一张卡。");
        TargetUid = mfResolveExhaustTargetUid(b, *spell, target);
        if (TargetUid.empty()) return fail("目标卡已移除、不在本场牌堆中，或属于正在施放的神通。");
        const auto* TargetCard = findCard(b, TargetUid);
        const auto* TargetSpell = TargetCard ? battleSpell(TargetCard->spell) : nullptr;
        if (!TargetSpell) return fail("目标卡的神通定义无效。");
        TargetName = TargetSpell->name;
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
    if (!TargetUid.empty()) mfExhaustOneCard(b, TargetUid);
    if (!artifact) b.hand.erase(std::find(b.hand.begin(), b.hand.end(), uid));
    b.pendingDraw = 0;
    const ArtMods targetMods = b.enemyCards.enabled ? battleEnemyMods(b) : ArtMods{};
    const double reduction = b.enemyCards.enabled
        ? targetMods.reduction + b.enemyCards.reduction + b.enemyCards.guard : 0;
    string text = battleEffect(p, b, *spell, reduction, targetMods.bodyCounter);
    if (!TargetUid.empty()) text += "移除一张：" + TargetName + "（" + TargetUid + "）。";

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
    if (content::parseSpellKind(spell->kind) == SpellKind::Human) {
        extra = " 剑气" + std::to_string(qiBefore) + "（伤害+" +
                std::to_string(qiBefore * 7) + "%）→" +
                std::to_string(b.swordQi) +
                (content::parseSpellKind(spell->kind) == SpellKind::Human ? "，消耗全部剑气与当前8%气血" : "");
    }
    b.log.push_back(text + extra);
    if (!b.shadowPlayer && b.log.size() > 12)
        b.log.erase(b.log.begin(), b.log.begin() + (b.log.size() - 12));

    if (b.hp <= 0) battleFinish(g, false);
    else if (b.enemyHp <= 0) battleFinish(g, true);
    return out;
}

// 用途：兽潮动作收口记日；参与出牌击杀、完整回合、撤离；传合法动作，返回原动作结果。
// 先完成战果登记再推进一天，避免末日漏分；推进前释放Battle引用，普通/天机战不耗此时间。
TurnResult battleTurn(Game& game, const string& id, const string& target) {
    const int previousDay=game.clock.days;
    bool tideBattle = false;
    string eventId;
    if (game.player.arts.battle) {
        const auto& battle = *game.player.arts.battle;
        tideBattle = !battle.shadowPlayer && content::parseBattleKind(battle.kind) == BattleKind::Tide;
        eventId = battle.tideEventId;
    }
    const TurnResult result = executeBattleTurn(game, id, target);
    if (!result.ok || !tideBattle) return result;
    if (game.clock.days!=previousDay) return result; // 致命收口已记本日，不能重复扣天。
    const bool endedBattle = !game.player.arts.battle;
    if (id != "end" && id != "escape" && !endedBattle) return result;
    if (game.player.arts.battle) ++game.player.arts.battle->tideDaysCharged;
    calendarBattleDay(game);
    // 到期只撤出仍未完成的本期对局；已击杀对局已先记分，不会被该分支抹去。
    if (game.player.arts.battle && game.player.arts.battle->tideEventId == eventId &&
        eventId == game.tide.eventId && game.clock.days >= game.tide.endDay) {
        game.player.arts.battle->log.push_back("兽潮战期结束，守军鸣金收兵。");
        battleFinish(game, false, true);
    }
    tide::settleIfDue(game);
    return result;
}

// 用途：把当前手牌 UID 映射为神通 ID 列表。
// 参与流程：界面手牌序列化。
// 使用方式：传战斗，保留手牌位置顺序。
// 返回值：与手牌逐项对应的神通 ID，找不到牌时为空项。
vector<string> handSpells(const Battle& b) {
    vector<string> out;
    for (const auto& uid : b.hand) {
        const Card* c = findCard(b, uid);
        out.push_back(c ? c->spell : string());
    }
    return out;
}

} // namespace nz

