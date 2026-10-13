// 宁州一梦 · C++ 版 — 玩家可直接操作的行动（实现）
#include "core/play.h"
#include "core/renown.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <limits>

#include "core/arts.h"
#include "core/economy.h"
#include "core/friendship.h"
#include "core/battle.h"
#include "core/shop.h"
#include "core/crafting/craft_prices.h"
#include "core/source_limits.h"
#include "core/sim.h"
#include "core/talents.h"
#include "core/sect_trial.h"
#include "core/sword.h"
#include "data/reference_tables.h"

namespace nz::play {

using std::string;
using std::vector;

namespace {

// 行情系数：坊市买东西按行情浮动，1.0 是平常价
// 用途：把世界行情折算为交易系数。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
double marketMul(const Game& g) {
    return 1.0 + (g.world.market - 1.0) * 0.5;
}

// 用途：计入行情与声望加成后的卖价。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, int base，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int sellPriceOf(const Game& g, int base) {
    // 反过来，名声让你卖得更贵
    const double bonus = 1.0 + renown::sellBonus(g.player.renown);
    return std::max(1, jsRound(base * 0.55 * marketMul(g) * bonus));
}

// 用途：按完整配方材料市场价确定成品买价。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const data::RecipeDef& recipe，按本模块入口调用。
// 返回值：说明所述数值结果；不额外消耗资源。
int potionBuyPrice(const Game& g, const data::RecipeDef& recipe) {
    const auto Quote = crafting::mfQuotePotionPrice(g, recipe.id);
    return Quote.Valid ? Quote.BuyPrice : 0;
}


} // namespace

// 用途：复用既有买价公式；流程：普通坊市、兽潮报价与成交；用法：传药材/器材稳定 ID；返回：正常单价，未知为零。
int normalMaterialBuyPrice(const Game& g, const string& kind, const string& id) {
    if (kind == "herb") return crafting::mfNormalMaterialBuyPrice(g, crafting::MaterialPriceKind::Herb, id);
    if (kind == "material") return crafting::mfNormalMaterialBuyPrice(g, crafting::MaterialPriceKind::ForgeMaterial, id);
    return 0;
}

// ============================================================
// 坊市
// ============================================================
// 用途：列出当前城池实际可售的丹药草药器材。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<MarketItem> marketList(const Game& g) {
    vector<MarketItem> out;
    if (!ecoIsCity(g.player.location)) return out;

    const Arts& a = g.player.arts;

    // --- 丹药：已会炼的丹方可以买到成品 ---
    for (const auto& r : data::RecipeDef_TABLE()) {
        // 只卖低阶丹，高阶要自己炼
        if (r.grade > 3) continue;
        MarketItem m;
        m.kind = "potion";
        m.id = r.id;
        m.name = r.name;
        m.desc = artPillUseInfo(g.player, r).effect;
        m.dao = r.dao;
        m.price = potionBuyPrice(g, r);
        if (m.price <= 0) continue;
        m.sellPrice = shop::salePrice(g, "potion", r.id);
        auto it = a.potions.find(r.id);
        m.owned = (it == a.potions.end()) ? 0 : it->second;
        out.push_back(m);
    }

    // --- 药材：本地出产的便宜 ---
    for (const auto& h : data::HerbDef_TABLE()) {
        if (!ordinaryHerbError(g.player, h.grade).empty()) continue;
        MarketItem m;
        m.kind = "herb";
        m.id = h.id;
        m.name = h.name;
        m.desc = std::to_string(h.grade) + "品药材" + (h.main.empty() ? "" : (" · 主" + h.main));
        // 本地药材打折
        const int base = h.sourcePrice > 0 ? h.sourcePrice : 20 + h.grade * 15;
        m.price = normalMaterialBuyPrice(g, "herb", h.id);
        if (m.price <= 0) continue;
        m.sellPrice = sellPriceOf(g, base);
        auto it = a.herbs.find(h.name);
        m.owned = (it == a.herbs.end()) ? 0 : it->second;
        out.push_back(m);
    }
    // 普通材料铺只供一至二阶资材。三阶以上必须去秘境寻找。
    for (const auto& material : data::MaterialDef_TABLE()) {
        if (!ordinaryMaterialError(g.player, material.grade).empty()) continue;
        MarketItem m;
        m.kind = "material";
        m.id = material.id;
        m.name = material.name;
        m.dao = material.element;
        m.desc = std::to_string(material.grade) + "阶材料 · " + material.element + " · " + material.polarity;
        m.price = normalMaterialBuyPrice(g, "material", material.id);
        if (m.price <= 0) continue;
        m.sellPrice = sellPriceOf(g, material.price);
        const auto owned = a.materials.find(material.id);
        m.owned = owned == a.materials.end() ? 0 : owned->second;
        out.push_back(std::move(m));
    }
    return out;
}

// 用途：列出当前境界可购买的成品法器。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<MarketItem> gearList(const Game& g) {
    vector<MarketItem> out;
    if (!ecoIsCity(g.player.location)) return out;

    // 法器随境界解锁：炼气期只能买到低阶
    const int maxGrade = ordinaryMaterialGradeLimit(g.player);
    for (const auto& sh : data::ShapeDef_TABLE()) {
        for (int grade = 1; grade <= maxGrade; ++grade) {
            MarketItem m;
            m.kind = "gear";
            m.id = sh.id + "-" + std::to_string(grade);
            m.name = std::string(sh.name) + "·" +
                     std::to_string(grade) + "阶";
            m.desc = (sh.slot == "weapon")   ? "兵器"
                     : (sh.slot == "armor")  ? "护甲"
                                             : "饰物";
            const auto Quote = crafting::mfQuoteStandardGearPrice(g, grade);
            if (!Quote.Valid) continue;
            m.price = Quote.BuyPrice;
            m.sellPrice = Quote.SellPrice;
            out.push_back(m);
        }
    }
    return out;
}

// 用途：验证地点品阶库存与灵石后购入指定数量。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& kind, const string& id, int count，按本模块入口调用。
// 返回值：成功空串；失败中文原因且不购入资源。
string marketBuy(Game& g, const string& kind, const string& id, int count) {
    if (g.ended) return "这一世已经结束。";
    if (g.pending.has_value()) return "请先处理当前事件。";
    if (g.player.arts.battle.has_value()) return "斗法中无法交易。";
    if (count < 1 || count > 1000) return "购买数量须为 1 至 1000。";
    if (!ecoIsCity(g.player.location))
        return "此处不是城池，没有坊市。可以在「舆图」前往城池。";

    Player& p = g.player;
    Arts& a = p.arts;

    if (kind == "potion") {
        const data::RecipeDef* r = findRecipe(id);
        if (!r) return "坊市里没有这味丹药。";
        if (r->grade > 3) return "高阶丹药坊市不管卖，需自行炼制。";
        const int unit = potionBuyPrice(g, *r);
        if (unit <= 0) return "丹药材料报价无效，请刷新后重试。";
        const long long TotalCost = static_cast<long long>(unit) * count;
        if (TotalCost > std::numeric_limits<int>::max()) return "丹药购买金额超过支持范围。";
        const int total = static_cast<int>(TotalCost);
        if (p.stones < total)
            return "灵石不足：需要 " + std::to_string(total) +
                   "，你只有 " + std::to_string(p.stones) + "。";
        const auto owned = a.potions.find(r->id);
        if (owned != a.potions.end() && static_cast<long long>(owned->second) + count > 1000000000LL)
            return "背包已无法收存更多丹药。";
        p.stones -= total;
        a.potions[r->id] += count;
        logEvent(g, "self",
                 "在坊市购入" + r->name + " ×" + std::to_string(count) + "。",
                 false, "", "灵石 −" + std::to_string(total));
        return "";
    }

    if (kind == "herb") {
        // 前端传的是药材 id（如 "6001"），但也容忍直接传名称，方便手写请求
        const data::HerbDef* h = findHerbById(id);
        if (!h) h = findHerbByName(id);
        if (!h) return "坊市里没有这味药材。";
        const string sourceError = ordinaryHerbError(p, h->grade);
        if (!sourceError.empty()) return sourceError;
        const int unit = normalMaterialBuyPrice(g, "herb", h->id);
        if (unit <= 0) return "药材市场报价无效，请刷新后重试。";
        const int total = unit * count;
        if (p.stones < total)
            return "灵石不足：需要 " + std::to_string(total) +
                   "，你只有 " + std::to_string(p.stones) + "。";
        long long herbs = count;
        for (const auto& [name, amount] : a.herbs) herbs += amount;
        if (herbs > 1000000000LL || static_cast<long long>(p.herbs) + count > 1000000000LL)
            return "背包已无法收存更多药材。";
        p.stones -= total;
        a.herbs[h->name] += count;
        p.herbs += count;
        logEvent(g, "self",
                 "在坊市购入" + h->name + " ×" + std::to_string(count) + "。",
                 false, "", "灵石 −" + std::to_string(total));
        return "";
    }

    if (kind == "material") {
        const data::MaterialDef* material = nullptr;
        for (const auto& item : data::MaterialDef_TABLE())
            if (item.id == id) { material = &item; break; }
        if (!material) return "坊市里没有这份炼器材料。";
        const string sourceError = ordinaryMaterialError(p, material->grade);
        if (!sourceError.empty()) return sourceError;
        const int unit = normalMaterialBuyPrice(g, "material", material->id);
        if (unit <= 0) return "材料市场报价无效，请刷新后重试。";
        const int total = unit * count;
        if (p.stones < total)
            return "灵石不足：需要 " + std::to_string(total) + "，你只有 " + std::to_string(p.stones) + "。";
        const auto owned = a.materials.find(material->id);
        if (owned != a.materials.end() && static_cast<long long>(owned->second) + count > 1000000000LL)
            return "背包已无法收存更多材料。";
        p.stones -= total;
        a.materials[material->id] += count;
        logEvent(g, "self", "在坊市购入" + material->name + " ×" + std::to_string(count) + "。",
                 false, "", "灵石 −" + std::to_string(total));
        return "";
    }

    if (kind == "gear") {
        if (count != 1) return "法器请每次购买一件。";
        // id 形如 "sword-2"
        const size_t dash = id.find_last_of('-');
        if (dash == string::npos) return "没有这件法器。";
        const string shapeId = id.substr(0, dash);
        const string gradeText = id.substr(dash + 1);
        if (gradeText.size() != 1 || gradeText.front() < '1' || gradeText.front() > '5')
            return "法器品阶无效。";
        const int grade = gradeText.front() - '0';
        const data::ShapeDef* sh = nullptr;
        for (const auto& s : data::ShapeDef_TABLE())
            if (s.id == shapeId) sh = &s;
        if (!sh) return "没有这件法器。";
        if (grade > 2) return "三阶及以上法器不在普通坊市出售，请以秘境材料自行炼制或向道友寻求真实藏品。";
        if (grade > ordinaryMaterialGradeLimit(p)) return "二阶法器需达到筑基期。";

        const auto Quote = crafting::mfQuoteStandardGearPrice(g, grade);
        if (!Quote.Valid) return Quote.Reason;
        const int unit = Quote.BuyPrice;
        if (p.stones < unit)
            return "灵石不足：需要 " + std::to_string(unit) +
                   "，你只有 " + std::to_string(p.stones) + "。";

        p.stones -= unit;
        
        Gear gear;
        do {
            gear.id = "buy-" + std::to_string(g.clock.days) + "-" + std::to_string(g.economy.seq++);
        } while (std::any_of(a.gear.begin(), a.gear.end(), [&](const Gear& item) { return item.id == gear.id; }));
        gear.name = std::string(sh->name) + "·" + std::to_string(grade) + "阶";
        gear.slot = sh->slot;
        gear.shape = sh->id;
        gear.grade = grade;
        gear.price = unit;
        gear.craftCost = unit;
        gear.bonus = 0.04 * grade;
        a.gear.push_back(gear);
        logEvent(g, "self", "购入法器「" + gear.name + "」。", true, "",
                 "灵石 −" + std::to_string(unit));
        return "";
    }

    return "不认识的货品。";
}

// ============================================================
// NPC 往来
// ============================================================
// 用途：筛选所在地仍在世可交互NPC。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
vector<const Npc*> nearbyPeople(const Game& g) {
    vector<const Npc*> out;
    for (const auto& n : g.npcs) {
        if (!n.alive) continue;
        if (n.location != g.player.location) continue;
        out.push_back(&n);
    }
    // 好感度高的排前面，玩家更容易找到熟人
    std::sort(out.begin(), out.end(), [&](const Npc* a, const Npc* b) {
        return relationOf(g, a->id) > relationOf(g, b->id);
    });
    return out;
}

// 用途：按NPC ID查询现有人物。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& id，按本模块入口调用。
// 返回值：匹配定义或实体指针；未找到时为空。
const Npc* findPerson(const Game& g, const string& id) {
    for (const auto& n : g.npcs)
        if (n.id == id && n.alive && n.health > 0 && n.age < n.lifespan) return &n;
    return nullptr;
}

// 用途：把好感数值转换为中文关系等级。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入int v，按本模块入口调用。
// 返回值：对应文字、ID或失败原因；动作错误接口成功时为空。
string relationLabel(int v) {
    if (v >= 80) return "生死之交";
    if (v >= 60) return "知己";
    if (v >= 40) return "好友";
    if (v >= 20) return "相熟";
    if (v >= 1) return "点头之交";
    if (v == 0) return "素不相识";
    if (v >= -20) return "有些芥蒂";
    if (v >= -50) return "结怨";
    return "仇敌";
}

// 用途：预览指定道友当前可做往来及条件。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& npcId，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
static int socialAffinityGain(const Player& player, const string& action) {
    int base = 0;
    if (action == "chat") base = 3 + player.luck / 25;
    else if (action == "study") base = 3;
    else if (action == "travel") base = 12;
    else if (action == "deep" || action == "partner") base = 15;
    else if (action == "master" || action == "disciple") base = 10;
    return base > 0 ? base + talentAffinityBonus(player) : 0;
}

vector<SocialAction> socialActions(const Game& g, const string& npcId) {
    vector<SocialAction> out;
    const Npc* n = findPerson(g, npcId);
    if (!n || n->location != g.player.location) return out;

    const Player& p = g.player;
    const int rel = relationOf(g, npcId);
    const Bonds& b = g.bonds;

    auto add = [&](const char* id, const char* name, const char* desc, int days,
                   int cost) {
        SocialAction a;
        a.id = id;
        a.name = name;
        a.desc = desc;
        const int affinity = socialAffinityGain(p, id);
        if (affinity > 0) a.desc += "；好感 +" + std::to_string(affinity) +
            (talentAffinityBonus(p) ? "（含福缘深厚 +2）" : "");
        a.days = days;
        a.cost = cost;
        out.push_back(a);
    };

    add("chat", "闲谈叙话", "聊上几句，增进了解；同一道友每月一次", 1, 0);
    add("gift", "赠送灵药", "赠送背包中的一份药材；好感按价值与偏好折算，交情按价值累积", 1, 0);
    add("spar", "切磋剑艺", "以真实神通卡牌进行非致命斗法；同一道友每月一次，用时3天", 3, 0);
    out.back().desc += "；完成切磋好感 +" + std::to_string(4 + talentAffinityBonus(p)) + "，撤离时 +4";
    add("study", "请教修行", "讨论修行，悟性 +2；与彻夜论道共用每人每月两次限额", 2, 5);
    add("travel", "结伴游历", "一同出门 10 天；同一道友每月一次", 10, 10);
    if (rel >= 30)
        add("deep", "彻夜论道", "深谈一场，心境 +2", 5, 0);
    if (rel >= 60 && b.partner.empty())
        add("partner", "结为道侣", "确认道侣名分", 7, 0);
    // 条件不满足时仍显示入口与准确原因，避免玩家误以为没有拜师系统。
    add("master", "拜师礼", "拜本门高境修士为师：好感40、声望10，20灵石、5天", 5, 20);
    if (rel >= 30 && b.disciples.size() < 2 && p.stage >= 3 &&
        n->stage < p.stage &&
        std::find(b.disciples.begin(), b.disciples.end(), npcId) == b.disciples.end())
        add("disciple", "收为弟子", "将对方收入门下", 5, 0);

    // 逐个算可用性，并给出具体原因
    const int month = g.clock.days / 30;
    const auto itBond = p.npcBonds.find(npcId);
    const NpcBond* npcBond = itBond == p.npcBonds.end() ? nullptr : &itBond->second;
    const string blocked = friendship::interactionError(g, npcId);
    for (auto& a : out) {
        a.enabled = true;
        if (!blocked.empty()) { a.enabled = false; a.reason = blocked; continue; }
        if (a.id == "gift") {
            if (p.herbs < 1 || std::none_of(p.arts.herbs.begin(), p.arts.herbs.end(),
                                           [](const auto& entry) { return entry.second > 0; })) {
                a.enabled = false;
                a.reason = "没有药材可送（可在坊市购买）";
            }
        } else if (a.id == "spar") {
            if (p.health < 40) {
                a.enabled = false;
                a.reason = "气血不足 40，先休养";
            }
        } else if (a.id == "study") {
            if (p.stones < a.cost) {
                a.enabled = false;
                a.reason = "灵石不足（需 " + std::to_string(a.cost) + "）";
            }
        } else if (a.id == "travel") {
            if (p.stones < a.cost) {
                a.enabled = false;
                a.reason = "灵石不足（需 " + std::to_string(a.cost) + "）";
            } else if (p.health < 50) {
                a.enabled = false;
                a.reason = "气血不足 50，先休养";
            }
        } else if (a.id == "partner") {
            if (b.master == npcId ||
                std::find(b.disciples.begin(), b.disciples.end(), npcId) != b.disciples.end()) {
                a.enabled = false;
                a.reason = "师徒不可结为道侣";
            } else if (p.age < 18 || n->age < 18) {
                a.enabled = false;
                a.reason = "双方须成年";
            } else if (std::abs(p.stage - n->stage) > 3) {
                a.enabled = false;
                a.reason = "境界相差超过 3 小境";
            }
        } else if (a.id == "master") {
            a.reason = sectMasterError(g, npcId);
            a.enabled = a.reason.empty();
        } else if (a.id == "disciple") {
            if (b.master == npcId) {
                a.enabled = false;
                a.reason = "不能将现任师尊收为弟子";
            } else if (b.partner == npcId) {
                a.enabled = false;
                a.reason = "道侣不能再结为师徒";
            } else if (b.disciples.size() >= 2) {
                a.enabled = false;
                a.reason = "门下已满两人";
            }
        }
        if (npcBond && a.id == "chat" && npcBond->talkMonth == month) {
            a.enabled = false; a.reason = "本月已与这位道友闲谈，请下月再叙。";
        }
        if (npcBond && (a.id == "study" || a.id == "deep") &&
            npcBond->discussMonth == month && npcBond->discussCount >= 2) {
            a.enabled = false; a.reason = "本月已与这位道友讨论两次修行。";
        }
        if (npcBond && a.id == "travel" && npcBond->travelMonth == month) {
            a.enabled = false; a.reason = "本月已与这位道友结伴游历。";
        }
        if (npcBond && a.id == "spar" && npcBond->sparMonth == month) {
            a.enabled = false; a.reason = "本月已与这位道友切磋。";
        }
    }
    return out;
}

// 用途：执行已验证的请教赠礼交流与切磋。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& npcId, const string& actionId，按本模块入口调用。
// 返回值：SocialResult结果，包含说明所述状态或计算数据。
SocialResult socialDo(Game& g, const string& npcId, const string& actionId) {
    SocialResult r;
    r.error = friendship::interactionError(g, npcId);
    if (!r.error.empty()) return r;
    const Npc* n = findPerson(g, npcId);
    if (!n || n->location != g.player.location) {
        r.error = "此人已不在附近。";
        return r;
    }

    // 重新校验一遍，防止前端传来不可用的动作
    const auto acts = socialActions(g, npcId);
    const SocialAction* act = nullptr;
    for (const auto& a : acts)
        if (a.id == actionId) act = &a;
    if (!act) {
        r.error = "没有这项往来。";
        return r;
    }
    if (!act->enabled) {
        r.error = act->reason.empty() ? "当前不能这样做。" : act->reason;
        return r;
    }

    Player& p = g.player;
    Arts& a = p.arts;
    Bonds& b = g.bonds;
    const string who = n->name;
    const int month = g.clock.days / 30;

    if (actionId == "chat") {
        p.npcBonds[npcId].talkMonth = month;
        const int gain = socialAffinityGain(p, actionId);
        addRelation(g, npcId, gain);
        p.insight = std::min(INSIGHT_MAX, p.insight + 1);
        r.relationDelta = gain;
        r.text = "你与" + who + "闲谈一阵，气氛颇为投契。";
        logEvent(g, "npc", r.text + "（好感 +" + std::to_string(gain) + "）",
                 false, npcId);
    } else if (actionId == "gift") {
        string herb;
        for (const auto& [name, count] : a.herbs) if (count > 0) { herb = name; break; }
        const auto gift = friendship::giftItem(g, npcId, "herb", herb);
        if (!gift.ok) { r.error = gift.error; return r; }
        r.relationDelta = gift.affinityDelta; r.text = gift.text;
    } else if (actionId == "spar") {
        p.npcBonds[npcId].sparMonth = month;
        EnemySpec enemy; enemy.name = n->name; enemy.id = n->id;
        enemy.stage = n->stage; enemy.health = n->health; enemy.kind = "spar";
        enemy.style = n->dao; enemy.sect = n->sect; enemy.gender = n->gender;
        enemy.lethal = false;
        battleStart(g, enemy);
        r.text = "你与" + who + "约定点到为止，以各自功法与神通切磋。";
    } else if (actionId == "study") {
        p.stones -= act->cost;
        const int gain = 8 + p.insight / 20;
        artGain(p, a.focus, gain);
        p.insight = std::min(INSIGHT_MAX, p.insight + 2);
        r.relationDelta = socialAffinityGain(p, actionId);
        addRelation(g, npcId, r.relationDelta);
        r.text = "你向" + who + "请教修行疑难，受益良多。";
        logEvent(g, "npc", r.text + "（悟性 +2 · 大道经验 +" +
                                std::to_string(gain) + "）", false, npcId,
                 "灵石 −" + std::to_string(act->cost));
    } else if (actionId == "travel") {
        p.npcBonds[npcId].travelMonth = month;
        p.stones -= act->cost;
        const int gain = socialAffinityGain(p, actionId);
        addRelation(g, npcId, gain);
        p.insight = std::min(INSIGHT_MAX, p.insight + 4);
        artGain(p, a.focus, 5);
        g.adventure.total += 10;
        g.adventure.since += 10;
        r.relationDelta = gain;
        r.text = "你与" + who + "结伴游历十日，见闻与情谊都有增长。";
        logEvent(g, "npc", r.text + "（好感 +" + std::to_string(gain) + " · 悟性 +4）", true, npcId,
                 "灵石 −" + std::to_string(act->cost));
    } else if (actionId == "deep") {
        const int gain = socialAffinityGain(p, actionId);
        addRelation(g, npcId, gain);
        a.heart += 2;
        // 星河剑派「观水悟剑，论道收益提高」：论道给的大道经验 +50%。
        // 这条写在宗门说明里，但之前一直没接 —— 论道给的是固定 12 点。
        const int dao = (p.sect == "xinghe") ? 18 : 12;
        artGain(p, a.focus, dao);
        if (b.partner == npcId) b.bondDays += 5;
        r.relationDelta = gain;
        r.text = "你与" + who + "彻夜论道，彼此心意更近了一层。";
        logEvent(g, "npc",
                 r.text + "（好感 +" + std::to_string(gain) + " · 心境 +2 · 大道经验 +" +
                     std::to_string(dao) + "）",
                 true, npcId);
    } else if (actionId == "partner") {
        b.partner = npcId;
        b.bondDays = 0;
        r.relationDelta = socialAffinityGain(p, actionId);
        addRelation(g, npcId, r.relationDelta);
        r.text = "你与" + who + "结为道侣。往后可相伴共修。";
        logEvent(g, "self", r.text, true, npcId);
    } else if (actionId == "master") {
        p.stones -= 20;
        b.master = npcId;
        r.relationDelta = socialAffinityGain(p, actionId);
        addRelation(g, npcId, r.relationDelta);
        r.text = "你行拜师礼，" + who + "收下你的拜帖。";
        logEvent(g, "sect", r.text, true, npcId, "灵石 −20");
    } else if (actionId == "disciple") {
        b.disciples.push_back(npcId);
        r.relationDelta = socialAffinityGain(p, actionId);
        addRelation(g, npcId, r.relationDelta);
        r.text = who + "拜入门下。";
        logEvent(g, "sect", r.text, true, npcId);
    } else {
        r.error = "没有这项往来。";
        return r;
    }

    if (actionId == "study" || actionId == "deep") {
        NpcBond& bond = p.npcBonds[npcId];
        if (bond.discussMonth != month) { bond.discussMonth = month; bond.discussCount = 0; }
        ++bond.discussCount;
    }
    r.ok = true;
    return r;
}

// ============================================================
// 炼丹 / 炼器（即时结算）
// ============================================================
// 用途：汇总即时制作候选及既有数值门槛。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入const Game& g, const string& kind，按本模块入口调用。
// 返回值：对应目录或计算结果列表。
vector<CraftOption> craftOptions(const Game& g, const string& kind) {
    vector<CraftOption> out;
    const Player& p = g.player;
    const Arts& a = p.arts;

    if (kind == "brew") {
        for (const auto& rec : data::RecipeDef_TABLE()) {
            CraftOption c;
            c.kind = "brew";
            c.id = rec.id;
            c.name = rec.name;
            c.desc = artPillUseInfo(p, rec).effect;
            const BrewInfo b = artBrewInfo(p, rec.id);
            c.output = b.output;
            c.chance = b.chance;
            c.stoneCost = b.cost;
            c.days = ecoCraftDays(p, "brew", rec.grade);
            c.enabled = true;
            if (!b.valid) {
                c.enabled = false;
                c.reason = "丹炉不足以炼此丹";
            } else if (p.stones < b.cost) {
                c.enabled = false;
                c.reason = "灵石不足（需 " + std::to_string(b.cost) + "）";
            } else {
                // 检查药材是否齐备，缺哪味就说哪味
                string missing;
                for (const auto& [herbName, need] : parseIngredients(rec.ingredients)) {
                    const data::HerbDef* h = findHerbByName(herbName);
                    if (!h) continue;
                    auto it = a.herbs.find(h->name);
                    const int have = (it == a.herbs.end()) ? 0 : it->second;
                    if (have < need) {
                        missing += h->name + "（有 " + std::to_string(have) +
                                   "/" + std::to_string(need) + "） ";
                    }
                }
                if (!missing.empty()) {
                    c.enabled = false;
                    c.reason = "缺药材：" + missing;
                }
            }
            if (c.enabled) {
                c.reason = ecoActionError(g, "brew", rec.id);
                c.enabled = c.reason.empty();
            }
            out.push_back(c);
        }
    }
    return out;
}

// 用途：执行即时制作并推进相应天数。
// 参与流程：人生推进、成长与世界模拟。
// 使用方式：传入Game& g, const string& kind, const string& id，按本模块入口调用。
// 返回值：CraftResult结果，包含说明所述状态或计算数据。
CraftResult craftDo(Game& g, const string& kind, const string& id) {
    CraftResult r;
    const string validation = ecoActionError(g, kind, id);
    if (!validation.empty()) {
        r.error = validation;
        return r;
    }
    Player& p = g.player;
    Arts& a = p.arts;

    if (kind == "brew") {
        const data::RecipeDef* rec = findRecipe(id);
        if (!rec) {
            r.error = "没有这张丹方。";
            return r;
        }
        const BrewInfo b = artBrewInfo(p, id);
        if (!b.valid) {
            r.error = "丹炉不足以炼此丹。";
            return r;
        }
        if (p.stones < b.cost) {
            r.error = "灵石不足：需要 " + std::to_string(b.cost) + "。";
            return r;
        }
        // 缺料检查
        for (const auto& [herbName, need] : parseIngredients(rec->ingredients)) {
            const data::HerbDef* h = findHerbByName(herbName);
            if (!h) continue;
            auto it = a.herbs.find(h->name);
            const int have = (it == a.herbs.end()) ? 0 : it->second;
            if (have < need) {
                r.error = "缺药材：" + h->name + " 需要 " + std::to_string(need) +
                          "，只有 " + std::to_string(have) + "。";
                return r;
            }
        }

        // 开炉
        p.stones -= b.cost;
        for (const auto& [herbName, need] : parseIngredients(rec->ingredients)) {
            const data::HerbDef* h = findHerbByName(herbName);
            if (!h) continue;
            a.herbs[h->name] -= need;
            p.herbs = std::max(0, p.herbs - need);
        }
        const data::FurnaceDef* f = findFurnace(a.furnace);
        if (!(artHas(a, "丹", 4) && f && f->grade > rec->grade)) a.durability--;
        p.alchemy = std::min(500, p.alchemy + 3);
        artGain(p, "丹", 12);

        const bool win = randomOf(g) < b.chance;
        r.success = win;
        if (win) {
            a.potions[rec->id] += b.output;
            a.progress.brew++;
            r.text = "开炉成丹，" + rec->name + " 得" + std::to_string(b.output) + "枚。";
            logEvent(g, "self", r.text, true, "",
                     "丹术 +3 · 灵石 −" + std::to_string(b.cost));
        } else {
            r.text = "开炉失败，" + std::string(rec->name) + " 的材料耗尽了。";
            logEvent(g, "self", r.text, true, "",
                     "丹术 +3 · 灵石 −" + std::to_string(b.cost));
        }
        r.ok = true;
        return r;
    }

    r.error = "不认识的炼制方式。";
    return r;
}

} // namespace nz::play
