// 宁州一梦 · 手机端字号兜底（仅 ≤690px 生效，桌面端不运行）
// 各面板 CSS 在窄屏下把不少文字压到 8～13px；逐条改几百个选择器易漏，
// 这里在渲染后给「自身含文字且计算字号 <14px」的元素加 .nz-fs14，由 mobile.css 提到 14px。
const MQ = window.matchMedia('(max-width: 690px)');
const SKIP = '.map-pin, .atlas-dungeon-pin, .atlas-compass, .landscape, svg, .nz-fs-skip';
let queued = new Set(), raf = 0;

function scan(root) {
  if (!MQ.matches || !(root instanceof Element)) return;
  const list = [root, ...root.querySelectorAll('*')];
  for (const el of list) {
    if (el.classList.contains('nz-fs14') || el.closest(SKIP)) continue;
    let own = false;
    for (const n of el.childNodes) if (n.nodeType === 3 && n.textContent.trim()) { own = true; break; }
    if (!own && !el.matches('input, select, textarea')) continue;
    if (parseFloat(getComputedStyle(el).fontSize) < 14) el.classList.add('nz-fs14');
  }
}
function flush() { raf = 0; const q = [...queued]; queued.clear(); q.forEach(scan); }
function queue(n) {
  const el = n.nodeType === 1 ? n : n.parentElement;
  if (el) queued.add(el);
  if (!raf) raf = requestAnimationFrame(flush);
}
new MutationObserver(ms => { if (!MQ.matches) return; for (const m of ms) { if (m.type === 'characterData') queue(m.target); else m.addedNodes.forEach(queue); } })
  .observe(document.body, { childList: true, subtree: true, characterData: true });
MQ.addEventListener('change', () => queue(document.body));
queue(document.body);
