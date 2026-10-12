// View helpers: all server/user text crosses an escaping boundary.
export const esc = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
export const fmt = n => typeof n === 'number' ? n.toLocaleString('zh-CN', {maximumFractionDigits: 2}) : esc(n ?? '—');
export function action(label, path, body = {}, options = {}) {
  return `<button type="button" class="btn ${esc(options.className || '')}" data-command="${esc(path)}" data-body="${esc(JSON.stringify(body))}"${options.disabled ? ' disabled' : ''}${options.title ? ` title="${esc(options.title)}"` : ''}>${esc(label)}</button>`;
}
export const card = (title, body, extra = '') => `<section class="card ${esc(extra)}">${title ? `<h2 class="section-title">${esc(title)}</h2>` : ''}${body}</section>`;
export const empty = text => `<p class="empty">${esc(text)}</p>`;
export const tag = text => `<span class="badge">${esc(text)}</span>`;
export const tabs = (group, items, active) => `<nav class="subnav" aria-label="${esc(group)}分类">${items.map(item => { const [id, label] = Array.isArray(item) ? item : [item.id,item.label || item.name]; return `<button type="button" data-ui="${esc(group)}-tab" data-value="${esc(id)}" aria-pressed="${id === active}" class="${id === active ? 'selected' : ''}">${esc(label)}</button>`; }).join('')}</nav>`;
export const numericPreview = p => !p ? '' : `<div class="numeric-preview">${p.text ? `<strong>${esc(p.text)}</strong>` : ''}${p.conditions ? `<p>${esc(p.conditions)}</p>` : ''}${p.context ? `<p class="meta">${esc(p.context)}</p>` : ''}${p.reason ? `<p>${esc(p.reason)}</p>` : ''}</div>`;
export const meter = (value, max, label, tone = '') => `<div class="meter-label"><span>${esc(label)}</span><b>${fmt(value)} <span>/ ${fmt(max)}</span></b></div><progress class="${esc(tone)}" value="${Number(value) || 0}" max="${Number(max) || 1}" aria-label="${esc(label)}"></progress>`;
export const uiButton = (label, id, value = '', cls = '') => `<button type="button" class="btn ${esc(cls)}" data-ui="${esc(id)}" data-value="${esc(value)}">${esc(label)}</button>`;
export const legacyLink = (label = '在经典界面打开完整功能', hash = '') => `<a class="btn legacy-link" href="index.html${esc(hash)}">${esc(label)} ↗</a>`;
