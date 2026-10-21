#include "harness.h"
#include "core/battle.h"
#include "core/combat/water_flow.h"
#include <array>
#include <limits>

namespace nztest {
namespace {
// 用途：构造单张显式定义；流程：纯水流报价回归；返回：不读目录、不建立战斗的独立牌值。
nz::SpellDef mfWaterFlowSpell(const nz::string& Id, const nz::string& Kind) {
    return {Id, "水流测试", "水", 3, Kind, .1, 0, 7};
}
}

// 用途：验证原生水攻击的补牌预算和目录配置；流程：独立或统一harness；返回：失败计入统一框架。
void mfRunWaterFlowTests() {
    using namespace nz;
    std::printf("\n--- 原生水流补牌 ---\n");
    check(battleSpell("softwater") && battleSpell("softwater")->kind == "drawTo", "柔水无痕恢复为补至指定手牌数");
    const auto Source = mfWaterFlowSpell("waterSeal", "water");
    const auto Release = mfWaterFlowSpell("wavehit", "wave");
    {
        const auto First = WaterFlow::mfQuoteAttack(Source, 0, 2, 0);
        checkEq(First.Draws, 1, "蓄水攻击实际新增水势时补一张");
        checkEq(First.AfterUsed, 1, "蓄水补牌占用本轮一次共享预算");
        const auto Second = WaterFlow::mfQuoteAttack(Release, 4, 2, First.AfterUsed);
        checkEq(Second.Draws, 1, "四层水势释放可接续补一张");
        checkEq(Second.AfterUsed, 2, "蓄水和浪击共享两张预算");
        const auto Third = WaterFlow::mfQuoteAttack(Source, 2, 4, Second.AfterUsed);
        checkEq(Third.Draws, 0, "两张预算耗尽后增水攻击不再自补");
        checkEq(Third.AfterUsed, 2, "预算耗尽不溢出计数");
        const auto NextRound = WaterFlow::mfQuoteAttack(Source, 0, 2, 0);
        checkEq(NextRound.Draws, 1, "调用者新回合显式重置预算后可再次补牌");
    }
    {
        for (const auto* Id : {"waterSeal", "stormSword"}) {
            const auto Spell = mfWaterFlowSpell(Id, "water");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 5, 6, 0).Draws, 1, "接近水势上限实际新增一层仍补牌");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 6, 6, 0).Draws, 0, "满水实际新增零层不补牌");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 4, 3, 0).Draws, 0, "水势减少不误判为蓄水补牌");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 8, 10, 0).Draws, 1, "法器扩容后的原生蓄水仍按实际新增报价");
        }
        for (const auto* Id : {"wavehit", "waterfall"}) {
            const auto Spell = mfWaterFlowSpell(Id, "wave");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 3, 1, 0).Draws, 0, "三层浪击未到补牌门槛");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 4, 2, 0).Draws, 1, "四层浪击按释放前水势补牌");
            checkEq(WaterFlow::mfQuoteAttack(Spell, 6, 3, 2).Draws, 0, "满层浪击不能绕过共享预算");
        }
    }
    {
        for (const auto& Spell : std::array<SpellDef, 7>{
            mfWaterFlowSpell("artifact-wave", "wave"), mfWaterFlowSpell("wave_consume", "wave"),
            mfWaterFlowSpell("waterSword", "sword"), mfWaterFlowSpell("waterDraw", "draw"),
            mfWaterFlowSpell("water", "shield"), mfWaterFlowSpell("waterSeal", "wave"),
            mfWaterFlowSpell("wavehit", "water")}) {
            const auto Quote = WaterFlow::mfQuoteAttack(Spell, 4, 6, 1);
            checkEq(Quote.Draws, 0, "非白名单或类型不符不触发原生水补牌：" + Spell.id);
            checkEq(Quote.AfterUsed, 1, "不相关牌不消耗水补牌预算：" + Spell.id);
            auto Configured = Spell;
            check(!WaterFlow::mfConfigureSpell(Configured), "不配置法器、五行水剑、经营牌或类型不符牌：" + Spell.id);
            checkNear(Configured.scale, .1, "未命中白名单不改变倍率");
            checkEq(Configured.cooldown, 7, "未命中白名单不改变冷却");
        }
    }
    {
        for (const auto& Input : std::array<std::array<int, 3>, 6>{{
            {{-1, 2, 0}}, {{0, -1, 0}}, {{0, 2, -1}}, {{0, 2, 3}},
            {{0, 2, std::numeric_limits<int>::max()}}, {{0, 2, std::numeric_limits<int>::min()}}
        }}) {
            const auto Quote = WaterFlow::mfQuoteAttack(Source, Input[0], Input[1], Input[2]);
            checkEq(Quote.Draws, 0, "非法层数或预算不获得补牌");
            checkEq(Quote.AfterUsed, Input[2], "非法输入不静默修复或溢出预算");
        }
        const auto First = WaterFlow::mfQuoteAttack(Source, 0, 2, 0);
        const auto Repeated = WaterFlow::mfQuoteAttack(Source, 0, 2, 0);
        checkEq(First.Draws, Repeated.Draws, "相同纯报价可重复查询而不扣预算");
        checkEq(First.AfterUsed, Repeated.AfterUsed, "纯报价不保存隐式跨查询计数");
        checkEq(Source.cooldown, 7, "只读报价不修改神通定义");
    }
    {
        struct Expected { const char* Id; const char* Kind; double Scale; };
        for (const auto& Entry : std::array<Expected, 4>{{
            {"waterSeal", "water", .5}, {"stormSword", "water", .75},
            {"wavehit", "wave", .9}, {"waterfall", "wave", 1.2}
        }}) {
            auto Spell = mfWaterFlowSpell(Entry.Id, Entry.Kind);
            check(WaterFlow::mfConfigureSpell(Spell), "四张原生水攻击可配置：" + Spell.id);
            checkNear(Spell.scale, Entry.Scale, "原生攻击使用选定倍率：" + Spell.id);
            checkEq(Spell.cooldown, 0, "原生水攻击无输出冷却：" + Spell.id);
            checkEq(Spell.cost, 3, "水流模块不改变费用与返灵预算");
            checkNear(Spell.statusScale, 1, "水流模块不改变实际增水状态倍率");
        }
    }
}
} // namespace nztest

#ifdef NZ_WATER_FLOW_STANDALONE
int main() {
    nztest::mfRunWaterFlowTests();
    return nztest::failures() == 0 ? 0 : 1;
}
#endif
