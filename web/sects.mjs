// 宁州一梦 · C++ 版 — 五宗一览与成就面板
//
// 对应 JS app.mjs 的 sects 页（五宗卡 + 拜入山门）与 arts-ui 的成就列表。
// C++ 之前只能靠第 3 年的「山门来信」进金虹剑派，
// 26 部拓展功法又全靠宗门藏经阁，等于大半养成线被卡死。

'use strict';

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

let sectCache = null;
let achCache = null;

// 用途：读取宗门目录。
// 流程：优先复用缓存，必要时POST sects并保存响应。
// 调用方式：宗门与任务页面可传force。
// 返回值：Promise，宗门目录对象。
export async function loadSects(force) {
  if (sectCache && !force) return sectCache;
  sectCache = await fetch('/api/sects', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(r => r.json()).catch(() => null);
  return sectCache;
}

// 用途：读取本世成就进度。
// 流程：优先复用缓存，必要时POST achievements并保存响应。
// 调用方式：成就页面可传force。
// 返回值：Promise，成就对象。
export async function loadAchievements(force) {
  if (achCache && !force) return achCache;
  achCache = await fetch('/api/achievements', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: '{}',
  }).then(r => r.json()).catch(() => null);
  return achCache;
}

// 用途：清除宗门和成就缓存。
// 流程：同时重置sectCache与achCache。
// 调用方式：角色状态变化时调用。
// 返回值：无返回值。
export function invalidateSects() { sectCache = null; achCache = null; }

// ------------------------------------------------------------
// 五宗一览
// ------------------------------------------------------------
// 用途：展示可拜入的宗门。
// 流程：逐宗读取门规、条件和当前身份，生成入宗按钮。
// 调用方式：宗门页传入目录和player。
// 返回值：宗门卡片HTML字符串。
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
        ? `<div class="sect-foot">已是本门弟子 · ${player?.location === s.id ? '当前在山门' : '请回山门办理差事、受业和传承'}</div>
           ${player?.location === s.id ? `<div class="sect-entry-actions"><button data-action="sect-open" data-id="${esc(s.id)}" data-sub="mentors">拜师与受业</button><button data-action="sect-open" data-id="${esc(s.id)}" data-sub="jobs">宗门差事</button><button data-action="sect-open" data-id="${esc(s.id)}" data-sub="library">藏经阁</button></div>` : `<button data-action="sect-locate" data-id="${esc(s.id)}">前往本门山门 →</button>`}`
        : `<button class="sect-join" data-action="join-sect" data-sect="${esc(s.id)}"
                   ${s.enabled ? '' : 'disabled'}>
             ${s.enabled ? `报名入门考核（${s.trial?.cost ?? 10} 灵石）` : esc(s.reason || '暂不可入')}
           </button>
           ${player?.location !== s.id ? `<button class="subtle" data-action="sect-locate" data-id="${esc(s.id)}">在舆图中查看山门 →</button>` : ''}`}
    </div>`).join('') + `</div>`;
}

// 用途：展示师尊和授业条件。
// 流程：按当前宗门及权威候选列表组织拜师与授业入口。
// 调用方式：宗门页传入目录与state。
// 返回值：师尊面板HTML字符串。
export function renderSectMentors(data, state) {
  const player = state.player || {};
  if (!player.sect) return '<div class="empty">先到山门完成入门考核，入宗后可向同门师长行拜师礼。</div>';
  const mentors = data?.mentors || [];
  const need = data?.mentorAffinityNeed ?? 40;
  const renown = data?.masterRequirement ?? 10;
  const lesson = (state.bondActions || []).find(action => action.id === 'lesson');
  return `<div class="sect-mentors" data-sect-mentors><p class="panel-note">拜师需本门师长境界高于你、好感至少${esc(need)}、声望至少${esc(renown)}，行礼用时5天、花费20灵石。师长在外游历时须先赶往当面相见；受业须回本宗山门，每月一次。</p>`
    + `<div class="sect-mentor-grid">${mentors.map(mentor => {
      const here = mentor.location === player.location;
      const status = mentor.isMaster ? '现任师尊' : mentor.enabled ? '可拜师' : '尚未达条件';
      const reason = mentor.isMaster ? lesson?.reason || '可在师门日常接受授业。' : mentor.reason || '';
      return `<article class="sect-mentor-card${mentor.isMaster ? ' current' : ''}" data-sect-mentor="${esc(mentor.id)}">`
        + `<div class="sect-mentor-head">${mentor.portrait ? `<img class="sect-mentor-portrait" src="${esc(mentor.portrait)}" alt="${esc(mentor.name)}立绘" loading="lazy">` : ''}`
        + `<div><strong>${esc(mentor.name)}</strong><span class="tag">${esc(status)}</span><p>${esc(mentor.realmName)} · 好感 ${esc(mentor.relation ?? 0)} / ${esc(need)}</p><small>现居 ${esc(mentor.locationName || '未知地点')}${here ? ' · 可以当面往来' : ' · 须先赶路相见'}</small></div></div>`
        + `<p class="sect-mentor-reason">${esc(reason || '条件已满足，可以行拜师礼。')}</p><div class="sect-entry-actions">`
        + (mentor.isMaster ? `<button data-action="bond" data-id="lesson" ${lesson?.enabled ? '' : 'disabled'}>接受师尊授业</button>`
          : `<button class="primary" data-action="social" data-npc="${esc(mentor.id)}" data-id="master" ${mentor.enabled ? '' : 'disabled'}>行拜师礼</button>`)
        + (here ? `<button data-action="sect-mentor-detail" data-npc="${esc(mentor.id)}">当面往来 · 增进好感</button>`
          : `<button data-action="sect-locate" data-id="${esc(mentor.location)}">在舆图查看所在 →</button>`)
        + `</div></article>`;
    }).join('') || '<div class="empty">当前没有在世且境界更高的同门可拜为师。可先与同门往来，等待同门突破或新的师长入宗。</div>'}</div></div>`;
}

// 用途：展示本门任务与考核。
// 流程：筛选山门行动并显示真实费用报酬及可做原因。
// 调用方式：任务宗门分类传入state、options与data。
// 返回值：宗门任务HTML字符串。
export function renderSectTasks(state, options, data) {
  const player = state.player || {};
  if (!player.sect) return '<div class="empty">散修不能承接宗门差事，请先通过山门考核。</div>';
  const tasks = (options || []).filter(option => option.type === 'task' && (option.sect || option.id === 'sect' || option.id?.startsWith('sect-')));
  const used = data?.sectTaskUsed ?? tasks[0]?.monthlyUsed ?? 0;
  const limit = data?.sectTaskLimit ?? tasks[0]?.monthlyLimit ?? 3;
  const atSect = player.location === player.sect;
  return `<div class="sect-task-board" data-sect-task-board><p class="panel-note">本月已接 ${esc(used)} / ${esc(limit)} 件宗门差事，所有差事共用额度。接单即占额度，中止不返还；完成后领取下列报酬。${!atSect ? '请先赶回所属山门接差事。' : ''}</p>`
    + (!atSect ? `<button data-action="sect-locate" data-id="${esc(player.sect)}">在舆图查看本门 →</button>` : '')
    + tasks.map(task => `<article class="sect-task-card" data-sect-task="${esc(task.id)}"><div><h3>${esc(task.name)}</h3><p>${esc(task.desc || '')}</p>`
      + `<div class="sect-task-rewards"><span>${esc(task.days || 0)} 天</span><span>灵石 +${esc(task.pay || 0)}</span><span>贡献 +${esc(task.contribution ?? (task.id === 'sect' ? 15 : 0))}</span>`
      + `${task.daoExp ? `<span>${task.dao ? `${esc(task.dao)}道` : '本宗大道'}经验 +${esc(task.daoExp)}</span>` : ''}`
      + `${task.daoPoints > 0 ? `<span>悟道点 +${esc(task.daoPoints)}</span>` : ''}</div>`
      + `${!task.enabled && task.reason ? `<p class="sect-task-reason">${esc(task.reason)}</p>` : ''}</div>`
      + `<button data-action="eco-run" data-type="task" data-id="${esc(task.id)}" ${task.enabled ? '' : 'disabled'}>${task.enabled ? '接下差事' : '暂不可接'}</button></article>`).join('')
    + (!tasks.length ? '<div class="empty">正在读取本门差事，请稍候。</div>' : '') + `</div>`;
}

// 用途：展示藏经阁神通来源。
// 流程：筛选本门条目，说明已学状态、兑换条件与费用。
// 调用方式：宗门页传入state。
// 返回值：藏经阁神通HTML字符串。
export function renderSectSpellLibrary(state) {
  const list = (state.librarySpells || []).filter(sp => !sp.sect || sp.sect === state.player.sect);
  if (!list.length) return '<div class="empty">本门暂未提供可请领的神通。</div>';
  const realms = ['炼气初期', '炼气中期', '炼气后期', '筑基初期', '筑基中期', '筑基后期', '金丹初期', '金丹中期', '金丹后期', '元婴初期', '元婴中期', '元婴后期', '化神初期', '化神中期', '化神后期'];
  return `<p class="panel-note">用贡献请领本门神通。学会后在「功法与悟道 → 神通搭配」选择携带，可与主修功法和悟道搭配。</p>
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
// 用途：展示成就当前与目标。
// 流程：读取已达成状态，进度条限制0至100%，完成条目显示已达成。
// 调用方式：成就页传入data。
// 返回值：成就HTML；未加载时空状态HTML。
export function renderAchievements(data) {
  const list = data?.achievements || [];
  if (!list.length) return '<div class="empty">成就信息未加载。</div>';
  const done = list.filter(a => a.done).length;

  return `<div class="ach-head">已达成 <b>${done}</b> / ${list.length}</div>`
    + list.map(a => {
        const pct = a.target > 0
          ? Math.max(0, Math.min(100, Math.round(a.progress / a.target * 100))) : 0;
        return `<div class="ach-row ${a.done ? 'done' : ''}">
          <div class="ach-top"><strong>${esc(a.name)}</strong>
            <span>${a.done ? '已达成' : `${a.progress} / ${a.target}`}</span></div>
          <div class="ach-bar"><i style="width:${pct}%"></i></div>
        </div>`;
      }).join('');
}
