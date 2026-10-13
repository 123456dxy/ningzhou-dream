import { esc } from './shell.mjs';

// 用途：格式化服务端报价数值；流程：纯价格展示；返回：安全文本，缺失或非法数值为待报价。
function quoteNumber(value) {
  return typeof value === 'number' && Number.isFinite(value) && value >= 0
    ? esc(value) : '待报价';
}

// 用途：展示制作成品的权威材料定价及法器额外加成；流程：工坊、坊市和行囊共用。
// 使用：传服务端pricing对象及potion/gear类别；返回：默认折叠HTML，无资源变化或价格重算。
// 缺报价/失败报价不把后端默认零当成交价，材料价值与缺料采购支出分别解释。
export function renderCraftPricing(pricing, { kind = 'potion', compact = false } = {}) {
  if (pricing?.valid !== true) {
    const reason = pricing?.priceReason ? `：${esc(pricing.priceReason)}` : '';
    return `<p class="panel-note craft-pricing-pending" data-craft-pricing-pending>定价待报价${reason}</p>`;
  }
  const isGear = kind === 'gear';
  const bonuses = isGear ? [
    ['铭刻', pricing.inscriptionMultiplier],
    ['声望出售', pricing.renownSaleMultiplier],
    ['器道 / 自炼', pricing.craftedSaleMultiplier],
  ] : [];
  const activeBonuses = bonuses.filter(([, multiplier]) => typeof multiplier === 'number'
    && Number.isFinite(multiplier) && multiplier > 1);
  const hasBonusDetails = bonuses.every(([, multiplier]) => typeof multiplier === 'number' && Number.isFinite(multiplier));
  const bonusText = isGear ? `<p class="panel-note craft-price-bonuses">额外出售加成：${activeBonuses.length
    ? activeBonuses.map(([label, multiplier]) => `${label} ×${quoteNumber(multiplier)}`).join(' · ')
    : hasBonusDetails ? '未应用额外加成' : '加成明细待报价'}</p>` : '';
  const outputText = !isGear ? ` · 标准产量 ${quoteNumber(pricing.standardOutput)} 枚` : '';
  const estimatedText = pricing.estimatedMaterials
    ? '<p class="panel-note craft-price-estimate">标准材料估价：没有完整配料账，按同品阶十份原料估算。</p>' : '';
  return `<details class="craft-pricing${compact ? ' compact' : ''}" data-craft-pricing="${isGear ? 'gear' : 'potion'}">`
    + `<summary>定价 · 最终出售价 ${quoteNumber(pricing.sellPrice)} 灵石</summary>`
    + `<div class="craft-pricing-detail"><p class="panel-note">完整配方材料市价 ${quoteNumber(pricing.materialCost)} 灵石${outputText}</p>`
    + `<div class="craft-cost">基础出售价 ${quoteNumber(pricing.baseSellPrice)} 灵石（材料市价 1.5 倍）<br>`
    + `基准购入价 ${quoteNumber(pricing.buyPrice)} 灵石（材料市价 3 倍）</div>`
    + bonusText + `<div class="craft-cost">最终出售价 ${quoteNumber(pricing.sellPrice)} 灵石</div>`
    + estimatedText + '<p class="panel-note">使用完整配方材料价值，已有库存不抵扣；不含炼制费用，不重复叠加行情。出售仍需满足当地资金与需求限制。</p>'
    + `</div></details>`;
}
