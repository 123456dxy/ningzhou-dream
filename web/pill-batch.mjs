import { esc } from './shell.mjs';
const bindings = new WeakMap();
export function renderPillBatchControls(id) {
  return `<section class="pill-batch" data-pill-batch="${esc(id)}"><label>服用数量 <input type="range" min="1" max="1" value="1" data-pill-count="${esc(id)}" aria-label="服用数量滑条" disabled><input type="number" min="1" max="1000" value="1" data-pill-count="${esc(id)}" aria-label="服用数量" disabled></label><p data-pill-quote role="status">展开后读取可服数量；一次最多1,000枚。</p><div class="arts-actions"><button type="button" class="primary" data-pill-batch-use disabled>服用所选数量</button><button type="button" data-pill-batch-max disabled>最多可服</button><button type="button" data-pill-batch-retry hidden>重新报价</button></div></section>`;
}
export function bindPillBatchControls(root, options) {
  let binding = bindings.get(root);
  if (!binding) {
    binding = { options, selections: new Map(), pending: new Map(), timers: new Map(), locked: false };
    bindings.set(root, binding);
    root.addEventListener('input', event => {
      if (!event.target.matches('[data-pill-count]')) return;
      if (binding.locked || binding.options.isBusy()) return;
      const section = event.target.closest('[data-pill-batch]');
      const value = Number(event.target.value);
      const scope = binding.options.scope(), key = `${scope}:${section.dataset.pillBatch}`;
      binding.selections.set(key, value);
      section.querySelectorAll('[data-pill-count]').forEach(input => { if (input !== event.target) input.value = event.target.value; });
      section.querySelectorAll('[data-pill-batch-use],[data-pill-batch-max]').forEach(button => button.disabled = true);
      // Invalidate the in-flight response before the debounce, so old choices never win.
      binding.pending.set(key, Symbol());
      clearTimeout(binding.timers.get(key)?.timer);
      const scheduled = { scope, section, timer: null };
      scheduled.timer = setTimeout(() => {
        if (binding.timers.get(key) !== scheduled) return;
        binding.timers.delete(key);
        if (section.isConnected && binding.options.scope() === scope) refresh(section);
      }, 150);
      binding.timers.set(key, scheduled);
    });
    root.addEventListener('toggle', event => {
      if (event.target.tagName === 'DETAILS' && event.target.open) event.target.querySelectorAll('[data-pill-batch]').forEach(refresh);
    }, true);
    root.addEventListener('click', async event => {
      const button = event.target.closest('[data-pill-batch-use],[data-pill-batch-max],[data-pill-batch-retry]');
      if (!button || button.disabled || !root.contains(button)) return;
      event.preventDefault(); event.stopPropagation();
      const section = button.closest('[data-pill-batch]');
      if (binding.locked || binding.options.isBusy()) return;
      if (button.hasAttribute('data-pill-batch-retry')) { refresh(section); return; }
      const quote = section._pillQuote;
      const count = button.hasAttribute('data-pill-batch-max') ? quote?.maxUsableCount : Number(section.querySelector('input[type="number"]').value);
      if (!quote || !Number.isInteger(count) || count < 1 || count > quote.maxUsableCount) return;
      binding.locked = true;
      root.querySelectorAll('[data-pill-batch]').forEach(current => current.querySelectorAll('button,input').forEach(element => element.disabled = true));
      try { await binding.options.onUse({ id: section.dataset.pillBatch, count }); }
      finally {
        binding.locked = false;
        root.querySelectorAll('[data-pill-batch]').forEach(refresh);
      }
    }, true);
    async function refresh(section) {
      if (!section.isConnected) return;
      const details = section.closest('details');
      if (details && !details.open) return;
      const scope = binding.options.scope(), id = section.dataset.pillBatch;
      const key = `${scope}:${id}`, token = Symbol(); binding.pending.set(key, token);
      const count = binding.selections.get(key) ?? 1;
      section.querySelectorAll('[data-pill-count]').forEach(input => input.value = String(count));
      const status = section.querySelector('[data-pill-quote]');
      status.textContent = '正在核对本批实际药效…';
      section.querySelectorAll('button').forEach(button => button.disabled = true);
      if (binding.locked || binding.options.isBusy()) section.querySelectorAll('[data-pill-count]').forEach(input => input.disabled = true);
      try {
        const result = await binding.options.requestQuote({ id, count });
        if (!section.isConnected || scope !== binding.options.scope() || binding.pending.get(key) !== token) return;
        const quote = result.quote || result;
        if (result.error || !Number.isInteger(quote.maxUsableCount)) throw new Error(result.error || '报价未完整返回。');
        section._pillQuote = quote;
        const maximum = quote.maxUsableCount;
        const locked = binding.locked || binding.options.isBusy();
        section.querySelectorAll('[data-pill-count]').forEach(input => { input.max = String(Math.max(1, maximum)); input.disabled = locked || maximum < 1; });
        status.textContent = `${quote.enabled ? quote.effect || '' : quote.reason || ''}；${quote.toleranceRemainingAfter >= 0 ? `服后耐药剩余 ${quote.toleranceRemainingAfter} 次；` : ''}当前最多可服 ${maximum} 枚；一次最多1,000枚。${quote.stopReason || ''}`;
        section.querySelector('[data-pill-batch-use]').disabled = locked || quote.enabled !== true;
        section.querySelector('[data-pill-batch-max]').disabled = locked || maximum < 1;
        section.querySelector('[data-pill-batch-max]').textContent = `最多可服 ${maximum} 枚`;
        section.querySelector('[data-pill-batch-retry]').hidden = true;
      } catch (error) {
        if (!section.isConnected || scope !== binding.options.scope() || binding.pending.get(key) !== token) return;
        status.textContent = error.message || '报价失败，请重试。';
        const retry = section.querySelector('[data-pill-batch-retry]'); retry.hidden = false; retry.disabled = binding.locked || binding.options.isBusy();
      }
    }
    binding.refresh = refresh;
  }
  binding.options = options;
  for (const [key, scheduled] of binding.timers) {
    if (scheduled.scope !== options.scope() || !scheduled.section.isConnected) {
      clearTimeout(scheduled.timer); binding.timers.delete(key);
    }
  }
  root.querySelectorAll('[data-pill-batch]').forEach(binding.refresh);
}
