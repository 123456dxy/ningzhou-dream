import { esc } from './shell.mjs';

// 用途：展示服务端核算的一键补料明细和预计灵石。
// 流程：检查报价是否对应当前配方，列出缺口与货源，用 enabled 控制确认购买。
// 调用方式：炼丹或炼器子页传入只读报价响应、当前 payload 和操作名称。
// 返回值：补料区域 HTML；报价未就绪时只提供查询按钮，不估算价格。
export function renderCraftSupply(quote, payload) {
  const request = esc(JSON.stringify(payload));
  if (!quote) return `<div class="craft-supply"><button data-action="craft-supply-quote" data-payload="${request}">查看一键补料报价</button><p class="panel-note">按选定配方与炉数，只补不足材料；先查看当地货源与预计灵石。</p></div>`;
  if (quote.error) return `<div class="craft-supply"><p class="card-why">${esc(quote.error)}</p><button data-action="craft-supply-quote" data-payload="${request}">重新查询补料</button></div>`;
  const rows = quote.rows || quote.materials || [];
  const cost = quote.cost ?? quote.totalCost;
  return `<div class="craft-supply"><h4>一键补料</h4><div class="craft-supply-rows">`
    + rows.map(item => `<p><strong>${esc(item.name)}</strong> · 需 ${esc(item.required)}，已有 ${esc(item.owned)}，补 ${esc(item.missing ?? item.buyCount ?? 0)}${item.reason ? ` · ${esc(item.reason)}` : ''}</p>`).join('')
    + `</div><p>预计消耗 <b>${Number.isFinite(cost) ? esc(cost) + ' 灵石' : '报价待查'}</b></p>`
    + (quote.reason ? `<p class="card-why">${esc(quote.reason)}</p>` : '')
    + `<button class="primary" data-action="craft-supply-buy" data-payload="${request}" ${quote.enabled === true && Number.isFinite(cost) ? '' : 'disabled'}>确认购买缺少材料</button>`
    + `<button data-action="craft-supply-quote" data-payload="${request}">刷新报价</button></div>`;
}

// 用途：在法宝材料回收前展示真实返还材料与手续费。
// 流程：读取服务端报价中的材料和费用，生成明确的确认与取消按钮。
// 调用方式：行囊“材料回收预览”加载只读报价后插入 modal。
// 返回值：回收确认弹窗 HTML；不在预览阶段出售或拆解法宝。
export function renderSalvageQuote(gear, quote) {
  const materials = quote.materials || quote.returns || [];
  const fee = quote.fee ?? quote.costStones;
  return `<div class="modal-inner"><h2>材料回收 · ${esc(gear.name)}</h2><p>回收后该法宝会消失，请先核对材料和费用。</p>`
    + `<ul>${materials.map(item => `<li>${esc(item.name)} ×${esc(item.count ?? item.amount)}</li>`).join('') || '<li>没有可返还材料。</li>'}</ul>`
    + `<p>手续费：${Number.isFinite(fee) ? esc(fee) + ' 灵石' : '待查'}</p><p>${esc(quote.reason || quote.error || '')}</p>`
    + (quote.sourceNote ? `<p class="panel-note">${esc(quote.sourceNote)}</p>` : '')
    + (quote.estimated ? '<p class="panel-note">这件旧法宝没有完整配料记录；以上按现有信息估算。</p>' : '')
    + `<div class="year-controls"><button class="primary" data-action="gear-salvage-confirm" data-id="${esc(gear.id)}" ${quote.enabled === true && Number.isFinite(fee) ? '' : 'disabled'}>确认材料回收</button><button data-action="preview-close">取消</button></div></div>`;
}
