// Position snapshots belong to a rendered workspace scope, never a DOM index.
const anchorAttributes = ['data-inventory-item', 'data-selected-item', 'data-selected-spell', 'data-spell-id', 'data-selected-recipe', 'data-ui-anchor'];
function stableSelector(element) {
  if (!element) return null;
  if (element.id) return `#${CSS.escape(element.id)}`;
  const attributes = ['data-ui-scroll', 'data-disclosure-key', 'data-inventory-pill-details', ...anchorAttributes, 'data-input', 'data-pill-count', 'data-brew-search', 'data-brew-grade', 'data-brew-batch', 'data-spell-dao', 'data-ui', 'data-action'];
  for (const attribute of attributes) if (element.hasAttribute(attribute)) {
    let selector = `${element.tagName.toLowerCase()}[${attribute}="${CSS.escape(element.getAttribute(attribute))}"]`;
    for (const suffix of ['type', 'data-id', 'data-value', 'data-field', 'data-kind']) if (element.hasAttribute(suffix)) selector += `[${suffix}="${CSS.escape(element.getAttribute(suffix))}"]`;
    return selector;
  }
  for (const className of ['home-secondary', 'player-picker', 'bounded-log']) if (element.classList.contains(className)) return `.${className}`;
  return null;
}
export function captureUiPosition(root, scopeKey) {
  if (!root) return null;
  const active = root.contains(document.activeElement) ? document.activeElement : null;
  const anchors = [...root.querySelectorAll(anchorAttributes.map(attribute => `[${attribute}]`).join(','))]
    .map(element => ({ selector: stableSelector(element), top: element.getBoundingClientRect().top }))
    .filter(anchor => anchor.selector).sort((a, b) => Math.abs(a.top - 100) - Math.abs(b.top - 100));
  const details = [...root.querySelectorAll('details')].map(element => ({ selector: stableSelector(element), summary: element.querySelector(':scope > summary')?.textContent?.trim(), open: element.open }));
  const scroll = [...root.querySelectorAll('[data-ui-scroll],.main-content,.home-secondary,.player-picker,.bounded-log')]
    .map(element => ({ selector: stableSelector(element), top: element.scrollTop, left: element.scrollLeft })).filter(entry => entry.selector);
  return { scopeKey, top: window.scrollY, left: window.scrollX, anchors, details, scroll,
    focus: stableSelector(active), selectionStart: active?.selectionStart, selectionEnd: active?.selectionEnd };
}
export function restoreUiPosition(root, snapshot, scopeKey) {
  if (!root || !snapshot || snapshot.scopeKey !== scopeKey) return;
  for (const entry of snapshot.details) {
    const element = entry.selector ? root.querySelector(entry.selector) : [...root.querySelectorAll('details')].find(candidate => candidate.querySelector(':scope > summary')?.textContent?.trim() === entry.summary);
    if (element) element.open = entry.open;
  }
  for (const entry of snapshot.scroll) { const element = root.querySelector(entry.selector); if (element) { element.scrollTop = entry.top; element.scrollLeft = entry.left; } }
  window.scrollTo({ left: snapshot.left, top: snapshot.top, behavior: 'instant' });
  for (const anchor of snapshot.anchors) {
    const element = root.querySelector(anchor.selector);
    if (element) {
      const delta = element.getBoundingClientRect().top - anchor.top;
      let container = element.parentElement;
      while (container && container !== document.body) {
        if (/(auto|scroll)/.test(getComputedStyle(container).overflowY) && container.scrollHeight > container.clientHeight) break;
        container = container.parentElement;
      }
      if (container && container !== document.body) container.scrollTop += delta;
      else window.scrollBy({ top: delta, behavior: 'instant' });
      break;
    }
  }
  const focused = snapshot.focus && root.querySelector(snapshot.focus);
  focused?.focus({ preventScroll: true });
  if (focused?.setSelectionRange && typeof snapshot.selectionStart === 'number') {
    try { focused.setSelectionRange(snapshot.selectionStart, snapshot.selectionEnd); } catch { /* Range/number fields do not support selection. */ }
  }
}
