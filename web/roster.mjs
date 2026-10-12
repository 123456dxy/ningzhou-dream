// 宁州一梦 · C++ 版 — 好友通讯录
//
// 与「宗门知交」的区别：
//   知交页只显示当前所在地的人（能当面往来）
//   通讯录显示所有认识的人，不限地点，可筛选排序，能远程传讯/邀请
//
// 数据由 /api/roster 提供，前端只负责筛选和排版。

'use strict';

let cache = null;        // 上次拉到的名单
let cacheKey = '';
let filter = { text: '', sort: 'relation', strangers: false, realm: 'all' };
let selected = null;

// 用途：转义插入HTML的文字。
// 流程：将空值转为空串，再替换五种HTML特殊字符。
// 调用方式：各渲染函数传入文本。
// 返回值：安全文本字符串。
const esc = s => String(s ?? '').replace(/[&<>"']/g,
  c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

// 用途：生成好感进度。
// 流程：限制可见进度宽度并生成进度条。
// 调用方式：人物或通讯录渲染传入好感值。
// 返回值：进度条HTML字符串。
function relBar(v) {
  const w = Math.max(0, Math.min(100, (v + 100) / 2));
  return `<div class="rel-bar ${v < 0 ? 'neg' : ''}"><i style="width:${w}%"></i></div>`;
}

// 用途：整理通讯录关系标签。
// 流程：读取人物已知、关系、师徒和身份状态，收集标签。
// 调用方式：通讯录渲染传入条目。
// 返回值：标签数组。
function tagsOf(e) {
  const t = [];
  if (e.isMaster) t.push('<span class="tag master">师尊</span>');
  if (e.isPartner) t.push('<span class="tag partner">道侣</span>');
  if (e.isDisciple) t.push('<span class="tag disciple">弟子</span>');
  if (e.here) t.push('<span class="tag">同在此地</span>');
  return t.join('');
}

// 拉名单（按需刷新）
// 用途：读取好友通讯录。
// 流程：按参数与缓存键复用数据，必要时POST roster。
// 调用方式：通讯录页面可传force。
// 返回值：Promise，通讯录对象或失败时null。
export async function loadRoster(force) {
  const key = `${filter.sort}|${filter.strangers}`;
  if (!force && cache && cacheKey === key) return cache;
  const r = await fetch('/api/roster', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ strangers: filter.strangers, sort: filter.sort }),
  }).then(x => x.json()).catch(() => null);
  cache = r?.roster || [];
  cacheKey = key;
  return cache;
}

// 用途：清除通讯录缓存。
// 流程：重置cache与cacheKey。
// 调用方式：邀约、支线或游戏状态变化后调用。
// 返回值：无返回值。
export function invalidateRoster() { cache = null; cacheKey = ''; }

// 本地筛选
// 用途：筛选并排序通讯录条目。
// 流程：按本地搜索、境界和关系条件筛选后按当前顺序排列。
// 调用方式：通讯录渲染无参调用。
// 返回值：可见人物数组。
function visible() {
  let list = cache || [];
  const q = filter.text.trim();
  if (q) {
    list = list.filter(e =>
      e.name.includes(q) || e.sectName.includes(q) ||
      e.personality.includes(q) || e.locationName.includes(q));
  }
  if (filter.realm !== 'all') {
    const ranges = { low: [0, 2], mid: [3, 8], high: [9, 14] };
    const [lo, hi] = ranges[filter.realm] || [0, 14];
    list = list.filter(e => e.stage >= lo && e.stage <= hi);
  }
  return list;
}

// 用途：生成通讯录界面。
// 流程：根据缓存、筛选与state组织搜索、分类、人物和动作。
// 调用方式：通讯录页面传入state。
// 返回值：通讯录HTML字符串。
export function renderRoster(state) {
  const list = visible();
  const stat = state.rosterStat || { known: 0, here: 0 };

  const rows = list.map(e => `
    <div class="roster-row ${selected === e.id ? 'selected' : ''}" data-rsel="${e.id}">
      <div class="rn">${esc(e.name)}${tagsOf(e)}</div>
      <div class="rmeta">
        ${esc(e.gender)} · ${e.age} 岁 · ${esc(e.realmName)} ·
        ${esc(e.sectName)} · ${esc(e.personality)}
        ${e.alive === false
          ? `<span class="dead-tag">已离世${e.deathYear ? ` · 第 ${e.deathYear} 年` : ''}</span>`
          : ''}
      </div>

      <div class="rrel">${relBar(e.relation)}
        <span class="rel-num">${e.relation}</span>
        <span class="rel-label">${esc(e.relationLabel)}</span>
      </div>
      <div class="racts">
        ${e.canMessage
          ? `<button data-raction="message" data-npc="${e.id}">传讯</button>` : ''}
        ${e.canInvite
          ? `<button data-raction="invite" data-npc="${e.id}">邀请前来</button>` : ''}
        ${e.here
          ? `<button data-raction="meet" data-npc="${e.id}">当面往来</button>` : ''}
      </div>
    </div>`).join('');

  const realms = [['all', '全部境界'], ['low', '炼气'],
                  ['mid', '筑基·金丹'], ['high', '元婴以上']];

  return `
    <div class="chronicle-panel">
      <h3>好友通讯录</h3>
      <p class="panel-note">
        共认识 <b>${stat.known}</b> 人，其中 <b>${stat.here}</b> 人与你同在此地。
        传讯可以远程维持交情；交情够高可以邀对方前来。
      </p>

      <div class="roster-bar">
        <input type="text" id="roster-search" placeholder="搜道号 / 宗门 / 性情 / 地点"
               value="${esc(filter.text)}">
        <div class="seg">
          ${[['relation', '按交情'], ['realm', '按境界'],
             ['name', '按道号'], ['sect', '按宗门']]
            .map(([k, n]) =>
              `<button data-rsort="${k}" class="${filter.sort === k ? 'active' : ''}">${n}</button>`)
            .join('')}
        </div>
        <div class="seg">
          ${realms.map(([k, n]) =>
            `<button data-rrealm="${k}" class="${filter.realm === k ? 'active' : ''}">${n}</button>`)
            .join('')}
        </div>
        <label style="font-size:13px;color:#4a5f56">
          <input type="checkbox" id="roster-strangers"
                 ${filter.strangers ? 'checked' : ''}> 含故人旧卷（已离世的熟人）
        </label>
      </div>

      ${list.length
        ? `<div class="roster-list">${rows}</div>`
        : `<div class="roster-empty">
             ${filter.strangers
               ? '没有符合筛选的人。'
               : '还没有认识的人。去「宁州舆图 → 当地仙友」见礼，或与同门往来。'}
           </div>`}
      <p class="panel-note" style="margin-top:10px">
        共 ${list.length} 条${filter.text ? `（关键词「${esc(filter.text)}」）` : ''}
      </p>
    </div>`;
}

// 事件绑定（由 app.mjs 调用一次）
const boundRoots = new WeakMap();
// 用途：绑定通讯录局部事件。
// 流程：委托分类、排序、动作与搜索输入，变更后调用外部重绘。
// 调用方式：页面插入后传入宿主和回调对象。
// 返回值：无返回值。
export function bindRoster(root, { onAction, onRerender }) {
  const handlers = { onAction, onRerender };
  if (boundRoots.has(root)) { boundRoots.set(root, handlers); return; }
  boundRoots.set(root, handlers);
  // 用途：委托通讯录排序、境界筛选、人物选择与往来动作。
  // 流程：读取最新回调，处理匹配的 data 属性并按需重新加载或重绘。
  // 调用方式：bindRoster 首次在宿主绑定，宿主 click 自动触发。
  // 返回值：无返回值；外部动作通过 onAction 执行。
  root.addEventListener('click', ev => {
    const { onAction, onRerender } = boundRoots.get(root);
    const sort = ev.target.closest('[data-rsort]');
    if (sort) {
      filter.sort = sort.dataset.rsort;
      invalidateRoster();
      loadRoster(true).then(onRerender);
      return;
    }
    const realm = ev.target.closest('[data-rrealm]');
    if (realm) { filter.realm = realm.dataset.rrealm; onRerender(); return; }

    const act = ev.target.closest('[data-raction]');
    if (act) {
      ev.stopPropagation();
      onAction(act.dataset.raction, act.dataset.npc);
      return;
    }
    const sel = ev.target.closest('[data-rsel]');
    if (sel) { selected = sel.dataset.rsel; onRerender(); }
  });

  // 用途：即时筛选通讯录姓名。
  // 流程：更新关键词并调用重绘，随后恢复搜索框焦点和光标。
  // 调用方式：通讯录搜索框的 input 事件触发。
  // 返回值：无返回值。
  root.addEventListener('input', ev => {
    const { onRerender } = boundRoots.get(root);
    if (ev.target.id === 'roster-search') {
      filter.text = ev.target.value;
      onRerender();
      // 保持焦点在输入框
      setTimeout(() => {
        const el = document.getElementById('roster-search');
        if (el) { el.focus(); el.setSelectionRange(el.value.length, el.value.length); }
      }, 0);
    }
    if (ev.target.id === 'roster-strangers') {
      filter.strangers = ev.target.checked;
      invalidateRoster();
      loadRoster(true).then(onRerender);
    }
  });
}
