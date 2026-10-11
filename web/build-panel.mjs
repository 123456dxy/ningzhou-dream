// 宁州一梦 · C++ 版 — 建号面板
//
// 为什么单独写一个文件：建号是个"非游戏内"的状态机，
// 把 34 项天赋和 5 项属性全列出来、实时算点数，逻辑不少，
// 塞进 app.mjs 会让那个文件很难读。
//
// 设计：
//   - 天赋全部列出，按类别分组，不再随机抽取
//   - 磨砺类天赋成本为负，选了会退还点数
//   - 五项属性各自 +/- ，剩余点数实时显示
//   - 点「开始这一世」才真正建号，之前只是本地状态

'use strict';

// 随机道号：姓 + 单/双字名，风格偏清冷仙气，和 NPC 名字同一套用字。
const SURNAMES = ['沈', '苏', '陆', '秦', '徐', '顾', '谢', '江', '萧', '林',
  '白', '温', '柳', '裴', '宋', '叶', '程', '许', '云', '商'];
const GIVEN = ['知微', '清川', '疏影', '砚秋', '鹤鸣', '书白', '既明', '南枝',
  '听雪', '望舒', '照野', '寒山', '停云', '饮冰', '挽星', '折玉',
  '素心', '怀瑾', '临风', '澄之', '观澜', '问樵', '含章', '可言',
  '星阑', '暮成', '青崖', '沉璧', '抚琴', '抱一'];

function randomName() {
  const s = SURNAMES[Math.floor(Math.random() * SURNAMES.length)];
  const g = GIVEN[Math.floor(Math.random() * GIVEN.length)];
  return s + g;
}

export function createBuildPanel({ onStart, onCancel }) {
  let opts = null;          // /api/build-options 的结果
  let name = randomName();
  let namesUsed = [];
  let gender = '女';
  let traits = [];          // 已选天赋 id
  let alloc = {};           // 属性 id -> 点数
  let budget = 10;
  let check = { remain: 10, spent: 0, allocSpent: 0, valid: true, error: '' };
  let root = null;
  let timer = null;
  let events = null;
  let starting = false;
  // 地图种子：决定这一世的初始资质/悟性与整个宁州。
  // 0 表示「让服务端现摇一个」。
  let seed = 0;

  // ---- 取选项 ----
  async function load() {
    const r = await fetch('/api/build-options', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ seed }),
    }).then(x => x.json()).catch(() => null);
    if (r) {
      opts = r;
      budget = r.budget;
      // 服务端现摇的种子要记下来，建号时必须用同一个
      if (r.innate && r.innate.seed) seed = r.innate.seed;
    } else if (root) {
      root.innerHTML = `<div class="build-overlay"><div class="build-panel"><h2>无法读取建号数据</h2><p>请确认 C++ 服务还在运行，然后重试。</p><div class="build-actions"><button id="bp-retry">重试</button>${onCancel ? '<button id="bp-cancel">返回</button>' : ''}</div></div></div>`;
    }
  }

  // ---- 校验（防抖，避免每个按键都打服务端）----
  function scheduleCheck() {
    clearTimeout(timer);
    timer = setTimeout(async () => {
      const r = await fetch('/api/build-check', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ traits, allocation: alloc, budget }),
      }).then(x => x.json()).catch(() => null);
      if (r) { check = r; paintPoints(); paintActions(); }
    }, 90);
  }

  // 本地即时估算，让界面不卡
  function estimate() {
    let spent = 0;
    for (const id of traits) {
      const t = (opts?.talents || []).find(x => x.id === id);
      if (t) spent += t.cost;
    }
    let allocSpent = 0;
    for (const k in alloc) allocSpent += alloc[k];
    return { spent, allocSpent, remain: budget - spent - allocSpent };
  }

  function paintPoints() {
    const host = root?.querySelector('#bp-points');
    if (!host) return;
    const e = estimate();
    // 一律用本地即时估算显示，不用服务端返回的 check.remain ——
    // check 是防抖后的异步结果，会慢一拍，
    // 玩家花完点数还看到「尚余 10」，会以为点数没花掉。
    const remain = e.remain;
    const cls = remain < 0 ? 'over' : (remain === 0 ? 'done' : '');
    host.innerHTML =
      `<div><label>可分配</label><strong>${budget}</strong></div>`
    + `<div><label>天赋消耗</label><strong>${e.spent}</strong></div>`
    + `<div><label>属性消耗</label><strong>${e.allocSpent}</strong></div>`
    + `<div><label>尚余</label><strong class="${cls}">${remain}</strong></div>`;
  }

  function paintActions() {
    const host = root?.querySelector('#bp-actions');
    if (!host) return;
    const e = estimate();
    const over = e.remain < 0;
    const why = over ? `点数超支 ${-e.remain} 点，请减少天赋或属性。`
                     : (check.error || '');
    host.innerHTML =
      `<span class="why">${why}</span>`
    + `<button id="bp-reset">清空重来</button>`
    + (onCancel ? `<button id="bp-cancel">返回</button>` : '')
    + `<button class="primary" id="bp-start" ${over || !name.trim() || starting ? 'disabled' : ''}>`
    + `${starting ? '正在开辟……' : '开始这一世'}</button>`;
  }

  function esc(s) {
    return String(s ?? '').replace(/[&<>"']/g,
      c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
  }

  // ---- 渲染 ----
  function render() {
    if (!opts) return;
    const e = estimate();

    // 天赋按类别分组
    const groups = {};
    for (const t of opts.talents) {
      (groups[t.category] ||= []).push(t);
    }
    // 类别顺序：先正成本（按最高单项降序），磨砺放最后
    const keys = Object.keys(groups).sort((a, b) => {
      if (a === '磨砺') return 1;
      if (b === '磨砺') return -1;
      const ca = Math.max(...groups[a].map(t => t.cost));
      const cb = Math.max(...groups[b].map(t => t.cost));
      if (ca !== cb) return cb - ca;
      return a.localeCompare(b, 'zh');
    });

    const talentHTML = keys.map(cat => {
      const cards = groups[cat].map(t => {
        const picked = traits.includes(t.id);
        // 天赋数量不设上限，所以不再因为「已选够 3 项」把其余变灰。
        // 唯一的门槛是点数：买不起的才变灰。
        const e = estimate();
        const off = !picked && t.cost > e.remain;
        const costCls = t.cost < 0 ? 'free' : (t.cost >= 6 ? 'bad' : '');
        const costText = t.cost < 0 ? `${t.cost}（退还）` : `${t.cost} 点`;
        return `<button class="talent-card ${picked ? 'picked' : ''} ${off ? 'off' : ''}" `
          + `data-talent="${t.id}" title="${esc(t.effect)}">`
          + `<div class="tc-head"><span class="tc-name">${esc(t.name)}</span>`
          + `<span class="tc-cost ${costCls}">${costText}</span></div>`
          + `<div class="tc-effect">${esc(t.effect)}</div></button>`;
      }).join('');
      return `<div class="talent-group"><h4>${esc(cat)} `
           + `<span>（${groups[cat].length} 项）</span></h4>`
           + `<div class="talent-grid">${cards}</div></div>`;
    }).join('');

    const allocHTML = opts.stats.map(s => {
      const v = alloc[s.id] || 0;
      const canAdd = v < s.max && e.remain > 0;
      return `<div class="alloc-row">
        <div class="an">${esc(s.name)}</div>
        <div class="ad">${esc(s.desc)}${s.id === 'body'
          ? `<br><span class="soft-cap">软上限：前 8 点每点 +6%，之后每点只 +2%，封顶 +72%</span>`
          : ''}</div>
        <div class="alloc-ctl">
          <button data-alloc="${s.id}" data-d="1" ${v > 0 ? '' : 'disabled'}>−</button>
          <span class="av">${v}</span>
          <button data-alloc="${s.id}" data-d="-1" ${canAdd ? '' : 'disabled'}>+</button>
          <span class="cap">/ ${s.max}</span>
        </div></div>`;
    }).join('');

    // 预览：让玩家看到点数花在哪
    const pickedNames = traits.map(id => {
      const t = opts.talents.find(x => x.id === id);
      return t ? t.name : id;
    });
    const preview =
      `天赋：${pickedNames.length ? esc(pickedNames.join(' · ')) : '（未选）'}`
    + `　｜　属性：${Object.entries(alloc).filter(([, v]) => v > 0)
        .map(([k, v]) => {
          const s = opts.stats.find(x => x.id === k);
          return `${s ? s.name : k} +${v}`;
        }).join(' · ') || '（未分配）'}`;

    root.innerHTML = `
      <div class="build-overlay">
        <div class="build-panel">
          <div class="build-top">
            <div>
              <h2>开辟这一世</h2>
              <p>天赋全部列出，按需挑选。只受点数限制，选多少项都行；磨砺类会退还点数。</p>
            </div>
            <div class="build-innate" id="bp-innate"></div>
      <div class="build-points" id="bp-points"></div>
          </div>

          <div class="build-section">
            <h3>道号与出身</h3>
            <div class="build-fields">
              <input type="text" id="bp-name" value="${esc(name)}"
                     placeholder="道号" maxlength="12">
              <button id="bp-name-reroll" title="随机一个道号">🎲 换一个</button>
              <div class="seg" id="bp-gender">
                <button data-gender="女" class="${gender === '女' ? 'active' : ''}">女</button>
                <button data-gender="男" class="${gender === '男' ? 'active' : ''}">男</button>
              </div>
            </div>
          </div>

          <div class="build-section">
            <h3>先天气运 <span>已选 ${traits.length} 项 · 只受点数限制，想选几项都行</span></h3>
            ${talentHTML}
          </div>

          <div class="build-section">
            <h3>属性分配 <span>剩余点数可以留到下一世，也可以在这里花掉</span></h3>
            <div class="alloc-list">${allocHTML}</div>
          </div>

          <div class="build-preview">${preview}</div>
          <div class="build-actions" id="bp-actions"></div>
        </div>
      </div>`;

    paintPoints();
    paintActions();
    paintInnate();
  }

  // 先天气运：这一世生来的资质与悟性由地图种子决定。
  // 展示出来玩家才知道自己底子如何、要不要重掷。
  function paintInnate() {
    const host = root?.querySelector('#bp-innate');
    if (!host) return;
    const inn = opts?.innate || {};
    if (!inn.apt) {
      host.innerHTML = '';
      return;
    }
    const grade = (v, lo, hi) => {
      if (v >= hi) return '上乘';
      if (v >= lo) return '中平';
      return '偏薄';
    };
    // 评价档位按初始掷出范围三等分（资质 60–120、悟性 15–60）
    const aptLo = inn.aptRollMin ?? 60, aptHi = inn.aptRollMax ?? 120;
    const insLo = inn.insightRollMin ?? 15, insHi = inn.insightRollMax ?? 60;
    const t1 = (lo, hi) => lo + (hi - lo) / 3;
    const t2 = (lo, hi) => lo + ((hi - lo) * 2) / 3;
    host.innerHTML =
      `<div class="innate-item"><label>灵根资质</label>`
    + `<strong>${inn.apt}</strong>`
    + `<span>${grade(inn.apt, t1(aptLo, aptHi), t2(aptLo, aptHi))}</span>`
    + `<em>初始 ${aptLo}–${aptHi} · 上限 ${inn.aptMax ?? 200}</em></div>`
    + `<div class="innate-item"><label>悟性</label>`
    + `<strong>${inn.insight}</strong>`
    + `<span>${grade(inn.insight, t1(insLo, insHi), t2(insLo, insHi))}</span>`
    + `<em>初始 ${insLo}–${insHi} · 上限 ${inn.insightMax ?? 200}</em></div>`
    + `<div class="innate-seed"><label>地图种子</label>`
    + `<strong>${inn.seed ?? '—'}</strong>`
    + `<button id="bp-reroll">换个种子</button></div>`;
  }

  // ---- 事件 ----
  function bind() {
    events = new globalThis.AbortController();
    root.addEventListener('click', async ev => {
      if (starting) return;
      if (ev.target.closest('#bp-retry')) { await load(); render(); return; }
      const t = ev.target.closest('[data-talent]');
      if (t) {
        const id = t.dataset.talent;
        const i = traits.indexOf(id);
        if (i >= 0) {
          traits.splice(i, 1);          // 再点一次取消
        } else {
          // 天赋数量不设上限 —— 唯一门槛是点数，超了下面会提示
          traits.push(id);
        }
        render();
        scheduleCheck();
        return;
      }

      const g = ev.target.closest('[data-gender]');
      if (g) { gender = g.dataset.gender; render(); return; }

      const a = ev.target.closest('[data-alloc]');
      if (a) {
        const id = a.dataset.alloc;
        const d = Number(a.dataset.d);
        const cur = alloc[id] || 0;
        // data-d=1 是减号，-1 是加号（按钮上 − 在左）
        const next = d === 1 ? cur - 1 : cur + 1;
        const def = opts.stats.find(x => x.id === id);
        if (next < 0 || next > (def?.max ?? 99)) return;
        if (d === -1) {
          const e = estimate();
          if (e.remain <= 0) return;   // 没点数了
        }
        alloc[id] = next;
        if (next === 0) delete alloc[id];
        render();
        scheduleCheck();
        return;
      }

      if (ev.target.closest('#bp-reset')) {
        traits = []; alloc = {};
        render(); scheduleCheck();
        return;
      }
      if (ev.target.closest('#bp-name-reroll')) {
        // 随机道号：骰子一按就换一个
        name = randomName();
        namesUsed.push(name);
        render();
        return;
      }
      if (ev.target.closest('#bp-reroll')) {
      // 换一个种子：先天气运与地图都会跟着换
      seed = Math.floor(Math.random() * 2000000000) + 1;
      await load();
      render();
      return;
    }
    if (ev.target.closest('#bp-cancel') && onCancel) { onCancel(); return; }

      if (ev.target.closest('#bp-start')) {
        const e = estimate();
        if (e.remain < 0) return;
        starting = true; paintActions();
        clearTimeout(timer);
        // 最后再校验一次，确保服务端也认可
        const r = await fetch('/api/build-check', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ traits, allocation: alloc, budget }),
        }).then(x => x.json()).catch(() => null);
        if (!r || !r.valid) {
          check = r || { error: '无法校验，请确认服务还在运行后重试。' };
          starting = false; paintActions(); return;
        }
        try { await onStart({ name: name.trim(), gender, traits, allocation: alloc, budget, seed }); }
        finally { starting = false; paintActions(); }
        return;
      }
    }, { signal: events.signal });

    root.addEventListener('input', ev => {
      if (ev.target.id === 'bp-name') {
        name = ev.target.value;
        paintActions();
      }
    }, { signal: events.signal });
  }

  // ---- 对外 ----
  return {
    async mount(host) {
      root = host;
      await load();
      render();
      bind();
    },
    unmount() {
      events?.abort();
      clearTimeout(timer);
      if (root) root.innerHTML = '';
      root = null;
    },
  };
}
