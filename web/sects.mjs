// 宁州一梦 · C++ 版 — 五宗一览与成就面板
//
// 对应 JS app.mjs 的 sects 页（五宗卡 + 拜入山门）与 arts-ui 的成就列表。
// C++ 之前只能靠第 3 年的「山门来信」进金虹剑派，
// 26 部拓展功法又全靠宗门藏经阁，等于大半养成线被卡死。

'use strict';

const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

let sectCache = null;
let achCache = null;

export async function loadSects(force) {
  if (sectCache && !force) return sectCache;
  sectCache = await fetch('/api/sects', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(r => r.json()).catch(() => null);
  return sectCache;
}

export async function loadAchievements(force) {
  if (achCache && !force) return achCache;
  achCache = await fetch('/api/achievements', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(r => r.json()).catch(() => null);
  return achCache;
}

export function invalidateSects() { sectCache = null; achCache = null; }

// ------------------------------------------------------------
// 五宗一览
// ------------------------------------------------------------
export function renderSectPicker(data, player) {
  const list = data?.sects || [];
  if (!list.length) return '<div class="empty">宗门信息未加载。</div>';

  return `<div class="sect-grid">` + list.map(s => `
    <div class="sect-card ${s.current ? 'current' : ''}"
         style="--sect-color:${esc(s.color || '#8a8a8a')}">
      <div class="sect-name">${esc(s.name)}
        ${s.current ? '<span class="tag">已拜入</span>' : ''}</div>
      <div class="sect-motto">「${esc(s.motto || '')}」· ${esc(s.element || '')}属</div>
      <div class="sect-desc">${esc(s.description || '')}</div>
      <div class="sect-stats">
        <span>门人 ${s.members ?? 0}</span>
        <span>影响力 ${s.influence ?? 0}</span>
        <span>府库 ${s.treasury ?? 0}</span>
      </div>
      ${!s.current && s.trial ? `<div class="sect-trial-info"><strong>${esc(s.trial.name || '入门考核')}</strong>
        <p>${esc(s.trial.desc || '与本门弟子切磋，胜利后拜入山门。')}</p>
        <small>${s.trial.cost ?? 10} 灵石 · ${s.trial.days ?? 3} 天 · ${esc(s.trial.stageName || '炼气初期')}起</small>
      </div>` : ''}
      ${s.current
        ? '<div class="sect-foot">已是本门弟子</div>'
        : `<button class="sect-join" data-action="join-sect" data-sect="${esc(s.id)}"
                   ${s.enabled ? '' : 'disabled'}>
             ${s.enabled ? `报名入门考核（${s.trial?.cost ?? 10} 灵石）` : esc(s.reason || '暂不可入')}
           </button>
           ${player?.location !== s.id ? `<button class="subtle" data-action="sect-locate" data-id="${esc(s.id)}">在舆图中查看山门 →</button>` : ''}`}
    </div>`).join('') + `</div>`;
}

export function renderSectSpellLibrary(state) {
  const list = (state.librarySpells || []).filter(sp => !sp.sect || sp.sect === state.player.sect);
  if (!list.length) return '<div class="empty">本门暂未提供可请领的神通。</div>';
  const realms = ['炼气初期', '炼气中期', '炼气后期', '筑基初期', '筑基中期', '筑基后期', '金丹初期', '金丹中期', '金丹后期', '元婴初期', '元婴中期', '元婴后期', '化神初期', '化神中期', '化神后期'];
  return `<p class="panel-note">用贡献请领本门神通。学会后在「洞府道法 → 神通搭配」选择携带，可与主修功法和悟道搭配。</p>
    <div class="sect-spell-library">${list.map(sp => {
      const reason = sp.known ? '已学会' : sp.reason || (sp.enabled ? '' : '暂不可请领');
      return `<article class="sect-library-card${sp.known ? ' known' : ''}">
        <div class="sect-library-top"><strong>${esc(sp.name)}</strong><span>${esc(sp.dao)}道</span></div>
        <p>${esc(sp.desc || '')}</p>
        <small>所需境界：${esc(sp.stageName || realms[sp.stage] || '更高境界')} · ${sp.points ?? 0} 贡献</small>
        <div class="sect-library-footer"><span>${esc(reason || '可请领')}</span>
          <button data-action="library-spell" data-id="${esc(sp.id)}" ${!sp.enabled || sp.known ? 'disabled' : ''}>${sp.known ? '已学会' : '请领神通'}</button>
        </div>
      </article>`;
    }).join('')}</div>`;
}

// ------------------------------------------------------------
// 成就
// ------------------------------------------------------------
export function renderAchievements(data) {
  const list = data?.achievements || [];
  if (!list.length) return '<div class="empty">成就信息未加载。</div>';
  const done = list.filter(a => a.done).length;

  return `<div class="ach-head">已达成 <b>${done}</b> / ${list.length}</div>`
    + list.map(a => {
        const pct = a.target > 0
          ? Math.min(100, Math.round(a.progress / a.target * 100)) : 0;
        return `<div class="ach-row ${a.done ? 'done' : ''}">
          <div class="ach-top"><strong>${esc(a.name)}</strong>
            <span>${a.progress} / ${a.target}</span></div>
          <div class="ach-bar"><i style="width:${pct}%"></i></div>
        </div>`;
      }).join('');
}
