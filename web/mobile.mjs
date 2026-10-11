// 手机展示增强：不访问 API、不触碰游戏状态；所有视觉覆盖限定在窄屏。
const MQ = window.matchMedia('(max-width: 690px)');
const SKIP = 'svg, .landscape, .atlas-compass, .map-pin, .atlas-dungeon-pin, .nz-fs-skip';
const queued = new Set();
let frame = 0;

// 旧面板有许多内联和动态 8～13px 字号。仅给有文字的叶节点兜底，保留标题层级。
function scan(root) {
  if (!MQ.matches || !(root instanceof Element) || !root.isConnected) return;
  for (const el of [root, ...root.querySelectorAll('*')]) {
    if (el.classList.contains('nz-fs14') || el.closest(SKIP)) continue;
    const ownText = [...el.childNodes].some(n => n.nodeType === Node.TEXT_NODE && n.textContent.trim());
    if (!ownText || el.matches('input, select, textarea')) continue;
    if (parseFloat(getComputedStyle(el).fontSize) < 14) el.classList.add('nz-fs14');
  }
}

const sidebar = document.querySelector('.sidebar');
const toggle = document.createElement('button');
toggle.type = 'button';
toggle.className = 'nz-character-toggle';
toggle.setAttribute('aria-controls', 'player-card progress-card npc-card');
toggle.setAttribute('aria-expanded', 'false');
toggle.innerHTML = '<span><span class="nz-character-name">人物与修行</span><span class="nz-character-disclosure">展开 ＋</span></span><small class="nz-character-summary"></small>';
sidebar?.querySelector('.brand')?.after(toggle);
toggle.addEventListener('click', () => {
  const open = sidebar.classList.toggle('nz-character-open');
  toggle.setAttribute('aria-expanded', String(open));
  toggle.querySelector('.nz-character-disclosure').textContent = open ? '收起 −' : '展开 ＋';
});

// 次要功能折叠到明确的菜单，存档入口仍常驻；桌面不隐藏任何工具。
const toolbar = document.querySelector('.toolbar');
const toolsToggle = document.createElement('button');
toolsToggle.type = 'button';
toolsToggle.className = 'subtle nz-tools-toggle';
toolsToggle.textContent = '账号 · 工具 ＋';
toolsToggle.setAttribute('aria-expanded', 'false');
toolsToggle.addEventListener('click', () => {
  const open = toolbar.classList.toggle('nz-tools-open');
  toolsToggle.setAttribute('aria-expanded', String(open));
  toolsToggle.textContent = open ? '收起工具 −' : '账号 · 工具 ＋';
});
toolbar?.append(toolsToggle);

// 常用页面常驻拇指区，其余入口沿用原导航；不复制业务处理逻辑。
const mainTabs = document.querySelector('.main-tabs');
const mobileNav = document.createElement('nav');
mobileNav.className = 'nz-mobile-nav';
mobileNav.setAttribute('aria-label', '手机常用页面');
mobileNav.innerHTML = [['chronicle', '纪事'], ['map', '舆图'], ['arts', '洞府'], ['market', '坊市']]
  .map(([id, label]) => `<button type="button" data-action="goto-tab" data-tab="${id}">${label}</button>`).join('')
  + '<button type="button" class="nz-nav-more" aria-expanded="false" aria-controls="nz-all-tabs">更多 ＋</button>';
if (mainTabs) mainTabs.id = 'nz-all-tabs';
document.body.append(mobileNav);
const navMore = mobileNav.querySelector('.nz-nav-more');
function closeNavigation() {
  document.body.classList.remove('nz-nav-open');
  navMore.setAttribute('aria-expanded', 'false');
}
navMore.addEventListener('click', () => {
  const open = document.body.classList.toggle('nz-nav-open');
  navMore.setAttribute('aria-expanded', String(open));
  if (open) mainTabs?.querySelector('button.active, button')?.focus();
});
document.addEventListener('click', event => {
  const destination = event.target.closest('.main-tabs button, .nz-mobile-nav [data-tab]');
  if (!MQ.matches) return;
  if (event.target.closest('.nz-map-return')) {
    document.querySelector('.atlas-frame')?.scrollIntoView({ block: 'start', behavior: 'auto' });
    return;
  }
  if (!destination) {
    if (!event.target.closest('.main-tabs, .nz-mobile-nav')) closeNavigation();
    return;
  }
  closeNavigation();
  requestAnimationFrame(() => document.getElementById('workspace')?.scrollIntoView({ block: 'start', behavior: 'auto' }));
});
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && MQ.matches && document.body.classList.contains('nz-nav-open')) {
    closeNavigation(); navMore.focus();
  }
});

function updateSummary() {
  const name = document.querySelector('#player-card .player-name')?.textContent.trim();
  const stats = [...document.querySelectorAll('#player-card .stat')];
  const value = label => stats.find(el => el.querySelector('label')?.textContent === label)?.querySelector('strong')?.textContent || '—';
  const title = name ? `${name} · ${value('境界')}` : '人物与修行';
  const summary = name ? `气血 ${value('气血')} · 灵石 ${value('灵石')} · 余寿 ${value('余寿')} 年` : '展开查看人物、境界与修炼进度';
  const titleEl = toggle.querySelector('.nz-character-name');
  const summaryEl = toggle.querySelector('.nz-character-summary');
  if (titleEl.textContent !== title) titleEl.textContent = title;
  if (summaryEl.textContent !== summary) summaryEl.textContent = summary;
}

function groupSecondaryTimeControls() {
  const group = document.querySelector('.nz-time-secondary');
  if (!MQ.matches) {
    if (group) group.replaceWith(...group.children);
    return;
  }
  if (group) return;
  const panel = document.querySelector('#workspace[data-page="chronicle"] .plan-panel:has(.plan-options)');
  if (!panel) return;
  const buttons = [...panel.children].filter(el => el.matches('button[data-days="3600"], button[data-action="advance-step"]'));
  if (buttons.length !== 2) return;
  const secondary = document.createElement('div');
  secondary.className = 'nz-time-secondary';
  buttons[0].before(secondary);
  secondary.append(...buttons);
}

function flush() {
  frame = 0;
  const roots = [...queued]; queued.clear();
  // 同一轮包含父节点时，无须重复扫描它的子树。
  roots.filter(el => !roots.some(parent => parent !== el && parent.contains(el))).forEach(scan);
  updateSummary();
  groupSecondaryTimeControls();
  const actionPanel = document.querySelector('#workspace[data-page="chronicle"] .plan-panel:has(.plan-options)');
  const result = document.querySelector('.action-feed .feed-item')?.textContent.trim();
  if (MQ.matches && actionPanel && result) {
    let nearby = actionPanel.querySelector('.nz-plan-feedback');
    if (!nearby) {
      nearby = document.createElement('p'); nearby.className = 'nz-plan-feedback';
      nearby.setAttribute('aria-label', '最近操作结果'); actionPanel.append(nearby);
    }
    const message = `最近：${result}`;
    if (nearby.textContent !== message) nearby.textContent = message;
  }
  const customOverlay = document.querySelector('.build-overlay, .story-overlay, #nz-login:not([hidden])');
  document.querySelector('.app-shell')?.toggleAttribute('inert', MQ.matches && !!customOverlay);
  document.getElementById('bp-name')?.setAttribute('aria-label', '角色道号');
  document.querySelectorAll('.plan-option').forEach(button => {
    button.setAttribute('aria-pressed', String(button.classList.contains('selected')));
    button.setAttribute('aria-label', button.textContent.replace(/\s+/g, ' ').trim());
  });
  const activeTab = document.querySelector('.main-tabs button.active')?.dataset.tab;
  mobileNav.querySelectorAll('[data-tab]').forEach(button => {
    const active = button.dataset.tab === activeTab;
    button.classList.toggle('active', active);
    if (active) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  });
  const secondary = activeTab && !mobileNav.querySelector(`[data-tab="${activeTab}"]`);
  navMore.classList.toggle('active', !!secondary);
  document.querySelectorAll('.main-tabs button').forEach(button => {
    if (button.classList.contains('active')) button.setAttribute('aria-current', 'page');
    else button.removeAttribute('aria-current');
  });
}
function queue(node) {
  const el = node.nodeType === Node.ELEMENT_NODE ? node : node.parentElement;
  if (el && !toggle.contains(el)) queued.add(el);
  if (!frame) frame = requestAnimationFrame(flush);
}
new MutationObserver(records => {
  for (const record of records) {
    if (record.type === 'characterData' || record.type === 'attributes') queue(record.target);
    else {
      record.addedNodes.forEach(queue);
      if (record.removedNodes.length) queue(record.target);
    }
  }
}).observe(document.body, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ['hidden'] });
MQ.addEventListener('change', () => queue(document.body));
document.body.classList.add('nz-mobile-ready');
queue(document.body);

if (sidebar && 'ResizeObserver' in window) {
  new ResizeObserver(() => {
    if (MQ.matches) document.documentElement.style.setProperty('--nz-status-height', `${sidebar.classList.contains('nz-character-open') ? 0 : sidebar.getBoundingClientRect().height}px`);
  }).observe(sidebar);
}
