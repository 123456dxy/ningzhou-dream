// 宁州一梦 · C++ 版 — 转世传承与结局界面
//
// 对应 JS app.mjs 的 showEnding / exportSave / showNew 里的传承部分。
// 之前 C++ 只有一个布尔值 ended，结局结算画面等于没有。

'use strict';

const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

let cache = null;

export async function loadLegacy(force) {
  if (cache && !force) return cache;
  cache = await fetch('/api/legacy', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(r => r.json()).catch(() => null);
  return cache;
}

export function invalidateLegacy() { cache = null; }

export async function draftTalents(seed, roll) {
  return fetch('/api/legacy/draft', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ seed, roll }),
  }).then(r => r.json()).catch(() => null);
}

export async function settleLife() {
  const r = await fetch('/api/legacy/settle', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(x => x.json()).catch(() => null);
  invalidateLegacy();
  return r;
}

// ------------------------------------------------------------
// 结局结算画面
// ------------------------------------------------------------
export function renderEnding(state, leg) {
  const e = state.ending || {};
  const ms = (leg?.milestones || []);
  const reached = ms.filter(m => m.reached);
  const fresh = reached.filter(m => !m.claimed);
  const cp = state.checkpoint;
  const canRestore = e.canRestore === true && cp?.enabled;
  const checkpointDay = Number.isFinite(cp?.days)
    ? `第 ${Math.floor(cp.days / 360) + 1} 年 ${Math.floor(cp.days % 360 / 30) + 1} 月 ${cp.days % 30 + 1} 日`
    : '最近保存时';
  const checkpointSource = cp?.source === 'manual' ? `手动存档位 ${cp.slot || 1}` : '十年自动存档';

  const list = reached.length
    ? reached.map(m => `<div class="pill-row">
        <span>${esc(m.label)}</span>
        <span class="item-sub">${m.claimed ? '已计入' : `+${m.points} 传承点`}</span>
      </div>`).join('')
    : `<div class="empty">这一世没有达成新的里程碑。</div>`;

  return `<div class="ending-card">
    <div class="ending-head">
      <h2>这一世落笔</h2>
      <p>${esc(e.cause || '暂别仙途')}</p>
    </div>
    ${canRestore ? `<div class="ending-restore" role="region" aria-label="陨落后的选择">
      <div><strong>仙途未必就此结束</strong>
        <p>接受这一世的结局，或回到最近存档继续修行。回档会撤回保存之后的进度。</p>
        <small>${esc(checkpointSource)} · ${esc(checkpointDay)}</small></div>
      <div class="ending-restore-actions"><button class="primary" data-action="checkpoint-restore">回到最近存档</button>
        <button data-action="legacy-settle">接受死亡并结算</button></div>
    </div>` : ''}
    <div class="ending-stats">
      <div><label>享年</label><strong>${e.age ?? '—'} 岁</strong></div>
      <div><label>最终境界</label><strong>${esc(e.realm || '—')}</strong></div>
      <div><label>知交</label><strong>${e.friends ?? 0} 位</strong></div>
      <div><label>评分</label><strong>${e.score ?? 0}</strong></div>
    </div>
    <h3>本世成就</h3>
    ${list}
    <div class="ending-foot">
      <div class="ending-gain">可换 <b>${fresh.reduce((a, m) => a + m.points, 0)}</b> 点传承
        · 当前累计 <b>${leg?.earned ?? 0}</b> / 30</div>
      <div class="year-controls">
        ${canRestore ? '' : '<button class="primary" data-action="legacy-settle">结算这一世</button>'}
        <button data-action="legacy-new">再写一世</button>
      </div>
      <p class="panel-note">传承点会提高下一世的建号预算（当前 ${leg?.budget ?? 10} 点），
        建号时会按类别展示全部天赋，供你自由挑选。</p>
    </div>
  </div>`;
}

// ------------------------------------------------------------
// 传承档案（设置页里展示）
// ------------------------------------------------------------
export function renderLegacyPanel(leg) {
  if (!leg) return '<div class="empty">传承档案未加载。</div>';
  const ms = leg.milestones || [];
  const done = ms.filter(m => m.claimed);
  return `<div class="detail-list" style="border-top:0;padding-top:0">
      <div><label>已转世</label><strong>${leg.runs} 世</strong></div>
      <div><label>传承点</label><strong>${leg.earned} / ${leg.maxPoints}</strong></div>
      <div><label>建号预算</label><strong>${leg.budget} 点</strong></div>
      <div><label>已达成里程碑</label><strong>${done.length} / ${ms.length}</strong></div>
    </div>
    ${done.length
      ? done.map(m => `<div class="pill-row"><span>${esc(m.label)}</span>
          <span class="item-sub">+${m.points}</span></div>`).join('')
      : '<div class="empty">还没有达成任何里程碑。</div>'}`;
}

// ------------------------------------------------------------
// 建号抽天赋面板
// ------------------------------------------------------------
export function renderDraftPanel(draft, roll, seed, budget, selected) {
  if (!draft) return '<div class="empty">正在抽取天赋…</div>';
  const total = (draft.talents || []).reduce(
    (a, t) => a + (selected.has(t.id) ? t.cost : 0), 0);

  return `<div class="draft-head">
      <span>从 12 项候选中挑选（可重抽两次）</span>
      <span>已用 <b>${total}</b> / ${budget} 点</span>
    </div>
    <div class="draft-grid">
      ${(draft.talents || []).map(t => `
        <button class="draft-card ${selected.has(t.id) ? 'picked' : ''}"
                data-draft="${esc(t.id)}">
          <div class="draft-top"><strong>${esc(t.name)}</strong>
            <span class="cost ${t.cost < 0 ? 'neg' : ''}">
              ${t.cost > 0 ? `−${t.cost}` : t.cost < 0 ? `+${-t.cost}` : '0'}</span></div>
          <div class="draft-tag">${esc(t.tag)}</div>
          <div class="draft-desc">${esc(t.desc)}</div>
        </button>`).join('')}
    </div>
    <div class="year-controls" style="margin-top:12px">
      <button data-action="legacy-reroll" ${roll >= 2 ? 'disabled' : ''}>
        重抽（剩 ${2 - roll} 次）</button>
      <button data-action="legacy-seed" data-seed="${seed}">换个种子</button>
    </div>`;
}
