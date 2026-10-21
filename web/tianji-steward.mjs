const escapeHtml = value => String(value ?? '').replace(/[&<>"']/g, char => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[char]));
const labels = { potion: '突破丹', herb: '高阶药材', material: '炼器矿材' };

// 可嵌入经典/v2的坊市或天机阁服务页，所有价格/可购数量消费后端报价。
export function renderTianjiSteward(view, { kind = 'potion' } = {}) {
  if (!view) return '';
  const stewards = view.stewards || [];
  const current = stewards.find(steward => steward.id === view.npc);
  const cards = stewards.map(steward => `<article class="tianji-steward-person ${steward.isHere ? 'is-here' : ''}"><strong>${escapeHtml(steward.name)}</strong><span>${escapeHtml(steward.realm)} · 天机阁驻城执事</span><small>${escapeHtml(steward.locationName)}分号 · ${steward.isHere ? '正在此地，可当面交易' : '需前往当地交易'}</small></article>`).join('');
  const entries = (view.offers || []).filter(offer => offer.kind === kind).map(offer => {
    const quote = offer.quote || {};
    return `<article class="tianji-steward-offer" data-steward-row="${escapeHtml(offer.id)}"><div><strong>${escapeHtml(offer.name)}</strong><small>${Number(offer.grade)}阶 · 固定单价 ${Number(offer.fixedUnitPrice)} 灵石 · 已有 ${Number(quote.owned || 0)}</small><p>${escapeHtml(offer.description)}</p></div><div class="tianji-steward-controls"><label>数量<input aria-label="${escapeHtml(offer.name)}购买数量" data-steward-count="${escapeHtml(offer.id)}" type="number" min="1" max="1000" step="1" value="1" ${view.isHere ? '' : 'disabled'}></label><span data-steward-total>总价 ${Number(quote.totalCost || 0)} 灵石</span><button type="button" data-steward-buy="${escapeHtml(offer.id)}" ${quote.enabled && view.isHere ? '' : 'disabled'}>购买</button><small data-steward-reason>${escapeHtml(quote.reason)}</small></div></article>`;
  }).join('');
  return `<section class="tianji-steward" data-steward-shop="${escapeHtml(view.npc)}"><h3>天机阁执事交易</h3><div class="tianji-steward-people">${cards}</div><p>${escapeHtml(view.fixedPriceNote)}</p><p class="muted">${escapeHtml(view.methodNote)}</p><div role="tablist" aria-label="执事货架分类" class="tianji-steward-tabs">${Object.entries(labels).map(([id, name]) => `<button type="button" role="tab" data-steward-kind="${id}" aria-selected="${id === kind}">${name}</button>`).join('')}</div><p>${current ? `${escapeHtml(current.name)} · ${escapeHtml(current.locationName)}分号` : '请前往五城天机阁分号当面交易。'}</p><p role="status" aria-live="polite" data-steward-status></p><div class="tianji-steward-offers">${entries}</div></section>`;
}

// mount的生命周期结束必须调用返回的dispose；输入报价有版本与容器存活检查，购买失败保留数量与位置。
// request(path, body)返回解包JSON；购买路由返回现有动作state，由onPurchased刷新主状态。
export function mountTianjiSteward(root, { request, onPurchased, onKindChange } = {}) {
  const host = root?.matches?.('[data-steward-shop]') ? root : root?.querySelector?.('[data-steward-shop]');
  if (!host) return () => {};
  let disposed = false;
  let buying = false;
  const versions = new Map();
  const quotes = new Map();
  const timers = new Map();
  const isLive = () => !disposed && host.isConnected;
  const status = message => { if (isLive()) host.querySelector('[data-steward-status]').textContent = message; };
  const post = request || (async (path, body) => {
    const response = await fetch(path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    const result = await response.json();
    if (!response.ok || result.ok === false || result.error) throw new Error(result.error || '执事交易失败，请重试。');
    return result;
  });
  async function quoteRow(input) {
    const id = input.dataset.stewardCount;
    const version = (versions.get(id) || 0) + 1;
    versions.set(id, version);
    const row = input.closest('[data-steward-row]');
    const button = row.querySelector('[data-steward-buy]');
    const reason = row.querySelector('[data-steward-reason]');
    button.disabled = true; quotes.delete(id);
    const count = Number(input.value);
    if (!Number.isInteger(count) || count < 1 || count > 1000) { reason.textContent = '购买数量须为1至1000。'; return; }
    reason.textContent = '正在核对报价…';
    try {
      const result = await post('/api/tianji-shop/quote', { npc: host.dataset.stewardShop, offer: id, count });
      if (!isLive() || versions.get(id) !== version) return;
      const quote = result.quote || result;
      quotes.set(id, quote);
      row.querySelector('[data-steward-total]').textContent = `总价 ${Number(quote.totalCost || 0)} 灵石`;
      reason.textContent = quote.reason || '';
      button.disabled = buying || !quote.enabled;
    } catch (error) {
      if (isLive() && versions.get(id) === version) reason.textContent = error.message;
    }
  }
  const inputListener = event => {
    const input = event.target.closest('[data-steward-count]');
    if (!input || buying) return;
    const id = input.dataset.stewardCount;
    clearTimeout(timers.get(id));
    versions.set(id, (versions.get(id) || 0) + 1);
    quotes.delete(id); input.closest('[data-steward-row]').querySelector('[data-steward-buy]').disabled = true;
    timers.set(id, setTimeout(() => quoteRow(input), 180));
  };
  const clickListener = async event => {
    const category = event.target.closest('[data-steward-kind]');
    if (category) { if (!buying) onKindChange?.(category.dataset.stewardKind); return; }
    const button = event.target.closest('[data-steward-buy]');
    if (!button || button.disabled || buying) return;
    buying = true;
    const row = button.closest('[data-steward-row]');
    const input = row.querySelector('[data-steward-count]');
    const id = button.dataset.stewardBuy;
    const count = Number(input.value);
    const previousDisabled = new Map();
    host.querySelectorAll('[data-steward-buy],[data-steward-count],[data-steward-kind]').forEach(node => { previousDisabled.set(node, node.disabled); });
    host.querySelectorAll('[data-steward-buy],[data-steward-count],[data-steward-kind]').forEach(node => { node.disabled = true; });
    status('正在当面交易…');
    try {
      let quote = quotes.get(id);
      if (!quote || quote.count !== count) {
        const result = await post('/api/tianji-shop/quote', { npc: host.dataset.stewardShop, offer: id, count });
        quote = result.quote || result;
      }
      if (!isLive()) return;
      if (!quote.enabled) throw new Error(quote.reason || '当前无法购买。');
      const result = await post('/api/tianji-shop/buy', { npc: host.dataset.stewardShop, offer: id, count, expectedCost: quote.totalCost });
      if (!isLive()) return;
      status('购买成功。');
      await onPurchased?.(result);
    } catch (error) {
      status(error.message || '交易未完成，请刷新报价后重试。');
    } finally {
      buying = false;
      if (isLive()) {
        previousDisabled.forEach((disabled, node) => { node.disabled = disabled; });
        // 其余条目在提交前仍会重新报价；当前行刷新余额和库存后才能再次购买。
        quotes.clear();
        host.querySelectorAll('[data-steward-count]').forEach(node => { if (node === input) void quoteRow(node); });
      }
    }
  };
  host.addEventListener('input', inputListener); host.addEventListener('click', clickListener);
  return () => { disposed = true; timers.forEach(clearTimeout); host.removeEventListener('input', inputListener); host.removeEventListener('click', clickListener); };
}
