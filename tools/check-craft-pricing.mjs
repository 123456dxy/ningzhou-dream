// 纯展示检查：报价来自固定权威夹具，不代替C++真实价格、买卖或浏览器验证。
import assert from 'node:assert/strict';
import { renderBrewIncome } from '../web/economy.mjs';
import { renderForgePreview } from '../web/artifacts.mjs';
import { renderInventoryPanel } from '../web/inventory-panel.mjs';

let checks = 0;
function check(value, label) { assert.ok(value, label); ++checks; }

const potionPricing = { valid: true, materialCost: 27, standardOutput: 1, baseSellPrice: 41,
  buyPrice: 81, sellPrice: 41, saleMultiplier: 1, inscriptionMultiplier: 1,
  renownSaleMultiplier: 1, craftedSaleMultiplier: 1, estimatedMaterials: false };
const gearPricing = { valid: true, materialCost: 292, standardOutput: 1, baseSellPrice: 438,
  buyPrice: 876, sellPrice: 677, saleMultiplier: 1.5456, inscriptionMultiplier: 1.12,
  renownSaleMultiplier: 1.2, craftedSaleMultiplier: 1.15, estimatedMaterials: false };

const brew = renderBrewIncome({ output: 2, sellPrice: 41, expectedSaleIncome: 55.06, pricing: potionPricing });
check(brew.includes('材料市价 27') && brew.includes('81'), '炼丹收入展示附带完整原料价值及成品买价');
check(brew.includes('1.5') && brew.includes('3 倍'), '炼丹明确材料市价的一点五倍与三倍规则');
check(brew.includes('期望不是保底'), '炼丹仍保留期望收益与实际成交限制');

const forge = renderForgePreview({ info: { valid: true, shapeName: '剑', grade: 1, chance: .6, days: 30,
  cost: 33, pricing: gearPricing } });
check(forge.includes('材料市价 292') && forge.includes('438') && forge.includes('677'), '炼器展示原料价值、基础卖价与含加成最终卖价');
check(forge.includes('铭刻') && forge.includes('1.12') && forge.includes('声望') && forge.includes('1.2')
  && forge.includes('自炼') && forge.includes('1.15'), '法器铭刻声望自炼加成分别展示');

const state = { player: { name: '定价测试', cores: 0, pills: 0 }, arts: { potions: { '5001': 1 }, materials: {}, herbs: {} },
  pillCatalog: [{ id: '5001', name: '化淤丹', grade: 1, effect: '恢复健康度', pricing: potionPricing }],
  gears: [{ id: 'gear-price', name: '锻剑铁剑', grade: 1, pricing: gearPricing }], furnaces: [] };
const inventory = renderInventoryPanel(state);
check(inventory.includes('材料市价 27') && inventory.includes('材料市价 292'), '行囊中的丹药与法器能查看各自定价');

const { renderCraftPricing } = await import('../web/craft-pricing.mjs');
const direct = renderCraftPricing(gearPricing, { kind: 'gear' });
check(direct.includes('<details') && !/<details[^>]*\bopen\b/.test(direct), '定价明细默认折叠保持卡片紧凑');
check(direct.includes('最终出售价 677') && !direct.includes('676.9728'), '最终价格只展示后端整数金额不另算乘区');
check(renderCraftPricing({ ...gearPricing, sellPrice: 701 }, { kind: 'gear' }).includes('最终出售价 701'), '后端给出不同最终报价时界面不重新计算');
check(renderCraftPricing({ ...gearPricing, estimatedMaterials: true }, { kind: 'gear' }).includes('标准材料估价'), '无真实配料账的旧法器显式标注估价');
const missing = renderCraftPricing();
check(missing.includes('待报价') && !missing.includes('0 灵石'), '没有pricing字段不会编造零售价');
const invalid = renderCraftPricing({ valid: false, materialCost: 0, priceReason: '材料账<损坏>' }, { kind: 'gear' });
check(invalid.includes('材料账&lt;损坏&gt;') && !invalid.includes('材料账<损坏>') && !invalid.includes('0 灵石'), '失败报价只展示转义原因且不将默认零当合法价格');
check(renderCraftPricing({ ...potionPricing, buyPrice: undefined }).includes('购入价 待报价'), '有效报价中的缺字段仍显示待报价');
check(renderBrewIncome({ output: 1, sellPrice: 41, expectedSaleIncome: 27.53 }).includes('待报价'), '旧响应缺价格详情时仍有明确占位');
console.log(`制作价格纯展示检查：${checks}/${checks} 通过（模拟权威字段）`);
