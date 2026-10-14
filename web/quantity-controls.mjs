// 桌面数量交互：滑条仅保存选择与展示合计，一键交易数量和报价始终来自服务端。
import { esc } from './shell.mjs';

// 用途：收束滑条草稿到整数范围；流程：首次显示及连续拖动；用法：传草稿与控件上限；返回：至少一的整数。
export function quantityValue(value, maximum = 99) {
  const upper = Number.isSafeInteger(Number(maximum)) && Number(maximum) > 0 ? Number(maximum) : 1;
  const draft = Number.isFinite(Number(value)) ? Math.floor(Number(value)) : 1;
  return Math.max(1, Math.min(upper, draft));
}

// 用途：生成带当前数量和上下限的滑条；流程：坊市购买、兽潮购买及灵田播种。
// 用法：传本地值及已确定的上限，attributes仅接收调用模块中已转义的控件属性；返回：不提交请求的HTML。
export function renderQuantitySlider({ value = 1, maximum = 99, enabled = true, label = '数量', unit = '份',
  attributes = '', className = '' } = {}) {
  const upper = quantityValue(maximum, Number.MAX_SAFE_INTEGER), selected = quantityValue(value, upper);
  return `<label class="quantity-slider ${esc(className)}" data-quantity-slider><span class="quantity-slider-heading">${esc(label)} <output data-quantity-value>${selected}</output> ${esc(unit)}</span>`
    + `<input type="range" min="1" max="${upper}" step="1" value="${selected}" data-quantity-range ${attributes}`
    + ` aria-label="${esc(label)}，1 至 ${upper} ${esc(unit)}" ${enabled && upper > 1 ? '' : 'disabled'}>`
    + `<span class="quantity-slider-limits" aria-hidden="true"><span>1</span><span>${upper}</span></span></label>`;
}

// 用途：拖动时局部更新数量、总价与提交数据，不重建被拖动的DOM。
// 流程：input和change事件共用；用法：传当前滑条，交易行需提供权威单价及余额属性；返回：规范化后的数量。
export function updateQuantitySlider(input) {
  const selected = quantityValue(input.value, input.max);
  input.value = String(selected);
  const wrapper = input.closest('[data-quantity-slider]');
  const output = wrapper?.querySelector('[data-quantity-value]');
  if (output) output.textContent = String(selected);
  const row = input.closest('[data-quantity-row]');
  const button = row?.querySelector('[data-quantity-submit]');
  if (button) {
    const unitPrice = Number(button.dataset.unitPrice), balance = Number(button.dataset.balance);
    const total = unitPrice * selected;
    const priced = Number.isSafeInteger(total) && total > 0;
    const permitted = button.dataset.permitted === 'true';
    button.dataset.count = String(selected);
    button.disabled = !permitted || !priced || balance < total;
    const reason = button.dataset.blockReason || '暂不可购买';
    button.textContent = !permitted ? reason : !priced ? '报价待刷新'
      : balance < total ? `还差 ${total - balance} 灵石` : `买入 · ${total} 灵石`;
    const totalLabel = row.querySelector('[data-quantity-total]');
    if (totalLabel) totalLabel.textContent = priced ? `合计 ${total} 灵石` : '报价待刷新';
  }
  return selected;
}

// 用途：把当前坊市收购资金直接贴在丹器处置入口旁；流程：工坊与行囊物品售卖。
// 用法：传完整后端state，只显示权威余额；返回：安全HTML，缺值显示待查而不猜测资金。
export function renderMarketCash(state) {
  const balance = state?.world?.marketCash ?? state?.marketCash;
  return `<p class="trade-market-cash" data-trade-market-cash>坊市剩余收购资金 <strong>${Number.isSafeInteger(balance) && balance >= 0 ? balance : '待查'}</strong> 灵石</p>`;
}

// 用途：展示每种丹药的一枚与最大可售交易入口；流程：工坊丹药行囊和行囊详情共用。
// 用法：传丹药稳定ID、后端maxSale和state；返回：简单按钮及当前收购资金，不显示最大数量或总收益。
export function renderPotionSaleActions(id, quote, state) {
  const count = Number(quote?.count), unitPrice = Number(quote?.unitPrice), total = Number(quote?.totalGain);
  const valid = Number.isSafeInteger(count) && count > 0 && Number.isSafeInteger(unitPrice) && unitPrice > 0
    && Number.isSafeInteger(total) && total > 0;
  const enabled = quote?.enabled === true && valid;
  const reason = quote?.reason || (!quote ? '出售报价待加载' : enabled ? '' : '暂不可出售');
  const attributes = `data-kind="potion" data-id="${esc(id)}" data-unit-price="${valid ? unitPrice : 0}"`;
  return `<div class="pill-sale-actions"><div class="pill-sale-buttons">`
    + `<button data-action="sell" ${attributes} data-count="1" ${enabled ? '' : 'disabled'} title="${esc(reason)}">出售1枚</button>`
    + `<button class="primary" data-action="sell-max" ${attributes} data-count="${valid ? count : 0}" ${enabled ? '' : 'disabled'} title="${esc(reason)}">`
    + `一键售出</button></div>` + renderMarketCash(state)
    + (reason ? `<p class="trade-limit-reason">${esc(reason)}</p>` : '')
    + `</div>`;
}
