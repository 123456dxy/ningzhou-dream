// 一键交易与拖动数量的定向展示检查；只验证桌面模块，不操作玩家存档。
import assert from 'node:assert/strict';
import { renderTideMarket } from '../web/tide-panel.mjs';
import { renderInventoryPanel } from '../web/inventory-panel.mjs';
import { renderCavePage } from '../web/cave-panel.mjs';

let passed = 0;
// 用途：记录本轮桌面断言；流程：定向纯展示验收；用法：传条件及说明；返回：无，失败立即结束。
function check(value, label) { assert.ok(value, label); passed++; console.log(`PASS ${label}`); }

const item = { kind: 'herb', id: 'h3', name: '三阶灵草', grade: 3, unitPrice: 50, basePrice: 100,
  stock: 20, owned: 4, enabled: true, maxPurchase: { enabled: true, count: 7, unitPrice: 50, totalCost: 350, reason: '' } };
const state = { player: { stones: 350, cores: 0, pills: 0 }, world: { marketCash: 1234 }, inCity: true, gears: [], arts: { potions: { heal: 9 } },
  pillCatalog: [{ id: 'heal', name: '化淤丹', grade: 1, enabled: true,
    maxSale: { enabled: true, count: 4, unitPrice: 31, totalGain: 124, reason: '', limitReason: '本月最多收购4枚' } }],
  tide: { eventId: 'event-A', totalScore: 3000, market: { open: true, daysLeft: 20, items: [item] } },
  cave: { garden: { plotCountUnlocked: 1, plantsPerPlotLimit: 99, harvestMultiplier: 3, hasGreenBottle: false,
    plantOptions: [{ herbId: 'seed', name: '恒心草', grade: 1, ownedCount: 8 }],
    plots: [{ plotId: 'plot1', remainingCapacity: 99, plantEnabled: true, seedCount: 0 }] } } };
const before = JSON.stringify(state);
const tide = renderTideMarket(state, { grade: '3', quantities: { 'herb:h3': 2 } });
check(/type="range"[^>]*min="1"[^>]*max="7"/.test(tide), '兽潮数量使用拖动滑条并按权威可买上限约束');
check(tide.includes('一键买满') && tide.includes('7 份') && tide.includes('350 灵石'), '每种材料的一键买满显示权威份数与总价');
check(/data-action="tide-market-buy-max"[^>]*data-count="7"[^>]*data-unit-price="50"/.test(tide), '一键购买携带当前报价份数与预期单价');
check(tide.includes('data-event-id="event-A"'), '一键购买仍携带本期活动标识');
const inventory = renderInventoryPanel(state);
check(inventory.includes('一键售出</button>') && inventory.includes('坊市剩余收购资金 <strong>1234</strong> 灵石')
  && !inventory.includes('124 灵石'), '行囊售出按钮简洁并直接贴当前坊市收购资金');
check(/<button(?=[^>]*data-action="sell-max")(?=[^>]*data-count="4")(?=[^>]*data-unit-price="31")/.test(inventory), '丹药批量出售携带权威数量及预期单价');
check(inventory.includes('出售1枚') && !inventory.includes('本月最多收购4枚'), '保留单枚售出入口且不增加可售数量与限额提示');
const cave = renderCavePage(state);
check(/type="range"[^>]*min="1"[^>]*max="8"/.test(cave), '播种滑条上限为当前拥有数量和空田容量');
check(!cave.includes('data-cave-reserve-amount'), '没有小绿瓶时仍不显示灵石储备控件');
check(JSON.stringify(state) === before, '展示不修改权威快照');
const disabledState = structuredClone(state);
disabledState.tide.market.items[0].maxPurchase = { enabled: false, count: 0, unitPrice: 50, totalCost: 0, reason: '行囊已满' };
disabledState.pillCatalog[0].maxSale = { enabled: false, count: 0, unitPrice: 31, totalGain: 0, reason: '坊市本月资金不足' };
check(/data-action="tide-market-buy-max"[^>]*disabled/.test(renderTideMarket(disabledState))
  && renderTideMarket(disabledState).includes('行囊已满'), '不可买时一键入口禁用并显示后端原因');
check(/data-action="sell-max"[^>]*disabled/.test(renderInventoryPanel(disabledState))
  && renderInventoryPanel(disabledState).includes('坊市本月资金不足'), '不可售时一键入口禁用并显示后端原因');
const malicious = structuredClone(state);
malicious.pillCatalog[0].maxSale.reason = '<img src=x onerror=alert(1)>';
check(!renderInventoryPanel(malicious).includes('<img src=x'), '后端交易原因保持HTML转义');
const gearState = structuredClone(state);
gearState.gears = [{ id: 'gear-A', name: '试炼剑', grade: 1, equipped: true,
  actions: { sell: { enabled: false, reason: '请先卸下法器' }, unequip: { enabled: true } } }];
const gearHtml = renderInventoryPanel(gearState);
check(/<button(?=[^>]*data-action="sell")(?=[^>]*data-kind="gear")(?=[^>]*data-id="gear-A")(?=[^>]*disabled)[^>]*>出售法器/.test(gearHtml)
  && gearHtml.includes('请先卸下法器'), '法宝处置区提供出售法器并遵守后端装备资格');
check(gearHtml.includes('坊市剩余收购资金 <strong>1234</strong> 灵石'), '法器处置旁直接显示当前坊市收购资金');
console.log(`一键交易与数量滑条纯展示：${passed} 项通过。`);
