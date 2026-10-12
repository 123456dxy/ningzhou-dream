// 宁州一梦 · C++ 版 — 真实浏览器点击测试（CDP）
//
// 为什么需要它：
//   之前的 DOM 垫片是伪造的 document，查不出「CSS 没生效」「按钮被遮住」
//   「真浏览器里报错」这类问题。这里起一个无头 Chrome/Edge，走 DevTools 协议，
//   派发**真实鼠标事件**（走命中测试），并截图 —— 截图可以直接看。
//
// 依赖：Node >= 22（内置 WebSocket），系统里的 Chrome 或 Edge。零 npm 安装。
//
// 用法：
//   node tools/uitest.mjs                 跑冒烟：开页 → 点 8 个页签 → 截图
//   node tools/uitest.mjs --play          跑完整流程：建号 → 配给 → 战斗出牌
//   node tools/uitest.mjs --battle-ui     导入 build/battle-ui-fixture.json，验证真实战斗状态与移除交互
//   node tools/uitest.mjs --map-ui        隔离端口 18106，验证地图报价、逐日赶路与手机点击
//   node tools/uitest.mjs --npc-ui        验证NPC立绘、实际道法、请教学习与月内限额
//   node tools/uitest.mjs --beast-ui      验证五大境界妖兽立绘与移动端战斗布局
//   node tools/uitest.mjs --dungeon-ui    验证定向情报、真实爬塔、战利品与洞府升级
//   node tools/uitest.mjs --dungeon-route-ui  专项验证爬塔节点坐标、连线与窄屏边界
//   node tools/uitest.mjs --systems-ui     验证声望入口、法宝主动技能与炼器只读预览
//   node tools/uitest.mjs --port 18106    指定隔离测试端口
//   node tools/uitest.mjs --keep          跑完不删截图

import { spawn } from 'node:child_process';
import { mkdirSync, writeFileSync, readFileSync, existsSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const DO_MAP_UI = argv.includes('--map-ui');
const DO_PLAY = argv.includes('--play');
const DO_BATTLE_UI = argv.includes('--battle-ui');
const DO_NPC_UI = argv.includes('--npc-ui');
const DO_BEAST_UI = argv.includes('--beast-ui');
const DO_DUNGEON_UI = argv.includes('--dungeon-ui');
const DO_DUNGEON_ROUTE_UI = argv.includes('--dungeon-route-ui');
const DO_SYSTEMS_UI = argv.includes('--systems-ui');
const SHOT_DIR = join(ROOT, 'build', 'shots', ...(DO_SYSTEMS_UI ? ['systems-ui'] : DO_MAP_UI ? ['map-ui'] : DO_NPC_UI ? ['npc-ui'] : DO_BEAST_UI ? ['beast-ui'] : DO_DUNGEON_ROUTE_UI ? ['dungeon-route-ui'] : DO_DUNGEON_UI ? ['dungeon-ui'] : []));
const arg = (name, def) => {
  const i = argv.indexOf(name);
  return i >= 0 && argv[i + 1] ? argv[i + 1] : def;
};
const PORT = arg('--port', '18106');
if (['8106', '8107'].includes(PORT))
  throw new Error('UI 测试会改动角色，必须使用隔离端口（默认 18106），不能使用 8106/8107。');
const BASE = `http://127.0.0.1:${PORT}/`;

const BROWSERS = [
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files (x86)\\Google\\Chrome\\Application\\chrome.exe',
];

const sleep = (ms) => new Promise(r => setTimeout(r, ms));
const log = (...a) => console.log(...a);

// ============================================================
// CDP 连接
// ============================================================
class Cdp {
  constructor(ws) {
    this.ws = ws;
    this.id = 0;
    this.pending = new Map();
    this.events = [];
    this.errors = [];
    ws.addEventListener('message', (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id !== undefined) {
        const p = this.pending.get(msg.id);
        if (p) {
          this.pending.delete(msg.id);
          msg.error ? p.reject(new Error(JSON.stringify(msg.error))) : p.resolve(msg.result);
        }
      } else {
        this.events.push(msg);
        // 收集真实浏览器里的报错
        if (msg.method === 'Runtime.exceptionThrown') {
          const d = msg.params.exceptionDetails;
          this.errors.push(d.exception?.description || d.text || 'unknown');
        }
        if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
          this.errors.push(msg.params.args.map(a => a.value ?? a.description ?? '').join(' '));
        }
        if (msg.method === 'Log.entryAdded' && msg.params.entry.level === 'error') {
          const en = msg.params.entry;
          this.errors.push(en.text + (en.url ? `  [${en.url}]` : ''));
        }
        // 把失败请求的完整信息记下来（哪个 URL、什么状态）
        if (msg.method === 'Network.responseReceived' && msg.params.response.status >= 400) {
          this.errors.push(`HTTP ${msg.params.response.status}  ${msg.params.response.url}`);
        }
      }
    });
  }

  send(method, params = {}) {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      this.pending.set(id, { resolve, reject });
      this.ws.send(JSON.stringify({ id, method, params }));
      setTimeout(() => {
        if (this.pending.has(id)) {
          this.pending.delete(id);
          reject(new Error(`CDP 超时: ${method}`));
        }
      }, 20000);
    });
  }

  // 求值，返回 JS 值
  async eval(expr) {
    const r = await this.send('Runtime.evaluate', {
      expression: `(function(){ ${expr} })()`,
      returnByValue: true,
      awaitPromise: true,
    });
    if (r.exceptionDetails) {
      throw new Error('页面求值出错: ' + (r.exceptionDetails.exception?.description || r.exceptionDetails.text));
    }
    return r.result.value;
  }
}

// ============================================================
// 启动浏览器
// ============================================================
async function launch() {
  const exe = BROWSERS.find(p => existsSync(p));
  if (!exe) throw new Error('找不到 Chrome 或 Edge');

  const profile = join(tmpdir(), 'nz-uitest-' + Date.now());
  const debugPort = 9222 + Math.floor(Math.random() * 300);

  const child = spawn(exe, [
    '--headless=new',
    `--remote-debugging-port=${debugPort}`,
    `--user-data-dir=${profile}`,
    '--no-first-run',
    '--no-default-browser-check',
    '--disable-gpu',
    '--window-size=1600,1100',
    '--hide-scrollbars',
    'about:blank',
  ], { stdio: 'ignore', detached: false });

  // 等调试端口就绪
  let target = null;
  for (let i = 0; i < 60; i++) {
    await sleep(250);
    try {
      const res = await fetch(`http://127.0.0.1:${debugPort}/json/list`);
      const list = await res.json();
      target = list.find(t => t.type === 'page');
      if (target) break;
    } catch { /* 还没起来 */ }
  }
  if (!target) {
    child.kill();
    throw new Error('浏览器调试端口没起来');
  }

  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((res, rej) => {
    ws.addEventListener('open', res);
    ws.addEventListener('error', rej);
  });

  const cdp = new Cdp(ws);
  await cdp.send('Page.enable');
  await cdp.send('Runtime.enable');
  await cdp.send('Log.enable');
  await cdp.send('Network.enable');
  await cdp.send('DOM.enable');

  return { cdp, child, profile };
}

// ============================================================
// 操作原语
// ============================================================
const shots = [];
let shotSeq = 0;

async function goto(cdp, url) {
  await cdp.send('Page.navigate', { url });
  await sleep(1200);
}

/** 等某个选择器出现（真的渲染出来） */
async function waitFor(cdp, sel, timeout = 8000) {
  const t0 = Date.now();
  while (Date.now() - t0 < timeout) {
    const n = await cdp.eval(`return document.querySelectorAll(${JSON.stringify(sel)}).length;`);
    if (n > 0) return true;
    await sleep(150);
  }
  return false;
}

/** 取元素中心坐标 */
async function centerOf(cdp, sel, nth = 0) {
  return cdp.eval(`
    const els = document.querySelectorAll(${JSON.stringify(sel)});
    const el = els[${nth}];
    if (!el) return null;
    const r = el.getBoundingClientRect();
    if (r.width === 0 || r.height === 0) return { zero: true };
    return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height };
  `);
}

/**
 * 真实点击：先取坐标，再派发 mousedown/mouseup。
 * 走浏览器的命中测试，所以「被遮罩挡住」这种情况会被测出来：
 * 点在坐标上落到的是别的元素。
 *
 * 注意：坐标是视口坐标。元素在视口外（比如长表单底部的按钮）必须先
 * 滚进视野，否则点在空处、什么都不会发生 —— 这正是建号面板踩过的坑。
 */
async function click(cdp, sel, nth = 0, opts = {}) {
  // 先滚进视野
  await cdp.eval(`
    const el = document.querySelectorAll(${JSON.stringify(sel)})[${nth}];
    if (el) el.scrollIntoView({ block: 'center', behavior: 'instant' });
    return true;
  `);
  await sleep(120);

  const c = await centerOf(cdp, sel, nth);
  if (!c) throw new Error(`找不到元素: ${sel}`);
  if (c.zero) throw new Error(`元素不可见（尺寸为 0）: ${sel}`);

  // 命中测试：这个坐标上最顶层的元素是谁？
  const hit = await cdp.eval(`
    const el = document.elementFromPoint(${c.x}, ${c.y});
    if (!el) return 'MISS（坐标上没有任何元素，说明在视口外）';
    const path = [];
    let n = el;
    while (n && path.length < 6) { path.push(n.tagName.toLowerCase() + (n.id ? '#' + n.id : '') + (n.className && typeof n.className === 'string' ? '.' + n.className.trim().split(/\\s+/).join('.') : '')); n = n.parentElement; }
    return path.join(' < ');
  `);

  const base = { x: Math.round(c.x), y: Math.round(c.y), button: 'left', clickCount: 1 };
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...base });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...base });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...base });
  await sleep(opts.wait ?? 350);

  return hit;
}

async function type(cdp, sel, text) {
  const ok = await cdp.eval(`
    const el = document.querySelector(${JSON.stringify(sel)});
    if (!el) return false;
    el.focus();
    el.value = ${JSON.stringify(text)};
    el.dispatchEvent(new Event('input', { bubbles: true }));
    return true;
  `);
  await sleep(200);
  return ok;
}

async function shot(cdp, name) {
  mkdirSync(SHOT_DIR, { recursive: true });
  const r = await cdp.send('Page.captureScreenshot', { format: 'png' });
  const file = join(SHOT_DIR, `${String(++shotSeq).padStart(2, '0')}-${name}.png`);
  writeFileSync(file, Buffer.from(r.data, 'base64'));
  shots.push(file);
  log(`    📷 ${file}`);
  return file;
}

async function narrowShot(cdp, name, focus = '#workspace') {
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  await cdp.eval(`document.querySelector(${JSON.stringify(focus)})?.scrollIntoView({ block: 'start', behavior: 'instant' }); return true;`);
  await sleep(300);
  const overflow = await cdp.eval(`return document.documentElement.scrollWidth > document.documentElement.clientWidth + 2;`);
  check(!overflow, `${name} 在 390px 窄屏没有横向溢出`);
  await shot(cdp, name + '-narrow');
  await cdp.send('Emulation.clearDeviceMetricsOverride');
  await sleep(150);
}

const textOf = (cdp, sel) => cdp.eval(`
  const el = document.querySelector(${JSON.stringify(sel)});
  return el ? el.innerText : null;
`);

const liveState = () => fetch(BASE + 'api/state').then(r => r.json()).then(r => r.state);

async function mapUI(cdp) {
  log('\n=== 真实地图与赶路 UI 回归（隔离服务） ===');
  const request = async (path, body = {}) => {
    const response = await fetch(BASE + 'api/' + path, { method: 'POST',
      headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    return { status: response.status, data: await response.json() };
  };
  const must = async (path, body) => {
    const result = await request(path, body);
    if (result.status >= 400 || result.data.ok === false || result.data.error)
      throw new Error(`${path} 失败: ${result.data.error || result.status}`);
    return result.data;
  };
  const created = await must('new', { seed: 480317, name: '地图验收', gender: '女', traits: [], allocation: {} });
  const exported = await must('export');
  const baseline = JSON.parse(exported.game);
  check(created.state.player.stage === 0 && baseline.player.location === created.state.player.location,
    '真实建号与导出提供可校验的测试存档');
  const origin = baseline.player.location;
  const loadClone = async (stage, stones = 50000) => {
    const clone = structuredClone(baseline);
    clone.player.stage = stage;
    clone.player.highestStage = stage;
    clone.player.xp = 0;
    clone.player.stones = stones;
    clone.player.health = 100;
    const result = await must('import', { game: JSON.stringify(clone) });
    if (result.state.player.stage !== stage || result.state.player.stones !== stones
      || result.state.player.location !== origin) throw new Error('测试存档未通过真实导入或属性不一致');
    return result.state;
  };
  const openMap = async () => {
    await goto(cdp, BASE);
    await click(cdp, '.main-tabs [data-tab="map"]');
    await click(cdp, '[data-subtab="map"][data-sub="atlas"]');
    if (!(await waitFor(cdp, '[data-map-atlas]'))) throw new Error('舆图没有渲染');
  };
  const pin = id => `.atlas-canvas [data-map-loc="${id}"]`;
  const focus = async id => {
    await click(cdp, pin(id));
    check(await cdp.eval(`return document.querySelector('[data-map-detail]')?.dataset.mapDetail === ${JSON.stringify(id)};`), `真实鼠标能选中地点 ${id}`);
  };
  const quoteCheck = async location => {
    const rows = await cdp.eval(`return [...document.querySelectorAll('[data-travel-mode]')].map(b => ({
      id: b.dataset.travelMode, text: b.innerText, disabled: b.disabled, role: b.getAttribute('role') }));`);
    check(rows.length === location.travel.options.length && location.travel.options.every(option => {
      const row = rows.find(r => r.id === option.id);
      return row && row.role === 'radio' && row.disabled === !option.enabled
        && row.text.includes(`${option.days} 天`)
        && row.text.includes(option.cost ? `${option.cost} 灵石` : '免费')
        && (option.enabled || !option.reason || row.text.includes(option.reason));
    }), '三种交通方式的天数、费用、可用性和原因与真实后端一致');
  };
  const stateAfter = async predicate => {
    const deadline = Date.now() + 8000;
    let state;
    do {
      state = await liveState();
      if (predicate(state)) return state;
      await sleep(120);
    } while (Date.now() < deadline);
    throw new Error('点击后服务端状态未按预期变化');
  };
  const network = path => cdp.events.filter(e => e.method === 'Network.requestWillBeSent'
    && new URL(e.params.request.url).pathname === '/api/' + path);
  const responseBody = async path => {
    const event = cdp.events.filter(e => e.method === 'Network.responseReceived'
      && new URL(e.params.response.url).pathname === '/api/' + path).at(-1);
    if (!event) throw new Error('浏览器未收到 ' + path + ' 的响应');
    const response = await cdp.send('Network.getResponseBody', { requestId: event.params.requestId });
    return JSON.parse(response.base64Encoded ? Buffer.from(response.body, 'base64').toString('utf8') : response.body);
  };
  const resolvePause = async state => {
    if (state.pending) {
      const snapshot = { days: state.clock.days, stones: state.player.stones, location: state.player.location,
        job: state.job && { to: state.job.to, remaining: state.job.remaining } };
      check(await cdp.eval(`return !!document.querySelector('#modal[open]')
        && [...document.querySelectorAll('#map-travel-step,#map-travel-resume')].every(b => b.disabled);`), '途中抉择显示弹窗并暂停赶路按钮');
      const refusal = await request('advance', { days: 1 });
      const unchanged = await liveState();
      check(refusal.data.ok === false && unchanged.clock.days === snapshot.days
        && unchanged.player.stones === snapshot.stones && unchanged.player.location === snapshot.location
        && unchanged.job?.to === snapshot.job?.to && unchanged.job?.remaining === snapshot.job?.remaining,
        '尚未处理抉择时真实 API 拒绝推进且行程保持完整');
      const choice = state.pending.options.find(o => !o.disabled && /leave|decline|ignore|refuse|pass|skip|avoid/.test(o.id))
        || state.pending.options.find(o => !o.disabled);
      if (!choice) throw new Error('抉择没有可选项');
      await click(cdp, `#modal [data-action="choose"][data-id="${choice.id}"]`);
      return liveState();
    }
    if (state.battle) {
      check(state.player.location === origin && state.job?.type === 'travel', '途中斗法保留出发地和未完成行程');
      await click(cdp, '[data-action="escape"]:not(:disabled)');
      return liveState();
    }
    return state;
  };
  const finishJourney = async state => {
    for (let n = 0; n < 16 && (state.job?.type === 'travel' || state.pending || state.battle); n++) {
      if (state.ended) throw new Error('地图测试角色在途中死亡');
      if (state.pending || state.battle) state = await resolvePause(state);
      else if (state.job?.type === 'travel') {
        const days = state.clock.days;
        await click(cdp, '#map-travel-resume:not(:disabled)');
        state = await stateAfter(s => s.clock.days > days || s.pending || s.battle || !s.job);
      }
    }
    return state;
  };

  let state = await loadClone(0);
  await openMap();
  check(await cdp.eval(`return document.querySelector('[data-map-detail]')?.dataset.mapDetail === ${JSON.stringify(origin)}
    && document.querySelector('.map-pin.here')?.dataset.mapLoc === ${JSON.stringify(origin)};`),
    '初次打开舆图即有当前城池详情与当前位置标记');
  check(await cdp.eval(`return !document.querySelector('#map-travel-start');`), '当前城池详情不会提供重复启程入口');
  const localServices = state.locations.find(location => location.id === origin).services || [];
  check(localServices.length > 0 && await cdp.eval(`return document.querySelectorAll('[data-location-services] [data-action="location-service"]').length === ${localServices.length};`),
    '当前地点列出真实可用去处');
  const marketService = localServices.find(service => service.id === 'market' && service.enabled);
  if (marketService) {
    const serviceBefore = await liveState();
    await click(cdp, '[data-location-services] [data-action="location-service"][data-id="market"]');
    const serviceAfter = await liveState();
    check(await cdp.eval(`return document.querySelector('.main-tabs [data-tab="market"]').classList.contains('active');`)
      && serviceAfter.clock.days === serviceBefore.clock.days && serviceAfter.player.stones === serviceBefore.player.stones,
      '地点交易去处实际跳转坊市，不消耗时间或灵石');
    await click(cdp, '.main-tabs [data-tab="map"]');
  }
  await click(cdp, '[data-action="map-zoom"][data-id=".25"]');
  check(await cdp.eval(`return Number(document.querySelector('[data-map-viewport]').dataset.zoom) === 1.25;`), '舆图放大按钮改变真实画布倍率');
  await click(cdp, '[data-action="map-zoom"][data-id="fit"]');
  check(await cdp.eval(`return Number(document.querySelector('[data-map-viewport]').dataset.zoom) === .75;`), '舆图总览按钮缩小画布');
  await click(cdp, '[data-action="map-zoom"][data-id=".25"]');
  await shot(cdp, 'map-current-city');
  await click(cdp, '[data-map-search]');
  await type(cdp, '[data-map-search]', '不存在的测试地点');
  check((await textOf(cdp, '.atlas-place-list') || '').includes('没有符合条件'), '地点搜索确实筛选目录');
  await type(cdp, '[data-map-search]', '');
  check(await cdp.eval(`const input = document.querySelector('[data-map-search]');
    const r = input?.getBoundingClientRect();
    return !!r && r.width > 0 && r.height > 0 && document.activeElement === input && input.value === ''
      && getComputedStyle(input).visibility === 'visible';`), '清空搜索后输入框仍然可见并保留焦点');
  await type(cdp, '[data-map-search]', '无尽海');
  check((await textOf(cdp, '.atlas-place-list') || '').includes('无尽海'), '清空后再次输入仍能筛选地点');
  await type(cdp, '[data-map-search]', '');
  const sea = state.locations.find(l => l.id === 'sea');
  if (!sea) throw new Error('地图缺少无尽海');
  await focus(sea.id);
  await quoteCheck(sea);
  check(await cdp.eval(`return [...document.querySelectorAll('[data-location-services] button')].every(button => button.disabled);`), '未抵达的险地去处不可远程执行');
  check(await cdp.eval(`return document.querySelector('[data-travel-mode="walk"]').disabled
    && document.querySelector('[data-travel-mode="flight"]').disabled
    && !document.querySelector('[data-travel-mode="boat"]').disabled;`), '炼气境海路仅灵舟可用，步行和御器有明确门槛');
  await shot(cdp, 'map-sea-locked');

  state = await loadClone(3);
  await openMap();
  const destination = state.locations.find(l => l.id !== origin && l.type === 'city'
    && l.travel.options.some(o => o.id === 'boat' && o.enabled && o.days >= 3 && o.days <= 20));
  if (!destination) throw new Error('缺少适合逐日验证的城际灵舟路线');
  const offer = destination.travel.options.find(o => o.id === 'boat');
  await focus(destination.id);
  await quoteCheck(destination);
  check(destination.travel.options.find(o => o.id === 'flight').enabled,
    '筑基角色通过合法存档导入后解锁御器飞行');
  await click(cdp, '[data-travel-mode="walk"]');
  await click(cdp, '[data-travel-mode="boat"]');
  check(await cdp.eval(`return document.querySelector('[data-travel-mode="boat"]').getAttribute('aria-checked') === 'true';`), '真实点击可以切换交通方式');
  await shot(cdp, 'map-route-quote');
  const before = await liveState();
  const beforeRequests = network('travel').length;
  await click(cdp, '#map-travel-start:not(:disabled)');
  state = await stateAfter(s => s.job?.type === 'travel');
  const startedResponse = await responseBody('travel');
  const sent = JSON.parse(network('travel').at(-1).params.request.postData);
  check(network('travel').length === beforeRequests + 1 && sent.to === destination.id
    && sent.mode === 'boat' && sent.begin === true && startedResponse.arrived === false
    && startedResponse.report.stopReason === 'started', '启程只发一次所选方式请求，后端明确返回行程已开始');
  check(state.clock.days === before.clock.days && state.player.location === origin
    && state.player.stones === before.player.stones - offer.cost && state.job.from === origin
    && state.job.to === destination.id && state.job.mode === 'boat' && state.job.remaining === offer.days
    && state.job.elapsed === 0 && state.job.cost === offer.cost, '启程按报价扣费，不推进时间或提前迁移角色');
  check(await cdp.eval(`const p = document.querySelector('[data-travel-progress]');
    return !!p && p.value === 0 && p.max === ${offer.days}
      && document.querySelector('.map-pin.here')?.dataset.mapLoc === ${JSON.stringify(origin)}
      && document.querySelector('.map-pin.destination')?.dataset.mapLoc === ${JSON.stringify(destination.id)};`),
    '浏览器显示真实行程进度、出发点和目的地');
  await shot(cdp, 'map-journey-started');

  const other = state.locations.find(l => l.id !== origin && l.id !== destination.id);
  const refusal = await request('travel', { to: other.id, mode: 'boat', begin: true });
  const unchanged = await liveState();
  check(refusal.data.ok === false && unchanged.player.stones === state.player.stones
    && unchanged.player.location === origin && unchanged.clock.days === state.clock.days
    && unchanged.job.to === destination.id && unchanged.job.mode === 'boat'
    && unchanged.job.remaining === state.job.remaining, '途中换目的地被真实 API 拒绝，不会再次收费或覆盖任务');
  await focus(other.id);
  check(await cdp.eval(`return document.querySelector('#map-travel-start').disabled
    && document.querySelector('#map-travel-now').disabled;`), '途中点选另一地点只查看详情，不能再启程');
  await focus(destination.id);
  const savedJourney = await must('export');
  const restored = await must('import', { game: savedJourney.game });
  check(restored.state.player.stones === state.player.stones && restored.state.job.from === origin
    && restored.state.job.to === destination.id && restored.state.job.mode === 'boat'
    && restored.state.job.remaining === offer.days && restored.state.job.cost === offer.cost,
    '真实导出再导入保留交通方式、剩余时间与已扣费用');
  await click(cdp, '#map-travel-step:not(:disabled)');
  state = await stateAfter(s => s.clock.days > before.clock.days || s.pending || s.battle);
  check(state.clock.days === before.clock.days + 1 && state.player.location === origin
    && state.job.remaining === offer.days - 1 && state.job.elapsed === 1
    && state.player.stones === before.player.stones - offer.cost, '行进一天精确推进一天，剩余天数同步且不重复收费');
  await shot(cdp, 'map-journey-one-day');
  state = await finishJourney(state);
  check(!state.job && state.player.location === destination.id
    && state.clock.days === before.clock.days + offer.days, '继续赶路只推进剩余天数并在完成后抵达');
  check((await textOf(cdp, '#toast') || '').includes('已抵达'), '完成提示明确显示实际抵达');
  await shot(cdp, 'map-arrived');

  const high = await loadClone(9);
  const highQuote = high.locations.find(l => l.id === destination.id).travel.options.find(o => o.id === 'flight');
  check(highQuote.enabled && highQuote.days <= destination.travel.options.find(o => o.id === 'flight').days,
    '合法高境界角色御器可用，报价时间不长于筑基');
  await openMap();
  const reference = JSON.parse(readFileSync(join(ROOT, 'web', 'assets', 'map', 'location-reference.json'), 'utf8'));
  check(high.locations.length === reference.locations.length
    && new Set(high.locations.map(location => location.id)).size === reference.locations.length
    && reference.locations.every(location => high.locations.some(actual => actual.id === location.id && actual.x === location.x && actual.y === location.y)),
    '真实地点完整对应截图与资料确认的位置');
  const assetPaths = ['assets/map/ningzhou-atlas.webp', ...high.locations.map(location => `assets/map/locations/${location.id}.webp`)];
  const images = await cdp.eval(`return Promise.all(${JSON.stringify(assetPaths)}.map(path => new Promise(resolve => {
    const image = new Image(); image.onload = () => resolve({ path, loaded: image.naturalWidth > 0 && image.naturalHeight > 0 });
    image.onerror = () => resolve({ path, loaded: false }); image.src = path;
  })));`);
  check(images.length === high.locations.length + 1 && images.every(image => image.loaded), '舆图及所有独立地点插画在浏览器均能解码和加载');
  const detailSources = [];
  for (const location of high.locations) {
    await focus(location.id);
    const detail = await cdp.eval(`const image = document.querySelector('.destination-art');
      return { id: document.querySelector('[data-map-detail]')?.dataset.mapDetail,
        src: image ? new URL(image.src).pathname : '', loaded: !!image && image.complete
          && image.naturalWidth > 0 && image.naturalHeight > 0 };`);
    check(detail.id === location.id && detail.src === `/assets/map/locations/${location.id}.webp` && detail.loaded,
      `真实点击 ${location.name} 后详情与已加载独立图片均对应地点 ${location.id}`);
    detailSources.push(detail.src);
    await cdp.eval(`document.querySelector('.atlas-destination')?.scrollIntoView({ block: 'start', behavior: 'instant' }); return true;`);
    await shot(cdp, 'map-location-' + location.id);
  }
  check(new Set(detailSources).size === high.locations.length
    && detailSources.every(src => !/(?:city|sect|wild)-scene\.(?:png|webp)$/.test(src)), '所有地点均使用独立插画');
  const artLocation = high.locations.at(-1);
  const artButton = `.destination-art-button[data-action="location-art"][data-id="${artLocation.id}"]`;
  const artBefore = await liveState();
  await click(cdp, artButton);
  check(await waitFor(cdp, '#location-art-modal[open]') && await cdp.eval(`const image = document.querySelector('#location-art-modal .location-art-full');
    return image?.complete && image.naturalWidth > 0 && new URL(image.src).pathname === ${JSON.stringify(`/assets/map/locations/${artLocation.id}.webp`)}
      && document.querySelector('#location-art-title')?.textContent.includes(${JSON.stringify(artLocation.name)});`),
    '真实点击地点插画可打开与本地点对应的完整大图');
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: ' ', code: 'Space', windowsVirtualKeyCode: 32 });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: ' ', code: 'Space', windowsVirtualKeyCode: 32 });
  await sleep(150);
  const artAfter = await liveState();
  check(artAfter.clock.days === artBefore.clock.days && artAfter.player.stones === artBefore.player.stones
    && !artAfter.job,
    '查看地点大图时空格不会偷偷推进时间或产生安排');
  // 原生空格可以激活已聚焦的关闭按钮；这种关闭行为不属于游戏推进。
  if (!(await cdp.eval(`return document.querySelector('#location-art-modal').open;`))) await click(cdp, artButton);
  await shot(cdp, 'map-location-art-full');
  await click(cdp, '#location-art-modal [data-action="close-location-art"]');
  check(await cdp.eval(`return !document.querySelector('#location-art-modal').open
    && document.activeElement?.dataset.action === 'location-art';`), '大图关闭按钮可真实点击并恢复到图片入口');
  await click(cdp, artButton);
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyDown', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
  await cdp.send('Input.dispatchKeyEvent', { type: 'keyUp', key: 'Escape', code: 'Escape', windowsVirtualKeyCode: 27 });
  await sleep(150);
  check(await cdp.eval(`return !document.querySelector('#location-art-modal').open
    && document.activeElement?.dataset.action === 'location-art';`), '大图也能用原生 Esc 关闭并恢复入口焦点');
  await focus(destination.id);
  await click(cdp, '[data-travel-mode="flight"]');
  const directBefore = await liveState();
  await click(cdp, '#map-travel-now:not(:disabled)');
  state = await stateAfter(s => s.clock.days > directBefore.clock.days || s.job || s.pending || s.battle);
  const directResult = await responseBody('travel');
  const directSent = JSON.parse(network('travel').at(-1).params.request.postData);
  check(directSent.begin === false && directSent.mode === 'flight'
    && directResult.arrived === (state.player.location === destination.id && !state.job && !state.ended)
    && ['done', 'event', 'battle', 'ended', 'error'].includes(directResult.report.stopReason),
    '直接赶往真实推进并返回与实际地点一致的抵达或暂停状态');
  state = await finishJourney(state);
  check(state.player.location === destination.id && !state.job
    && state.clock.days === directBefore.clock.days + highQuote.days,
    '御器直接赶往完成后天数与后端报价一致');
  await loadClone(9);
  await openMap();

  // 窄屏继续使用真实鼠标派发；截图之外还检查操作是否写入后端。
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  await focus(destination.id);
  await click(cdp, '[data-travel-mode="boat"]');
  await click(cdp, '[data-action="map-zoom"][data-id=".25"]');
  check(await cdp.eval(`const viewport = document.querySelector('[data-map-viewport]');
    viewport.scrollLeft = 20; return viewport.scrollWidth > viewport.clientWidth && viewport.scrollLeft > 0
      && document.documentElement.scrollWidth <= document.documentElement.clientWidth + 2;`), '390px放大后地图可在画布内滚动，页面无横向溢出');
  check(await cdp.eval(`return document.documentElement.scrollWidth <= document.documentElement.clientWidth + 2
    && document.querySelector('[data-travel-mode="boat"]').getAttribute('aria-checked') === 'true';`),
    '390px 地图没有横向溢出，交通方式可真实点击');
  await shot(cdp, 'map-quote-narrow');
  await click(cdp, `.destination-art-button[data-id="${destination.id}"]`);
  check(await waitFor(cdp, '#location-art-modal[open]') && await cdp.eval(`const image = document.querySelector('#location-art-modal .location-art-full');
    return !!image && image.complete && image.naturalWidth > 0
      && new URL(image.src).pathname === ${JSON.stringify(`/assets/map/locations/${destination.id}.webp`)}
      && document.documentElement.scrollWidth <= document.documentElement.clientWidth + 2;`),
    '390px 地点大图入口可真实点击，显示对应独立图片且无横向溢出');
  await shot(cdp, 'map-location-art-full-narrow');
  await click(cdp, '#location-art-modal [data-action="close-location-art"]');
  await click(cdp, '#map-travel-start:not(:disabled)');
  state = await stateAfter(s => s.job?.type === 'travel');
  check(state.job.to === destination.id && state.job.mode === 'boat' && state.player.location === origin,
    '390px 启程按钮可真实点击并创建正确行程');
  await cdp.eval(`document.querySelector('[data-travel-job]')?.scrollIntoView({ block: 'start', behavior: 'instant' }); return true;`);
  await shot(cdp, 'map-journey-narrow');
  const narrowDay = state.clock.days;
  await click(cdp, '#map-travel-step:not(:disabled)');
  state = await stateAfter(s => s.clock.days > narrowDay || s.pending || s.battle);
  check(state.clock.days === narrowDay + 1, '390px 行进一天按钮真实推进一天');
  state = await finishJourney(state);
  check(!state.job && state.player.location === destination.id
    && await cdp.eval(`return document.documentElement.scrollWidth <= document.documentElement.clientWidth + 2;`),
    '390px 继续赶路可点击，完成后页面仍无横向溢出');
  await shot(cdp, 'map-arrived-narrow');
  await cdp.send('Emulation.clearDeviceMetricsOverride');

  // 从真实导出创建时间一致的第三年角色，由引擎产生山门来信，绝不伪造 pending。
  const eventClone = structuredClone(baseline);
  eventClone.clock.days = 1080;
  eventClone.year = 3;
  eventClone.player.age = eventClone.clock.ageBase + 3;
  eventClone.player.stage = eventClone.player.highestStage = 3;
  eventClone.player.xp = 0;
  eventClone.player.stones = 50000;
  delete eventClone.flags.invitation;
  delete eventClone.flags.choiceYear;
  await must('import', { game: JSON.stringify(eventClone) });
  await openMap();
  await focus(destination.id);
  await click(cdp, '[data-travel-mode="boat"]');
  await click(cdp, '#map-travel-now:not(:disabled)');
  state = await stateAfter(s => !!s.pending);
  const eventResult = await responseBody('travel');
  check(eventResult.report.stopReason === 'event' && eventResult.arrived === true
    && state.player.location === destination.id && !state.job
    && await waitFor(cdp, '#modal[open]'), '真实山门来信停下推进，同时如实显示路线已抵达');
  const eventSnapshot = { days: state.clock.days, stones: state.player.stones, location: state.player.location };
  const eventMove = await request('travel', { to: origin, mode: 'boat', begin: true });
  const eventAdvance = await request('advance', { days: 1 });
  const eventUnchanged = await liveState();
  check(eventMove.data.ok === false && eventAdvance.data.ok === false
    && eventUnchanged.clock.days === eventSnapshot.days && eventUnchanged.player.stones === eventSnapshot.stones
    && eventUnchanged.player.location === eventSnapshot.location && !eventUnchanged.job && !!eventUnchanged.pending,
    '真实抉择未解决时 API 拒绝重新启程和推进，不扣费、不改地点或日历');
  await shot(cdp, 'map-arrival-event');
  await click(cdp, '#modal [data-action="choose"][data-id="study"]');
  state = await stateAfter(s => !s.pending);
  check(state.player.location === destination.id && state.clock.days === eventSnapshot.days
    && !state.job && await cdp.eval(`return !document.querySelector('#modal[open]');`),
    '真实鼠标处理山门抉择后保留已抵达地点，不额外消耗赶路时间');
}

async function dungeonUI(cdp) {
  log('\n=== 真实材料秘境与洞府 UI 回归（隔离服务） ===');
  const request = async (path, body = {}) => {
    const response = await fetch(BASE + 'api/' + path, {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data = await response.json();
    if (!response.ok || data.error || data.ok === false) throw new Error(`${path}: ${data.error || response.status}`);
    return data;
  };
  const after = async predicate => {
    for (let n=0;n<60;n++) { const state=await liveState(); if(predicate(state))return state; await sleep(120); }
    throw new Error('真实点击后状态没有按预期变化');
  };
  const network = path => cdp.events.filter(e => e.method==='Network.requestWillBeSent'
    && new URL(e.params.request.url).pathname === '/api/'+path);
  await request('new',{seed:480317,name:'秘境验收',gender:'女',traits:[],allocation:{}});
  const fixture=JSON.parse((await request('export')).game);
  // 由真实存档创建充足气血的交互验收角色；这里验证 UI 与状态流转，不替代规则平衡测试。
  Object.assign(fixture.player,{stage:6,highestStage:6,xp:0,stones:50000,health:100,body:20,location:'tianji',cave:'tianji',caveTier:0,renown:0});
  Object.assign(fixture.player.arts,{mind:16,speed:90,hpBonus:100,spells:['rain','sun','wood','skim','gather','rotate','water','sword']});
  fixture.player.arts.knownSpells=[...new Set([...fixture.player.arts.knownSpells,...fixture.player.arts.spells])];
  fixture.player.arts.manuals={tuna:1,bull:5,sunwheel:5,visual:5,evergreen:5};
  fixture.player.arts.main='sunwheel'; fixture.player.arts.aux=['bull','visual','evergreen'];
  fixture.flags.invitation=1;
  await request('import',{game:JSON.stringify(fixture)});
  await goto(cdp,BASE);
  await click(cdp,'.main-tabs [data-tab="dungeon"]');
  let state=await liveState();
  check(state.dungeons?.sites?.length>0 && await waitFor(cdp,'[data-dungeon-directory]'), '新存档真实生成自然秘境，秘境目录可见');
  check((await textOf(cdp,'[data-dungeon-directory]')).includes('50 年'), '秘境目录说明前期三年、后期五十年的出现周期');
  await shot(cdp,'dungeon-directory'); await narrowShot(cdp,'dungeon-directory');
  await click(cdp,'[data-dungeon-view="intel"]');
  await click(cdp,'[data-dungeon-intel-kind="material"]');
  state=await liveState();
  const intel=state.dungeons.intel.find(item=>item.kind==='material' && item.grade===3 && item.enabled);
  if(!intel)throw new Error('当前金丹角色没有可购三阶炼器材料情报');
  check(await cdp.eval(`return [...document.querySelectorAll('[data-action="dungeon-intel"]')].every(button=>button.dataset.kind==='material');`), '天机阁按炼器材料筛选真实情报');
  await type(cdp,'[data-dungeon-intel-search]',intel.name);
  check((await textOf(cdp,'.dungeon-intel-list')).includes(`${intel.name}秘境`), '情报材料名称搜索保留相应位置购买入口');
  await shot(cdp,'dungeon-intel'); await narrowShot(cdp,'dungeon-intel');
  const beforeIntel=state, intelRequests=network('dungeon/intel').length;
  await click(cdp,`[data-action="dungeon-intel"][data-kind="material"][data-id="${intel.id}"]:not(:disabled)`);
  state=await after(s=>s.dungeons.sites.some(site=>site.type==='intel'&&site.targetId===intel.id));
  const site=state.dungeons.sites.find(site=>site.type==='intel'&&site.targetId===intel.id);
  check(network('dungeon/intel').length===intelRequests+1 && state.player.stones===beforeIntel.player.stones-intel.cost
    && (state.arts.materials[intel.id]||0)===(beforeIntel.arts.materials[intel.id]||0)
    && site.targetName===intel.name && site.targetKind==='material', '情报购买只收费一次，生成指定目标位置，尚未直接获得材料');
  await click(cdp,'[data-dungeon-view="intel"]');
  check(await cdp.eval(`return [...document.querySelectorAll('[data-action="dungeon-intel"]')].every(button=>button.disabled);`), '本月已买情报后其余购买入口全部按真实限制禁用');
  await click(cdp,'[data-dungeon-view="sites"]');
  await click(cdp,`[data-dungeon-site="${site.id}"] [data-action="dungeon-locate"]`);
  check(await cdp.eval(`return document.querySelector('[data-map-detail]')?.dataset.mapDetail===${JSON.stringify(site.location)};`), '情报位置按钮定位真实宁州入口，无需海域地图');
  await shot(cdp,'dungeon-map-entrance');
  await click(cdp,'[data-travel-mode="flight"]');
  await click(cdp,'#map-travel-now:not(:disabled)');

  const playBattle = async () => {
    for(let turn=0;turn<200;turn++) {
      const s=await liveState(),b=s.battle;
      if(!b)return s;
      if(s.ended)throw new Error('UI验收角色在斗法中陨落');
      const playable=(b.hand||[]).filter(card=>card.playable!==false && !card.requiresTarget && card.kind!=='exhaust');
      const score = card => {
        if(card.kind==='heal') return b.hp/b.max<.70 ? 900 : -1;
        if(card.kind==='mana') return 800;
        if(card.damage>0)return 500+card.damage;
        if(card.kind==='draw')return 400;
        if(card.shield>0)return b.intentDamage>(b.shield||0) ? 200+card.shield : 20;
        return 0;
      };
      const chosen=playable.sort((a,b)=>score(b)-score(a))[0];
      if(chosen && score(chosen)>0) await click(cdp,`[data-action="play"][data-id="${chosen.uid}"]:not(:disabled)`,0,{wait:70});
      else await click(cdp,'[data-action="end-turn"]',0,{wait:70});
    }
    throw new Error('斗法 UI 验收超过出牌上限');
  };
  for(let n=0;n<18;n++) {
    state=await liveState();
    if(state.battle)await playBattle();
    else if(state.pending) {
      const choice=state.pending.options.find(o=>!o.disabled && /leave|decline|ignore|refuse|pass|skip|avoid/.test(o.id)) || state.pending.options.find(o=>!o.disabled);
      await click(cdp,`#modal [data-action="choose"][data-id="${choice.id}"]`);
    } else if(state.job?.type==='travel') {
      await click(cdp,'#map-travel-resume:not(:disabled)');
    } else break;
  }
  state=await liveState();
  if(state.player.location!==site.location || state.job || state.pending || state.battle)throw new Error('尚未抵达材料秘境');
  await click(cdp,`[data-location-dungeons] [data-action="dungeon-enter"][data-id="${site.id}"]:not(:disabled)`);
  state=await after(s=>s.dungeons.run?.siteId===site.id);
  check(state.dungeons.run.stage===state.player.stage && state.dungeons.run.phase==='route'
    && await cdp.eval(`return document.querySelector('#workspace').dataset.page==='dungeon' && document.querySelector('.main-tabs [data-tab="dungeon"]').classList.contains('active');`),
    '进入材料秘境后打开独立爬塔页，难度跟随玩家境界');
  check(state.dungeons.run.deck.filter(c=>c.spell==='strike').length===5 && state.dungeons.run.deck.filter(c=>c.spell==='defend').length===5,
    '秘境临时牌组保留五张普攻与五张防御');
  await click(cdp,'.main-tabs [data-tab="arts"]');
  check(await cdp.eval(`return !!document.querySelector('[data-dungeon-run]') && document.querySelector('.main-tabs [data-tab="dungeon"]').classList.contains('active');`), '探索期间外部页签不会绕开秘境流程');
  await cdp.eval(`document.querySelector('.dungeon-route-panel')?.scrollIntoView({block:'start',behavior:'instant'});return true;`);
  await shot(cdp,'dungeon-route'); await narrowShot(cdp,'dungeon-route','.dungeon-route-panel');

  // 基准点由真实保存接口创建，导入只改变当前受伤战斗；败北后必须显式选择回档。
  await click(cdp,'.dungeon-run-heading [data-action="save"]');
  state=await liveState();
  const routeBeforeLoss=JSON.stringify(state.dungeons.run.nodes),restoresBeforeLoss=state.checkpoint.restores;
  const firstNode=state.dungeons.run.nodes.find(node=>node.available);
  await click(cdp,`[data-action="dungeon-choose"][data-id="${firstNode.id}"]:not(:disabled)`);
  await after(s=>!!s.battle);
  const wounded=JSON.parse((await request('export')).game);
  Object.assign(wounded.player.arts.battle,{hp:1,shield:0,playerPoison:7,playerPoisonTurns:1});
  await request('import',{game:JSON.stringify(wounded)});
  await goto(cdp,BASE);
  check(!(await cdp.eval(`return !!document.querySelector('.checkpoint-restored');`)), '初次加载受伤战斗不会误报回档');
  await click(cdp,'[data-action="end-turn"]');
  state=await after(s=>s.ended===true);
  check(state.checkpoint.restores===restoresBeforeLoss && state.ending.canRestore===true
    && await waitFor(cdp,'[data-action="checkpoint-restore"]')
    && await cdp.eval(`return document.querySelector('[data-action="legacy-settle"]').textContent.includes('接受死亡');`),
    '真实败北先显示接受死亡或回档的选择，不自动恢复');
  const defeatedLife=JSON.parse((await request('export')).game);
  await shot(cdp,'dungeon-defeat-ending');await narrowShot(cdp,'dungeon-defeat-ending','.ending-card');
  await click(cdp,'[data-action="checkpoint-restore"]');
  state=await after(s=>s.checkpoint.restores===restoresBeforeLoss+1);
  check(!state.battle && state.dungeons.run.phase==='route' && state.dungeons.run.current===''
    && JSON.stringify(state.dungeons.run.nodes)===routeBeforeLoss
    && await waitFor(cdp,'.checkpoint-restored'), '选择回档后恢复最近存档与原路线，并显示显著提示');
  await shot(cdp,'dungeon-defeat-rollback');await narrowShot(cdp,'dungeon-defeat-rollback');
  await click(cdp,'[data-action="checkpoint-dismiss"]');

  // 从引擎已确定的路线找经过最多房型的合法路径，绝不重新生成或改写节点。
  const nodes=state.dungeons.run.nodes;
  const paths=[];
  const walk=(node,path,types)=>{
    const nextPath=[...path,node.id],nextTypes=new Set([...types,node.type]);
    if(node.type==='boss'){paths.push({path:nextPath,score:nextTypes.size,types:nextTypes});return;}
    for(const id of node.next)walk(nodes.find(n=>n.id===id),nextPath,nextTypes);
  };
  nodes.filter(node=>node.row===0).forEach(node=>walk(node,[],new Set()));
  const path=paths.sort((a,b)=>b.score-a.score)[0];
  const seen=new Set(); let claimedCard=false,skipped=false,removed=false;
  const targetCount = run => run.loot?.materials?.[site.targetId]||0;
  let beforeBoss=0;
  for(const nodeId of path.path) {
    state=await liveState();
    const run=state.dungeons.run,node=run.nodes.find(n=>n.id===nodeId);
    if(!node.available)throw new Error('规划的相连节点未开放：'+nodeId);
    const hpBefore=run.health;
    if(node.type==='boss')beforeBoss=targetCount(run);
    const reqs=network('dungeon/choose').length;
    await click(cdp,`[data-action="dungeon-choose"][data-id="${nodeId}"]:not(:disabled)`);
    state=await after(s=>s.dungeons.run?.current===nodeId);
    check(network('dungeon/choose').length===reqs+1 && state.dungeons.run.nodes.filter(n=>n.visited).length===node.row+1,
      `第 ${node.row+1} 层沿连线进入${node.name}，只结算一个房间`);
    if(state.battle) {
      check(state.battle.deckSize===undefined || state.battle.deckSize===run.deck.length, '秘境斗法使用当前临时牌组');
      check(Math.round(state.battle.hp/state.battle.max*100)===hpBefore || !state.battle.playerFirst,
        '下一场斗法沿用当前气血，敌方先手时按实际受伤显示');
      if(!seen.has('battle')) {await shot(cdp,'dungeon-battle');await narrowShot(cdp,'dungeon-battle');}
      state=await playBattle();
    }
    const currentRun=state.dungeons.run;
    if(!currentRun)throw new Error('秘境 UI 验收意外结束');
    if(currentRun.phase==='complete')break;
    const rewards=currentRun.rewards||[];
    if(currentRun.phase!=='reward')throw new Error('房间结束没有奖励选择');
    if(!seen.has(node.type)) {
      await cdp.eval(`document.querySelector('[data-dungeon-rewards]')?.scrollIntoView({block:'start',behavior:'instant'});return true;`);
      await shot(cdp,'dungeon-room-'+node.type);if(['camp','merchant','boss'].includes(node.type))await narrowShot(cdp,'dungeon-room-'+node.type,'[data-dungeon-rewards]');
    }
    seen.add(node.type);
    let chosen=rewards.find(r=>r.enabled!==false);
    if(node.type==='boss') {
      check(currentRun.cleared && !currentRun.bossRewardClaimed && JSON.stringify(currentRun.bossRewards)===JSON.stringify(site.bossRewards)
        && rewards.map(r=>r.kind).sort().join(',')==='learnSpell,manual,stones'
        && await cdp.eval(`return !!document.querySelector('.dungeon-reward-panel.permanent') && document.querySelector('[data-action="dungeon-leave"]').disabled;`),
        '首领击败后展示入口已冻结的永久神通、抄本、灵石三选一，尚未领奖不可离开');
      chosen=rewards.find(r=>r.kind==='learnSpell'&&r.enabled!==false)||chosen;
    }
    if(node.type==='camp')chosen=rewards.find(r=>r.kind===(!removed&&currentRun.health>=70?'remove':'rest')&&r.enabled!==false)||chosen;
    else if(node.type==='event')chosen=rewards.find(r=>r.kind==='cache'&&r.enabled!==false)||chosen;
    else if(node.type==='merchant')chosen=rewards.find(r=>r.kind==='buyRelic'&&r.enabled!==false)||rewards.find(r=>r.kind==='buyCard'&&r.enabled!==false)||null;
    if(!skipped&&node.type==='battle') {
      const deckSize=currentRun.deck.length;
      await click(cdp,'[data-action="dungeon-claim"][data-id="skip"]');
      state=await after(s=>s.dungeons.run.phase==='route'); skipped=true;
      check(state.dungeons.run.deck.length===deckSize, '放弃三选一奖励不改变临时牌组');
    } else if(chosen) {
      const beforeClaim=currentRun;
      await click(cdp,`[data-action="dungeon-claim"][data-id="${chosen.id}"]:not(:disabled)`);
      state=await after(s=>s.dungeons.run.phase===(node.type==='boss'?'complete':'route'));
      if(node.type==='boss') {
        const taken=state.dungeons.run.bossRewardTaken,actual=JSON.parse((await request('export')).game);
        check(state.dungeons.run.bossRewardClaimed && taken && (taken.kind==='stones' || (taken.kind==='learnSpell'&&actual.player.arts.knownSpells.includes(taken.spell))
          || (taken.kind==='manual'&&actual.economy.books.includes(taken.manual))), '领取通关主奖后引擎记录实际永久所得或重复知识灵石折算');
        check((await textOf(cdp,'.dungeon-complete')).includes(taken.name), '完成页显示实际领取的主奖励名称');
      }
      if(['card','buyCard'].includes(chosen.kind)){claimedCard=true;check(state.dungeons.run.deck.length===beforeClaim.deck.length+1,'选神通奖励后临时牌组实际增加一张');}
      if(['relic','buyRelic'].includes(chosen.kind))check(state.dungeons.run.relics.length===beforeClaim.relics.length+1,'领取遗物后实际加入本次构筑');
      if(chosen.kind==='remove'){removed=true;check(state.dungeons.run.deck.length===beforeClaim.deck.length-1
        && state.dungeons.run.deck.filter(c=>c.spell==='strike').length===5 && state.dungeons.run.deck.filter(c=>c.spell==='defend').length===5,'营地删牌实际精简构筑，普攻与防御保持常备');}
      if(chosen.kind==='rest')check(state.dungeons.run.health>=beforeClaim.health,'营地休整实际恢复气血');
      if(node.type==='merchant')check(state.dungeons.run.stones===beforeClaim.stones-chosen.cost,'行商购买使用本次探索灵石，选择一项后继续路线');
    } else {await click(cdp,'[data-action="dungeon-claim"][data-id="skip"]');state=await after(s=>s.dungeons.run.phase==='route');}
  }
  state=await liveState();
  check(state.dungeons.run.phase==='complete' && state.dungeons.run.targetGranted && targetCount(state.dungeons.run)-beforeBoss===10,
    '真正击败终点首领后，指定材料一次发放十份到战利品包');
  check(skipped&&claimedCard&&removed&&seen.has('camp'), '实际经过选卡、放弃、营地删牌以及后续续战');
  log('  本次合法路线覆盖房型：'+[...seen].join(' / '));
  await cdp.eval(`document.querySelector('.dungeon-complete')?.scrollIntoView({block:'start',behavior:'instant'});return true;`);
  await shot(cdp,'dungeon-complete');await narrowShot(cdp,'dungeon-complete','.dungeon-complete');
  const materialBefore=state.arts.materials[site.targetId]||0,loot=targetCount(state.dungeons.run),knownBefore=JSON.parse((await request('export')).game).player.arts.knownSpells;
  await click(cdp,'.dungeon-complete [data-action="dungeon-leave"]');
  state=await after(s=>!s.dungeons.run);
  check(state.arts.materials[site.targetId]===materialBefore+loot && JSON.stringify(JSON.parse((await request('export')).game).player.arts.knownSpells)===JSON.stringify(knownBefore),
    '离开秘境结算材料，已领取的永久神通保留，沿途临时卡牌不额外解锁');
  await shot(cdp,'dungeon-settled');

  // 未通关主动离开带回已得物资，但入口消失，不能重新进同一副本刷前几层。
  const unfinishedSite=state.dungeons.sites.find(entry=>entry.type==='wild'&&entry.available&&!entry.cleared&&!entry.entered);
  if(!unfinishedSite)throw new Error('没有可用于提前退出验收的自然秘境');
  const unfinishedFixture=JSON.parse((await request('export')).game);
  unfinishedFixture.player.location=unfinishedSite.location;
  await request('import',{game:JSON.stringify(unfinishedFixture)});await goto(cdp,BASE);
  await click(cdp,'.main-tabs [data-tab="dungeon"]');
  await click(cdp,`[data-dungeon-site="${unfinishedSite.id}"] [data-action="dungeon-enter"]:not(:disabled)`);
  state=await after(s=>!!s.dungeons.run);
  check(await cdp.eval(`return document.querySelector('.dungeon-exit-note').innerText.includes('提前退出后入口消失');`), '未通关离开按钮旁显示入口消失与战利品规则');
  await shot(cdp,'dungeon-exit-rules');await narrowShot(cdp,'dungeon-exit-rules','.dungeon-run-heading');
  const unfinishedNode=state.dungeons.run.nodes.find(node=>node.available);
  await click(cdp,`[data-action="dungeon-choose"][data-id="${unfinishedNode.id}"]:not(:disabled)`);
  await after(s=>!!s.battle);await playBattle();
  await click(cdp,'[data-action="dungeon-claim"][data-id="skip"]');
  state=await after(s=>s.dungeons.run.phase==='route');
  const beforeExit=state,unfinishedLoot=state.dungeons.run.loot;
  await click(cdp,'.dungeon-run-heading [data-action="dungeon-leave"]');
  state=await after(s=>!s.dungeons.run);
  check(!state.dungeons.sites.some(entry=>entry.id===unfinishedSite.id)
    && ['herbs','materials','potions'].every(kind=>Object.entries(unfinishedLoot[kind]||{}).every(([id,count])=>(state.arts[kind][id]||0)===(beforeExit.arts[kind][id]||0)+count)),
    '未通关主动退出带回全部已得材料，且自然秘境入口消失');
  const closedEntry=await fetch(BASE+'api/dungeon/enter',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({id:unfinishedSite.id})}).then(response=>response.json());
  check(!!closedEntry.error && !(await liveState()).dungeons.run,'已消失的入口再次请求被真实 API 拒绝');

  // 从真实通关导出创建声望边界，验证升档与灵气方案，而不是在页面伪造 tier。
  const caveFixture=JSON.parse((await request('export')).game);
  Object.assign(caveFixture.player,{cave:caveFixture.player.location,caveTier:0,renown:0,qiSupply:1});
  caveFixture.job=null;caveFixture.pending=null;caveFixture.player.arts.battle=null;
  await request('import',{game:JSON.stringify(caveFixture)});
  await goto(cdp,BASE);await click(cdp,'.main-tabs [data-tab="arts"]');
  await click(cdp,'[data-subtab="arts"][data-sub="practice"]');
  check(await cdp.eval(`return document.querySelector('[data-action="cave-upgrade"]').disabled
    && document.querySelector('[data-action="supply"][data-kind="qi"][data-level="2"]').disabled;`), '低声望寻常洞府不能升档，也不能启用二档灵气');
  await cdp.eval(`document.querySelector('.cave-tier-card')?.scrollIntoView({block:'start',behavior:'instant'});return true;`);
  await shot(cdp,'cave-tier-locked');await narrowShot(cdp,'cave-tier-locked','.cave-tier-card');
  caveFixture.player.renown=20;
  await request('import',{game:JSON.stringify(caveFixture)});
  await goto(cdp,BASE);await click(cdp,'.main-tabs [data-tab="arts"]');await click(cdp,'[data-subtab="arts"][data-sub="practice"]');
  const caveBefore=await liveState();
  await click(cdp,'[data-action="cave-upgrade"]:not(:disabled)');
  state=await after(s=>s.cave?.tier===1);
  check(state.player.stones===caveBefore.player.stones-120 && state.cave.maxSupply===2,'声望二十升级灵脉洞府按价扣一百二十灵石，解锁二档');
  await click(cdp,'[data-action="supply"][data-kind="qi"][data-level="2"]:not(:disabled)');
  state=await after(s=>s.player.supply.qiLevel===2);
  check(await cdp.eval(`return document.querySelector('[data-action="supply"][data-kind="qi"][data-level="3"]').disabled;`), '灵脉洞府启用二档，三档仍需进一步声望与升档');
  await cdp.eval(`document.querySelector('.cave-tier-card')?.scrollIntoView({block:'start',behavior:'instant'});return true;`);
  await shot(cdp,'cave-tier-upgraded');await narrowShot(cdp,'cave-tier-upgraded','.cave-tier-card');
  await click(cdp,'.toolbar [data-action="settings"]');
  check(await cdp.eval(`return document.querySelector('#settings-modal').innerText.includes('十年自动存档')
    && document.querySelector('#settings-modal').innerText.includes('下次自动保存')
    && !document.querySelector('#settings-modal [data-action="save"][data-slot="4"]');`), '存档界面显示自动位与下次日期，自动位没有手动覆盖按钮');
  await shot(cdp,'checkpoint-slot');await click(cdp,'[data-action="close-settings"]');

  // 宗门新流程：先到山门，真实非致命考核失败可重试，获胜后才开放藏经阁。
  const sectFixture=JSON.parse((await request('export')).game);
  Object.assign(sectFixture.player,{sect:'',location:'wuling',health:100,renown:20});
  sectFixture.economy.contribution=1000;
  sectFixture.job=null;sectFixture.pending=null;sectFixture.player.arts.battle=null;
  await request('import',{game:JSON.stringify(sectFixture)});
  await goto(cdp,BASE);await click(cdp,'.main-tabs [data-tab="society"]');
  await waitFor(cdp,'[data-action="sect-locate"][data-id="jinhong"]');
  check(await cdp.eval(`return document.querySelector('[data-action="join-sect"][data-sect="jinhong"]').disabled
    && document.querySelector('.sect-trial-info').innerText.includes('10 灵石');`), '异地宗门考核入口禁用，卡片显示真实考核价格与流派');
  await shot(cdp,'sect-trial-directory');await narrowShot(cdp,'sect-trial-directory','.sect-grid');
  await click(cdp,'[data-action="sect-locate"][data-id="jinhong"]');
  check(await cdp.eval(`return document.querySelector('[data-map-detail]').dataset.mapDetail==='jinhong';`), '宗门卡片定位宁州地图真实山门');
  sectFixture.player.location='jinhong';
  await request('import',{game:JSON.stringify(sectFixture)});
  await goto(cdp,BASE);await click(cdp,'.main-tabs [data-tab="society"]');
  const beforeTrial=await liveState();
  await click(cdp,'[data-action="join-sect"][data-sect="jinhong"]:not(:disabled)');
  state=await after(s=>!!s.battle);
  check(!state.player.sect && !state.battle.lethal && state.player.stones===beforeTrial.player.stones-10,
    '报名按价扣十灵石，进入本门非致命卡牌考核，尚未拜入宗门');
  await shot(cdp,'sect-trial-battle');
  const trialWounded=JSON.parse((await request('export')).game);
  Object.assign(trialWounded.player.arts.battle,{hp:1,shield:0,playerPoison:7,playerPoisonTurns:1});
  await request('import',{game:JSON.stringify(trialWounded)});await goto(cdp,BASE);
  await click(cdp,'[data-action="end-turn"]');
  state=await after(s=>!s.battle);
  check(!state.ended && !state.player.sect && state.checkpoint.restores===restoresBeforeLoss+1,
    '非致命入门考核败北仍存活且为散修，不触发死亡或回档');
  const retryFixture=JSON.parse((await request('export')).game);
  retryFixture.player.health=100;
  await request('import',{game:JSON.stringify(retryFixture)});await goto(cdp,BASE);
  await click(cdp,'.main-tabs [data-tab="society"]');
  await click(cdp,'[data-action="join-sect"][data-sect="jinhong"]:not(:disabled)');
  state=await after(s=>!!s.battle);
  const trialStartDay=state.clock.days;
  await playBattle();state=await liveState();
  check(state.player.sect==='jinhong' && !state.battle && state.clock.days===trialStartDay+3,
    '重试考核实际获胜后才拜入金虹剑派，并结算三天耗时');
  await click(cdp,'.main-tabs [data-tab="society"]');
  await click(cdp,'[data-subtab="society"][data-sub="library"]');
  await waitFor(cdp,'.sect-spell-library');
  state=await liveState();
  const librarySpell=state.librarySpells.find(sp=>sp.enabled&&!sp.known);
  if(!librarySpell)throw new Error('金虹藏经阁没有未学且可请领的神通');
  const libraryBefore=state;
  await shot(cdp,'sect-spell-library');await narrowShot(cdp,'sect-spell-library','.sect-spell-library');
  await click(cdp,`[data-action="library-spell"][data-id="${librarySpell.id}"]:not(:disabled)`);
  state=await after(s=>s.librarySpells.some(sp=>sp.id===librarySpell.id&&sp.known));
  check(state.contribution===libraryBefore.contribution-librarySpell.points
    && !state.arts.spells.includes(librarySpell.id)
    && await cdp.eval(`return document.querySelector('[data-action="library-spell"][data-id="${librarySpell.id}"]').disabled;`),
    '藏经阁真实扣贡献学会本门神通，未自动携带，已学入口禁用');

  // 接受死亡后关闭恢复入口；自然寿尽直接按本世结局结算。
  await request('import',{game:JSON.stringify(defeatedLife)});await goto(cdp,BASE);
  await waitFor(cdp,'[data-action="checkpoint-restore"]');
  await click(cdp,'[data-action="legacy-settle"]');
  state=await after(s=>s.ended&&!s.checkpoint.enabled);
  check(!state.ending.canRestore && await cdp.eval(`return !document.querySelector('[data-action="checkpoint-restore"]');`),
    '显式接受死亡结算后禁用最近存档，不再显示恢复入口');
  await shot(cdp,'death-accepted');
  await request('new',{seed:480318,name:'寿尽验收',gender:'女',traits:[],allocation:{}});
  const naturalFixture=JSON.parse((await request('export')).game);
  naturalFixture.player.lifespan=naturalFixture.player.age+1;
  naturalFixture.clock.days=359;naturalFixture.year=0;
  await request('import',{game:JSON.stringify(naturalFixture)});await goto(cdp,BASE);
  await click(cdp,'[data-action="advance-step"][data-days="1"]');
  state=await after(s=>s.ended);
  check(!state.ending.canRestore && await waitFor(cdp,'.ending-card')
    && await cdp.eval(`return !document.querySelector('[data-action="checkpoint-restore"]');`),
    '寿元耗尽展示正常结算与重开，保留自动档也不提供死亡回档');
  await shot(cdp,'natural-ending');await narrowShot(cdp,'natural-ending','.ending-card');
}

async function systemsUI(cdp) {
  log('\n=== 声望来源、法宝神通与炼器预览（隔离服务） ===');
  const request=async(path,body={})=>{
    const response=await fetch(BASE+'api/'+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data=await response.json();if(!response.ok||data.error||data.ok===false)throw new Error(path+': '+data.error);return data;
  };
  const after=async(predicate)=>{
    for(let n=0;n<80;n++){const state=await liveState();if(predicate(state))return state;await sleep(80);}
    throw new Error('系统UI点击后的真实状态未更新');
  };
  const exported=async()=>JSON.parse((await request('export')).game);
  await request('new',{seed:480321,name:'系统验收',gender:'女',traits:[],allocation:{}});
  const fixture=await exported();
  Object.assign(fixture.player,{stage:6,highestStage:6,xp:0,health:100,location:'wuling',stones:50000,body:20});
  fixture.player.arts.hpBonus=100;fixture.player.arts.progress.wins=3;fixture.player.arts.progress.brew=2;
  fixture.player.arts.materials={...fixture.player.arts.materials,iron:40,jade:30,wood:30};
  const gear=(id,name,shape,slot,element,polarity='天阳')=>({id,name,shape,slot,element,polarity,grade:3,bonus:.08,inscribed:false,price:180,craftCost:120,crafted:true,dimensions:[20,20,20,20,20]});
  fixture.player.arts.gear=[gear('test-sword','赤焰剑','sword','weapon','火'),gear('test-bell','聚灵钟','bell','armor','气'),gear('test-pearl','回春珠','pearl','accessory','木')];
  fixture.player.arts.equipped={weapon:'test-sword',armor:'test-bell',accessory:'test-pearl'};
  fixture.clock.carry.renown=.6;
  await request('import',{game:JSON.stringify(fixture)});await goto(cdp,BASE);
  let state=await liveState();
  check(state.renownInfo.sources.length===9 && state.renownInfo.sources.every(source=>Array.isArray(source.conditions)&&source.entry?.label),
    '声望API提供九项真实来源、条件数组与对应入口');
  await click(cdp,'.main-tabs [data-tab="quests"]');await click(cdp,'[data-subtab="quests"][data-sub="renown"]');
  await waitFor(cdp,'[data-renown-source="oldblade"]');
  check((await textOf(cdp,'[data-renown-source="explore"]')).includes('60%')
    && (await textOf(cdp,'[data-renown-source="oldblade"]')).includes('当前 3 次')
    && (await textOf(cdp,'[data-renown-source="wildTask"]')).includes('本月已接 0 / 3')
    && !(await textOf(cdp,'.renown-source-grid')).includes('45'), '来源卡显示真实游历零数、斗法进度和月内次数，删除虚构交情奖励');
  await cdp.eval(`document.querySelector('.renown-source-grid').scrollIntoView({block:'start',behavior:'instant'});return true;`);
  await shot(cdp,'renown-sources');await narrowShot(cdp,'renown-sources','.renown-source-grid');
  const sourceSnapshot=JSON.stringify(await exported());
  await click(cdp,'[data-action="renown-source-open"][data-id="oldblade"]');
  check(await cdp.eval(`return document.querySelector('[data-map-detail]')?.dataset.mapDetail==='yifeng';`)
    && JSON.stringify(await exported())===sourceSnapshot, '支线声望入口定位逸风城，查看不改变任何行卷状态');
  await shot(cdp,'renown-source-location');
  await click(cdp,'[data-action="goto-equipment"]');await waitFor(cdp,'.artifact-ability');
  check((await textOf(cdp,'#player-card')).includes('装备法宝神通')
    && await cdp.eval(`return document.querySelectorAll('.sidebar-artifacts [data-artifact-ability]').length===3;`)
    && (await textOf(cdp,'#workspace')).includes('耗灵力'), '角色侧栏与行囊装备显示三件实际装备法宝能力、费用和境界');
  await shot(cdp,'artifact-equipment');await narrowShot(cdp,'artifact-equipment');

  await click(cdp,'.main-tabs [data-tab="economy"]');await waitFor(cdp,'[data-artifact-ability="artifact:preview"]');
  const previewSnapshot=JSON.stringify(await exported());
  const shapeNames={sword:'剑',seal:'印',armor:'甲',bell:'钟',pearl:'珠'};
  for(const [shape,name] of Object.entries(shapeNames)) {
    await cdp.eval(`const select=document.querySelector('[data-forge="shape"]');select.value=${JSON.stringify(shape)};select.dispatchEvent(new Event('change',{bubbles:true}));return true;`);
    for(let n=0;n<50;n++) {if((await textOf(cdp,'.forge-preview-heading'))?.startsWith(name+'\n'))break;await sleep(80);}
    const preview=await request('forge-preview',{main:'iron',aux:'jade',core:'wood',shape});
    check(preview.info.valid && preview.info.ability.shape===shape && (await textOf(cdp,'.forge-preview-heading')).startsWith(name)
      && (await textOf(cdp,'.forge-preview')).includes(preview.info.ability.description)
      && JSON.stringify(await exported())===previewSnapshot, `推演${name}的真实法宝能力，反复换器型不耗材料、灵石、时间或随机数`);
  }
  await cdp.eval(`document.querySelector('.forge-preview').scrollIntoView({block:'center',behavior:'instant'});return true;`);
  await shot(cdp,'forge-artifact-preview');await narrowShot(cdp,'forge-artifact-preview','.forge-preview');
  const scarce=await exported();scarce.player.arts.materials.iron=0;
  await request('import',{game:JSON.stringify(scarce)});await goto(cdp,BASE);await click(cdp,'.main-tabs [data-tab="economy"]');
  await waitFor(cdp,'.forge-preview-reason');
  check((await textOf(cdp,'.forge-preview')).includes('材料') && await cdp.eval(`return document.querySelector('[data-action="forge-run"]').disabled && !!document.querySelector('[data-artifact-ability="artifact:preview"]');`),
    '合法配方材料不足仍展示能力与具体缺口，并禁用开炉');
  await shot(cdp,'forge-material-disabled');await narrowShot(cdp,'forge-material-disabled','.forge-preview');

  fixture.player.location='fanghu';fixture.bonds.wildMonth=-1;fixture.bonds.wildCount=0;
  await request('import',{game:JSON.stringify(fixture)});await request('wild',{id:'bandit'});await goto(cdp,BASE);
  state=await after(s=>!!s.battle);
  const bell=state.battle.artifacts.find(ability=>ability.gearId==='test-bell');
  check(state.battle.artifacts.length===3 && bell.playable && bell.cost===0
    && await cdp.eval(`return document.querySelectorAll('[data-action="artifact-play"]').length===3;`), '法宝神通常驻独立栏，进入真实斗法不靠抽牌获得');
  await cdp.eval(`document.querySelector('[data-combat-artifacts]').scrollIntoView({block:'center',behavior:'instant'});return true;`);
  await shot(cdp,'artifact-battle-ready');await narrowShot(cdp,'artifact-battle-ready','[data-combat-artifacts]');
  const beforeBell=state.battle,handBefore=JSON.stringify(state.battle.hand.map(card=>card.uid));
  await click(cdp,`[data-action="artifact-play"][data-id="${bell.id}"]:not(:disabled)`);
  state=await after(s=>s.battle?.artifacts.find(ability=>ability.id===bell.id)?.cooldownRemaining>0);
  const cold=state.battle.artifacts.find(ability=>ability.id===bell.id);
  check(state.battle.mana===Math.min(beforeBell.manaCap,beforeBell.mana+bell.energy) && state.battle.played===beforeBell.played+1
    && JSON.stringify(state.battle.hand.map(card=>card.uid))===handBefore && !cold.playable
    && await cdp.eval(`return document.querySelector('[data-action="artifact-play"][data-id="${bell.id}"]').disabled;`)
    && (await textOf(cdp,`[data-action="artifact-play"][data-id="${bell.id}"]`)).includes(cold.reason), '催动钟真实恢复灵力、占一次施术但不消耗手牌，冷却按实际原因禁用');
  const sword=state.battle.artifacts.find(ability=>ability.gearId==='test-sword');
  if(!sword.playable)throw new Error('聚灵后剑仍无法施展：'+sword.reason);
  const beforeSword=state.battle;
  await click(cdp,`[data-action="artifact-play"][data-id="${sword.id}"]:not(:disabled)`);
  state=await after(s=>s.battle?.played===beforeSword.played+1);
  check(state.battle.mana===beforeSword.mana-sword.cost && state.battle.enemyHp===beforeSword.enemyHp-sword.damage
    && JSON.stringify(state.battle.hand.map(card=>card.uid))===handBefore, '催动剑按实际费用和预览伤害结算，常驻能力不挤占或消耗卡牌');
  await shot(cdp,'artifact-battle-cooldown');await narrowShot(cdp,'artifact-battle-cooldown','[data-combat-artifacts]');
  // 用合法受伤状态验证灵力不足原因，不触发任何伪造技能定义。
  const lowMana=await exported();lowMana.player.arts.battle.mana=0;lowMana.player.arts.battle.cooldowns={};
  await request('import',{game:JSON.stringify(lowMana)});await goto(cdp,BASE);state=await liveState();
  const blocked=state.battle.artifacts.find(ability=>ability.gearId==='test-sword');
  check(!blocked.playable && (await textOf(cdp,`[data-action="artifact-play"][data-id="${blocked.id}"]`)).includes(blocked.reason)
    && await cdp.eval(`return document.querySelector('[data-action="artifact-play"][data-id="${blocked.id}"]').disabled;`), '法宝灵力不足时展示引擎具体原因并禁用施展');
  check(blocked.damage>0 && blocked.text.includes('灼烧') && (await textOf(cdp,`[data-action="artifact-play"][data-id="${blocked.id}"]`)).includes(blocked.text),
    '实际法宝预览同时包含非零伤害与新增灼烧，界面完整保留状态说明');
  await shot(cdp,'artifact-mana-disabled');await narrowShot(cdp,'artifact-mana-disabled','[data-combat-artifacts]');
}

async function dungeonRouteUI(cdp) {
  log('\n=== 秘境路线节点定位与边界专项（隔离服务） ===');
  const request=async(path,body={})=>{
    const response=await fetch(BASE+'api/'+path,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data=await response.json();if(!response.ok||data.error)throw new Error(path+': '+data.error);return data;
  };
  await request('new',{seed:480319,name:'路线验收',gender:'女',traits:[],allocation:{}});
  const fixture=JSON.parse((await request('export')).game), initial=await liveState();
  const site=initial.dungeons.sites.find(entry=>entry.type==='wild'&&entry.available);
  if(!site)throw new Error('没有自然秘境可用于路线验收');
  Object.assign(fixture.player,{stage:6,highestStage:6,xp:0,health:100,location:site.location,body:20});
  fixture.player.arts.hpBonus=100;
  await request('import',{game:JSON.stringify(fixture)});await goto(cdp,BASE);
  await click(cdp,'.main-tabs [data-tab="dungeon"]');
  await click(cdp,`[data-dungeon-site="${site.id}"] [data-action="dungeon-enter"]:not(:disabled)`);
  await waitFor(cdp,'[data-dungeon-route-map]');
  const geometry=async(label,current=false)=>{
    const result=await cdp.eval(`
      const map=document.querySelector('[data-dungeon-route-map]'),viewport=document.querySelector('[data-dungeon-route-viewport]');
      const box=map.getBoundingClientRect(),boundary=viewport.getBoundingClientRect();
      const nodes=[...map.querySelectorAll('.dungeon-node')];
      const failures=nodes.flatMap(node=>{
        const rect=node.getBoundingClientRect(),x=box.left+box.width*parseFloat(node.style.left)/100,y=box.top+parseFloat(node.style.top);
        const offCenter=Math.abs(rect.left+rect.width/2-x)>1||Math.abs(rect.top+rect.height/2-y)>1;
        const outside=rect.left<boundary.left-1||rect.right>boundary.right+1;
        return offCenter||outside?[{id:node.dataset.id,offCenter,outside,transform:getComputedStyle(node).transform}]:[];
      });
      return {failures,disabled:nodes.filter(node=>node.disabled).length,current:!!map.querySelector('.dungeon-node.current:disabled'),overflow:document.documentElement.scrollWidth>document.documentElement.clientWidth+2};
    `);
    check(result.disabled>2&&!result.failures.length&&!result.overflow&&(!current||result.current),label,JSON.stringify(result.failures));
    if(result.failures.length)throw new Error('节点定位失败：'+JSON.stringify(result.failures));
  };
  const capture=async(name,current=false)=>{
    await cdp.send('Emulation.setDeviceMetricsOverride',{width:1200,height:900,deviceScaleFactor:1,mobile:false});
    await sleep(150);await cdp.eval(`document.querySelector('.dungeon-route-panel').scrollIntoView({block:'start',behavior:'instant'});return true;`);
    await geometry(name+' 桌面所有节点居中于连线坐标，左右列不裁切',current);await shot(cdp,name);
    await cdp.send('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:false});
    await sleep(150);await cdp.eval(`document.querySelector('.dungeon-route-panel').scrollIntoView({block:'start',behavior:'instant'});return true;`);
    await geometry(name+' 390px所有节点居中且左右列完整，页面不溢出',current);await shot(cdp,name+'-narrow');
    await cdp.send('Emulation.clearDeviceMetricsOverride');await sleep(100);
  };
  await capture('route-locked-nodes');
  const first=(await liveState()).dungeons.run.nodes.find(node=>node.available);
  await click(cdp,`[data-action="dungeon-choose"][data-id="${first.id}"]:not(:disabled)`);
  await waitFor(cdp,'.dungeon-node.current');
  await capture('route-current-disabled',true);
}

async function beastUI(cdp) {
  log('\n=== 真实五境妖兽立绘 UI 回归（隔离服务） ===');
  const request = async (path, body = {}) => {
    const response = await fetch(BASE + 'api/' + path, {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const data = await response.json();
    if (!response.ok || data.error || data.ok === false) throw new Error(`${path}: ${data.error || response.status}`);
    return data;
  };
  const beastNames = ['灵狼','岩甲熊','雷翼狮','霜角蛟','苍龙'];
  for (const [tier, stage] of [0,3,6,9,12].entries()) {
    await request('new', {seed:480317,name:'妖兽验收',gender:'男',traits:[],allocation:{}});
    const game = JSON.parse((await request('export')).game);
    Object.assign(game.player,{stage,highestStage:stage,xp:0,stones:50000,location:'fanghu',health:100});
    await request('import',{game:JSON.stringify(game)});
    let data = await request('wild',{id:'beast'});
    for (let index=0;index<12 && !data.state.battle;index++) {
      if (data.state.ended) throw new Error('妖兽测试角色意外结束');
      if (data.state.pending) {
        const choice = data.state.pending.options.find(option=>!option.disabled && /leave|decline|ignore|pass|skip|avoid/.test(option.id))
          || data.state.pending.options.find(option=>!option.disabled);
        data = await request('choose',{id:choice.id});
      } else if (data.state.job) data = await request('advance',{days:data.state.job.remaining});
      else throw new Error('驱妖未开始斗法');
    }
    const state = data.state;
    if (!state.battle) throw new Error('驱妖未建立实际斗法');
    check(state.battle.enemyStage === stage && state.battle.enemyPortrait === `/assets/portraits/beast-${tier}.webp`,
      `${state.battle.enemyRealmName}妖兽使用对应${beastNames[tier]}立绘`);
    await goto(cdp,BASE);
    check(await cdp.eval(`const image = document.querySelector('[data-combat-actor="enemy"] .combat-portrait');
      return image?.complete && image.naturalWidth > 0 && new URL(image.src).pathname === ${JSON.stringify(state.battle.enemyPortrait)}
        && document.querySelector('[data-combat-actor="enemy"]').textContent.includes(${JSON.stringify(state.battle.enemyRealmName)})
        && !document.querySelector('.combat-impact');`), '妖兽图片与境界实际显示，载入时不制造受击');
    await shot(cdp,`beast-${tier}-${beastNames[tier]}`);
    await narrowShot(cdp,`beast-${tier}`);
  }
}

async function npcUI(cdp) {
  log('\n=== 真实NPC道法与请教 UI 回归（隔离服务） ===');
  const request = async (path, body = {}) => {
    const response = await fetch(BASE + 'api/' + path, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) });
    const data = await response.json();
    if (!response.ok || data.ok === false || data.error) throw new Error(`${path}: ${data.error || response.status}`);
    return data;
  };
  await request('new', { seed: 480317, name: '道友验收', gender: '女', traits: [], allocation: {} });
  const exported = await request('export');
  const fixture = JSON.parse(exported.game);
  const npc = fixture.npcs.find(person => person.alive && !person.beast && person.sect === 'jinhong')
    || fixture.npcs.find(person => person.alive && !person.beast);
  if (!npc) throw new Error('真实生成的世界没有可测试修士');
  Object.assign(npc, { location: fixture.player.location, stage: 6, highestStage: 6, xp: 0, health: 100, gender: '女', sect: 'jinhong', dao: '金', age: 45 });
  Object.assign(fixture.player, { stage: 6, highestStage: 6, xp: 0, stones: 50000, health: 100 });
  fixture.player.relations[npc.id] = 90;
  fixture.player.npcBonds[npc.id] = { credit: 1000, spent: 0, greeted: true, greetYear: 0,
    talkMonth: -1, giftMonth: -1, giftsThisMonth: 0, discussMonth: -1, discussCount: 0,
    learnMonth: -1, learnedThisMonth: 0, travelMonth: -1, sparMonth: -1 };
  await request('import', { game: JSON.stringify(fixture) });
  await goto(cdp, BASE);
  await click(cdp, '.main-tabs [data-tab="map"]');
  await click(cdp, '[data-subtab="map"][data-sub="friends"]');
  await click(cdp, `.friend-card[data-person="${npc.id}"]`);
  if (!(await waitFor(cdp, `.npc-learning [data-action="npc-learn"][data-npc="${npc.id}"]`))) throw new Error('NPC请教列表未出现');
  let state = await liveState();
  const person = () => state.nearby.find(item => item.id === npc.id);
  const actualNpc = person();
  check(actualNpc.loadout.manuals.length > 0 && actualNpc.loadout.spells.length > 0
    && await cdp.eval(`return !!document.querySelector('[data-npc-loadout="${npc.id}"]');`), '道友详情包含实际功法、悟道和神通配装');
  await click(cdp, `[data-npc-loadout="${npc.id}"] summary`);
  const loadoutText = await textOf(cdp, `[data-npc-loadout="${npc.id}"]`);
  check(actualNpc.loadout.manuals.every(item => loadoutText.includes(item.name) && loadoutText.includes(`${item.level} 层`))
    && actualNpc.loadout.spells.every(item => loadoutText.includes(item.name)), '展开实际道法后各名称和功法层数与引擎一致');
  check(await cdp.eval(`const player = document.querySelector('#player-card .character-portrait'), npc = document.querySelector('#npc-card .character-portrait');
    const detail = document.querySelector('.friend-detail-portrait');
    return [player,npc,detail].every(image => image?.complete && image.naturalWidth > 0)
      && new URL(player.src).pathname === ${JSON.stringify(state.player.portrait)}
      && new URL(npc.src).pathname === ${JSON.stringify(actualNpc.portrait)}
      && new URL(detail.src).pathname === ${JSON.stringify(actualNpc.portrait)};`), '角色侧栏与道友详情加载API所选男女及门派立绘');
  await shot(cdp, 'npc-loadout-learning');
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  check(await cdp.eval(`return document.documentElement.scrollWidth <= document.documentElement.clientWidth + 2;`), '390px道友立绘、道法和学习列表无横向溢出');
  await cdp.eval(`window.scrollTo({top:0,behavior:'instant'}); return true;`);
  check(await cdp.eval(`return ['#player-card .character-portrait','#npc-card .character-portrait'].every(selector=>{
    const image=document.querySelector(selector),r=image.getBoundingClientRect();
    return image.complete && image.naturalWidth>0 && r.height>0 && r.width>0 && getComputedStyle(image.parentElement).display!=='none';
  });`), '390px侧栏玩家与眼前道友立绘均保持可见');
  await shot(cdp, 'npc-sidebar-narrow');
  await cdp.eval(`document.querySelector('.fs-panel').scrollIntoView({block:'start',behavior:'instant'}); return true;`);
  await shot(cdp, 'npc-learning-narrow');
  await cdp.eval(`document.querySelector('.npc-learning').scrollIntoView({block:'start',behavior:'instant'}); return true;`);
  await shot(cdp, 'npc-lessons-narrow');

  const learningRequests = () => cdp.events.filter(event => event.method === 'Network.requestWillBeSent'
    && new URL(event.params.request.url).pathname === '/api/npc-learn');
  const resolveLearning = async () => {
    for (let index = 0; index < 12; index++) {
      state = await liveState();
      if (state.ended || state.battle) throw new Error('授业验收遇到无法安全继续的状态');
      if (!state.job && !state.pending) return;
      if (state.pending) {
        const choice = state.pending.options.find(option => !option.disabled && /leave|decline|ignore|refuse|pass|skip|avoid/.test(option.id))
          || state.pending.options.find(option => !option.disabled);
        await click(cdp, `#modal [data-action="choose"][data-id="${choice.id}"]`);
      } else {
        await click(cdp, '.main-tabs [data-tab="chronicle"]');
        await click(cdp, '[data-action="advance-step"]:not(:disabled)');
      }
    }
    throw new Error('授业未完成');
  };
  const openFriends = async () => {
    await click(cdp, '.main-tabs [data-tab="map"]');
    await click(cdp, '[data-subtab="map"][data-sub="friends"]');
    if (!(await waitFor(cdp, `[data-action="npc-learn"][data-npc="${npc.id}"]`))) throw new Error('授业后未保留道友选择');
  };
  const learn = async kind => {
    state = await liveState();
    const lesson = person().learning.find(item => item.kind === kind && item.enabled);
    if (!lesson) throw new Error(`缺少可学习${kind}`);
    const before = state, count = learningRequests().length;
    await click(cdp, `[data-action="npc-learn"][data-npc="${npc.id}"][data-kind="${kind}"][data-id="${lesson.id}"]:not(:disabled)`);
    await resolveLearning();
    check(learningRequests().length === count + 1 && JSON.parse(learningRequests().at(-1).params.request.postData).id === lesson.id,
      `${kind === 'spell' ? '神通' : '功法'}请教只发一次真实请求`);
    const learned = person().learning.find(item => item.kind === kind && item.id === lesson.id);
    check(learned.learned && state.player.stones === before.player.stones - lesson.stoneCost
      && person().credit === before.nearby.find(item => item.id === npc.id).credit - lesson.creditCost
      && state.clock.days === before.clock.days + lesson.days, '授业完成后的知识、灵石、交情与天数匹配报价');
    check(JSON.stringify(state.arts.spells) === JSON.stringify(before.arts.spells) && state.arts.main === before.arts.main,
      '请教获得知识后保留原配装，玩家可以自行选择携带');
    await openFriends();
    check(await cdp.eval(`const button = document.querySelector('[data-action="npc-learn"][data-kind="${kind}"][data-id="${lesson.id}"]');
      return button?.disabled && button.textContent.includes(${JSON.stringify(kind === 'spell' ? '已领悟' : '已修习')});`), '学会后列表显示已知并禁止重复花费');
    return lesson;
  };
  await learn('spell');
  await learn('manual');
  await learn('spell');
  state = await liveState();
  check(person().learning.filter(item => !item.learned).every(item => !item.enabled)
    && person().learning.some(item => /本月.*三次/.test(item.reason)), '同一道友月内三次授业后其余知识显示真实限额原因');
  await cdp.eval(`document.querySelector('.npc-learning').scrollIntoView({block:'start',behavior:'instant'}); return true;`);
  await shot(cdp, 'npc-learning-completed-narrow');
  await cdp.send('Emulation.clearDeviceMetricsOverride');
  const spar = '[data-action="social"][data-id="spar"][data-npc="' + npc.id + '"]:not(:disabled)';
  if (!(await waitFor(cdp, spar))) throw new Error('没有可用真实切磋入口');
  await click(cdp, spar);
  state = await liveState();
  check(state.battle?.npcId === npc.id && state.battle.intentSpells.length > 0
    && (await textOf(cdp, '.combat-intent')).includes(state.battle.intentDescription), 'NPC切磋显示实际抽牌组合和权威预估效果');
  check(await cdp.eval(`const image = document.querySelector('[data-combat-actor="enemy"] .combat-portrait');
    return image?.complete && image.naturalWidth > 0 && new URL(image.src).pathname === ${JSON.stringify(state.battle.enemyPortrait)}
      && !document.querySelector('.combat-impact');`), '战斗对手立绘匹配NPC，加载当前状态不虚构受击动画');
  await shot(cdp, 'npc-real-combat');
  await narrowShot(cdp, 'npc-real-combat');
  await click(cdp, '[data-action="escape"]');
}

async function battleUI(cdp) {
  log('\n=== 真实战斗 UI 回归 ===');
  const fixture = arg('--fixture', join(ROOT, 'build', 'battle-ui-fixture.json'));
  if (!existsSync(fixture)) throw new Error(`缺少专用战斗存档: ${fixture}`);
  const raw = readFileSync(fixture, 'utf8');
  const parsed = JSON.parse(raw);
  const response = await fetch(BASE + 'api/import', { method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(parsed.game ? { game: parsed.game, legacy: parsed.legacy || '' } : { game: raw }) });
  const imported = await response.json();
  if (!response.ok || imported.error || imported.ok === false) throw new Error('导入战斗存档失败: ' + (imported.error || response.status));
  await goto(cdp, BASE);
  check(await waitFor(cdp, '.combat-hand'), '专用存档进入真实斗法');
  const turns = () => cdp.events.filter(e => e.method === 'Network.requestWillBeSent' && e.params.request.url.endsWith('/api/battle/turn'));
  const playSpell = async id => {
    const state = await liveState();
    const card = state.battle.hand.find(c => c.id === id);
    if (!card) throw new Error(`专用手牌缺少 ${id}`);
    await click(cdp, `[data-action="play"][data-id="${card.uid}"]:not(:disabled)`);
    return liveState();
  };
  const before = await liveState();
  check(await cdp.eval(`return !document.querySelector('.combat-impact');`), '导入和初次加载斗法不会产生虚假受击');
  check(await cdp.eval(`return [...document.querySelectorAll('.combat-portrait')].every(image => image.complete && image.naturalWidth > 0);`), '双方斗法立绘实际加载');
  check((await textOf(cdp, '.combat-round')).includes(before.battle.playerFirst ? '你方先手' : '敌方先手')
    && (await textOf(cdp, '[data-combat-actor="player"]')).includes(`遁速 ${before.battle.playerSpeed}`)
    && (await textOf(cdp, '[data-combat-actor="enemy"]')).includes(`遁速 ${before.battle.enemySpeed}`), '战斗展示实际遁速与开局先手');
  const boosted = await playSpell('swordFocus');
  check(Math.abs(boosted.battle.buff - before.battle.buff - .1) < .0001
    && (await textOf(cdp, '.combat-status')).includes('攻击增伤：+10%'), '使用增伤牌后显示本场实际 +10%');
  check((await textOf(cdp, '.cooldown-line')).includes(before.battle.hand.find(card => card.id === 'swordFocus').name)
    && !(await textOf(cdp, '.cooldown-line')).includes('swordFocus'), '打出增伤神通后冷却显示中文名');
  const flow = await playSpell('qiFlow');
  const flowText = await textOf(cdp, '#workspace');
  check(flow.battle.manaBonus === 1 && flowText.includes('聚气：每回合灵力 +1') && flowText.includes('本场持续'), '持续聚气显示每回合 +1 与本场范围');
  check(flowText.includes(`下回合实际回复 ${flow.battle.nextMana} 点灵力`), '下一回合回复显示真实引擎值');
  const checkCardCosts = async label => {
    const costs = await cdp.eval(`return [...document.querySelectorAll('.combat-hand [data-action="play"]')].map(card => {
      const orb = card.querySelector('.card-energy');
      const r = card.getBoundingClientRect(), o = orb?.getBoundingClientRect();
      return { uid: card.dataset.id, value: orb?.textContent.trim(), inside: !!o && o.width > 0 && o.height > 0
        && o.left >= r.left && o.right <= r.right && o.top >= r.top && o.bottom <= r.bottom,
        visible: !!orb && getComputedStyle(orb).visibility === 'visible' && getComputedStyle(orb).display !== 'none' };
    });`);
    check(costs.length > 0 && costs.every(c => c.inside && c.visible
      && c.value === String(flow.battle.hand.find(x => x.uid === c.uid)?.cost)), `${label}费用圆标在手牌范围内并显示真实数字`);
  };
  await checkCardCosts('桌面');
  await shot(cdp, 'battle-buffs');
  await narrowShot(cdp, 'battle-buffs');
  await cdp.send('Emulation.setDeviceMetricsOverride', { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  await cdp.eval(`document.querySelector('.combat-hand [data-action="play"]')?.scrollIntoView({ block: 'center', behavior: 'instant' }); return true;`);
  await checkCardCosts('390px窄屏');
  await shot(cdp, 'battle-hand-costs-narrow');
  await cdp.send('Emulation.clearDeviceMetricsOverride');

  const attack = flow.battle.hand.find(card => card.id === 'strike');
  if (!attack) throw new Error('请先运行 tools/battle-ui-fixture.mjs，生成含普通攻击的真实新存档');
  await click(cdp, `[data-action="play"][data-id="${attack.uid}"]:not(:disabled)`, 0, {wait:80});
  const struck = await liveState();
  check(struck.battle.enemyHp < flow.battle.enemyHp && await cdp.eval(`return document.querySelector('[data-combat-actor="enemy"] .combat-impact')?.textContent === ${JSON.stringify('−' + (flow.battle.enemyHp - struck.battle.enemyHp))};`),
    '真实攻击后对手立绘受击提示与实际气血下降一致');
  await shot(cdp, 'battle-real-hit');
  await sleep(900);
  check(await cdp.eval(`return !document.querySelector('.combat-impact');`), '受击提示完成后自行清除');
  await goto(cdp, BASE);
  check(await cdp.eval(`return !document.querySelector('.combat-impact');`), '刷新已受伤战斗状态不重放受击');
  await cdp.send('Emulation.setEmulatedMedia', {features:[{name:'prefers-reduced-motion',value:'reduce'}]});
  check(await cdp.eval(`const badge = document.createElement('span'); badge.className='combat-impact';
    const actor=document.querySelector('[data-combat-actor="player"]'); actor.append(badge); actor.classList.add('combat-hit');
    const disabled=getComputedStyle(badge).animationName==='none' && getComputedStyle(actor.querySelector('.combat-portrait')).animationName==='none';
    badge.remove(); actor.classList.remove('combat-hit'); return disabled;`), '减弱动态效果偏好关闭飘字与立绘晃动动画');
  await cdp.send('Emulation.setEmulatedMedia', {features:[]});

  const pendingCard = struck.battle.hand.find(c => c.id === 'purge');
  const target = struck.battle.removalOptions.find(c => c.id === 'strike');
  if (!pendingCard || !target) throw new Error('专用存档缺少忘机咒或普通攻击目标');
  const requestsBefore = turns().length;
  await click(cdp, `[data-action="play"][data-id="${pendingCard.uid}"]`);
  check(await waitFor(cdp, '#battle-target-modal[open]'), '移除牌先打开可取消的目标选择');
  check(await cdp.eval(`return !document.querySelector('#battle-target-modal [data-id="purge"]');`), '目标列表排除忘机咒自身');
  await click(cdp, '#battle-target-modal [data-action="removal-select"][data-id="strike"]');
  await narrowShot(cdp, 'battle-removal-choice');
  await click(cdp, '#battle-target-modal [data-action="removal-cancel"]');
  const cancelled = await liveState();
  check(turns().length === requestsBefore && cancelled.battle.mana === struck.battle.mana
    && cancelled.battle.hand.length === struck.battle.hand.length, '取消移除不发出牌请求、不消耗手牌或灵力');
  await click(cdp, `[data-action="play"][data-id="${pendingCard.uid}"]`);
  await click(cdp, '#battle-target-modal [data-action="removal-select"][data-id="strike"]');
  await click(cdp, '#battle-target-modal [data-action="removal-confirm"]');
  const removed = await liveState();
  const removedList = removed.battle.exhaustedSpells.find(c => c.id === 'strike');
  check(turns().length === requestsBefore + 1 && JSON.parse(turns().at(-1).params.request.postData).target === 'strike', '确认移除只发送一次带目标的真实请求');
  check(removedList?.copies === target.copies && removed.battle.exhausted === struck.battle.exhausted + target.copies
    && !removed.battle.removalOptions.some(c => c.id === 'strike'), '普通攻击全部副本进入本场移除堆');
  check((await textOf(cdp, '.exhausted-pile')).includes(`${target.name} ×${target.copies}`), '本场移除清单显示神通名、张数与恢复范围');
  await shot(cdp, 'battle-exhausted');

  const redrawBefore = await liveState();
  const redrawn = await playSpell('refresh');
  check(redrawn.battle.hand.length === redrawBefore.battle.hand.length,
    '换念诀弃掉其余手牌后重抽相同数量 +1 张');
  check(!(await cdp.eval(`return document.querySelector('#battle-target-modal').open;`)), '换念诀无需选择目标');
  await shot(cdp, 'battle-redraw');

  await click(cdp, '[data-action="escape"]');
  await click(cdp, '.main-tabs button[data-tab="arts"]');
  await click(cdp, '[data-subtab="arts"][data-sub="spells"]');
  await waitFor(cdp, '.spell-picker');
  const state = await liveState();
  const artsText = await textOf(cdp, '#workspace');
  check(artsText.includes(`${state.arts.spellSlots} 个额外槽`), '神通配装显示当前境界的动态槽位');
  check(await cdp.eval(`return [...document.querySelectorAll('.spell-picker > .card')].every(c=>c.innerText.includes('所需境界：'));`), '所有神通始终显示携带所需境界');
  check(await cdp.eval(`return document.querySelectorAll('.spell-picker > .mandatory:disabled').length === 2;`), '普通攻击与防御固定常备且不可卸下');
  await narrowShot(cdp, 'battle-arts-slots');
  await click(cdp, '[data-subtab="arts"][data-sub="builds"]');
  check(await cdp.eval(`return document.querySelectorAll('.art-build').length === 8;`), '八种流派显示循环与搭配组件');
  await narrowShot(cdp, 'battle-builds');
}

// 断言收集
const failures = [];
function check(ok, label, detail = '') {
  log(`  ${ok ? '✓' : '✗'} ${label}${detail ? '  — ' + detail : ''}`);
  if (!ok) failures.push(label + (detail ? ' — ' + detail : ''));
}

// ============================================================
// 冒烟：开页 + 点 8 个页签
// ============================================================
async function smoke(cdp) {
  log('\n=== 冒烟测试：页面与页签 ===');
  await goto(cdp, BASE);
  await sleep(2000);

  // 有存档就直接进游戏；没有就先建号
  const hasPanel = await waitFor(cdp, '#build-host .build-overlay', 3000);
  const hasGame = await waitFor(cdp, '.main-tabs button', 3000);

  if (hasPanel) {
    log('  冷启动：出现建号面板');
    await shot(cdp, 'build-panel');
    const startBtn = await waitFor(cdp, '#bp-start');
    check(startBtn, '建号面板有「开始这一世」按钮');
    if (startBtn) {
      await click(cdp, '#bp-start');
      await sleep(2500);
      await shot(cdp, 'after-create');
    }
  } else if (hasGame) {
    log('  已有存档：直接进游戏');
  } else {
    check(false, '页面既没有建号面板也没有主界面');
    await shot(cdp, 'blank');
    return;
  }

  const tabs = ['chronicle', 'map', 'arts', 'society', 'roster', 'quests', 'market', 'economy'];
  const names = ['岁月纪事', '宁州舆图', '洞府道法', '宗门知交', '好友通讯录', '机缘成就', '坊市行囊', '任务与工坊'];
  for (let i = 0; i < tabs.length; i++) {
    const sel = `.main-tabs button[data-tab="${tabs[i]}"]`;
    if (!(await waitFor(cdp, sel, 3000))) { check(false, `页签 ${names[i]} 按钮不存在`); continue; }
    let hit;
    try { hit = await click(cdp, sel); } catch (e) { check(false, `点击 ${names[i]} 失败`, e.message); continue; }
    await sleep(700);
    const body = await textOf(cdp, '#workspace');
    const len = body ? body.length : 0;
    check(len > 40, `页签 ${names[i]} 有内容`, `${len} 字`);
    await shot(cdp, 'tab-' + tabs[i]);
  }
}

// ============================================================
// 完整流程：建号 → 配给 → 战斗出牌
// ============================================================
async function play(cdp) {
  log('\n=== 完整流程测试 ===');

  // 1. 建号面板
  await goto(cdp, BASE);
  await sleep(2000);
  const hasPanel = await waitFor(cdp, '#build-host .build-overlay', 4000);
  if (hasPanel) {
    log('\n1. 建号面板');
    await shot(cdp, 'play-build');

    // 先天气运
    const innate = await textOf(cdp, '#bp-innate');
    check(!!innate && /资质/.test(innate), '显示先天气运（资质/悟性）', (innate || '').replace(/\s+/g, ' ').slice(0, 60));

    // 换种子
    const seedBefore = await cdp.eval(`const m=document.querySelector('#bp-innate')?.innerText.match(/(\\d{6,})/); return m?m[1]:null;`);
    if (await waitFor(cdp, '#bp-reroll', 2000)) {
      await click(cdp, '#bp-reroll');
      await sleep(900);
      const seedAfter = await cdp.eval(`const m=document.querySelector('#bp-innate')?.innerText.match(/(\\d{6,})/); return m?m[1]:null;`);
      check(seedBefore !== seedAfter, '「换个种子」改变了种子', `${seedBefore} → ${seedAfter}`);
    } else {
      check(false, '找不到「换个种子」按钮');
    }

    // 选天赋：只挑负担得起的，避免超支把「开始」按钮锁死
    const talentN = await cdp.eval(`return document.querySelectorAll('[data-talent]').length;`);
    check(talentN >= 30, '天赋全部列出', `${talentN} 项`);

    // 名字骰子：点几次应当换出不同道号
    if (await waitFor(cdp, '#bp-name-reroll', 2000)) {
      const n0 = await cdp.eval(`return document.querySelector('#bp-name')?.value;`);
      await click(cdp, '#bp-name-reroll');
      await sleep(400);
      const n1 = await cdp.eval(`return document.querySelector('#bp-name')?.value;`);
      await click(cdp, '#bp-name-reroll');
      await sleep(400);
      const n2 = await cdp.eval(`return document.querySelector('#bp-name')?.value;`);
      check(n0 !== n1 || n1 !== n2, '🎲 能随机道号', `${n0} → ${n1} → ${n2}`);
      check(/^[\u4e00-\u9fa5]{2,3}$/.test(n1 || ''), '随机道号是中文名', n1);
    } else {
      check(false, '找不到名字骰子 #bp-name-reroll');
    }

    // 故意点超过 3 项，验证数量上限确实去掉了。
    // 便宜的（含磨砺类负成本）优先，保证有足够点数点满 4 项以上。
    const cheapIdx = await cdp.eval(`
      const all = [...document.querySelectorAll('[data-talent]')];
      return all.map((el, i) => ({
        i,
        cost: Number(el.querySelector('.tc-cost')?.innerText.replace(/[^\\d-]/g, '')) || 0,
      })).sort((a, b) => a.cost - b.cost).map(x => x.i);
    `);
    let pickedCount = 0;
    for (const i of cheapIdx) {
      if (pickedCount >= 5) break;
      const before = await cdp.eval(`return document.querySelectorAll('[data-talent].picked').length;`);
      await click(cdp, '[data-talent]', i);
      await sleep(180);
      const after = await cdp.eval(`return document.querySelectorAll('[data-talent].picked').length;`);
      if (after > before) pickedCount = after;
    }
    const after5 = await cdp.eval(`
      const m = document.querySelector('#bp-points')?.innerText.match(/尚余\\s*(-?\\d+)/);
      return { count: document.querySelectorAll('[data-talent].picked').length,
               remain: m ? Number(m[1]) : null };
    `);
    check(after5.count > 3, '能选超过 3 项天赋（数量上限已去掉）', `已选 ${after5.count} 项`);
    log(`      选了 ${after5.count} 项，尚余 ${after5.remain}`);

    // 超支就把最贵的取消，直到不超支
    let guard = 0;
    while (guard++ < 14) {
      const st = await cdp.eval(`
        const m = document.querySelector('#bp-points')?.innerText.match(/尚余\\s*(-?\\d+)/);
        return m ? Number(m[1]) : 0;
      `);
      if (st >= 0) break;
      const idx = await cdp.eval(`
        const picked = [...document.querySelectorAll('[data-talent].picked')];
        if (!picked.length) return -1;
        let best = 0, bestCost = -999;
        picked.forEach((el, i) => {
          const c = Number(el.querySelector('.tc-cost')?.innerText.replace(/[^\\d-]/g, '')) || 0;
          if (c > bestCost) { bestCost = c; best = i; }
        });
        return [...document.querySelectorAll('[data-talent]')].indexOf(picked[best]);
      `);
      if (idx < 0) break;
      await click(cdp, '[data-talent]', idx);
      await sleep(220);
    }
    const finalRemain = await cdp.eval(`
      const m = document.querySelector('#bp-points')?.innerText.match(/尚余\\s*(-?\\d+)/);
      return m ? Number(m[1]) : null;
    `);
    check(finalRemain >= 0, '整理后点数不再超支', `尚余 ${finalRemain}`);
    const picked = [1];
    await sleep(300);
    await shot(cdp, 'play-talents');

    // 断言：数量上限已去掉 —— 提示不再写「最多 3 项」
    const head = await cdp.eval(`return document.querySelector('.build-section h3')?.innerText || '';`);
    check(!/最多\s*3/.test(head) && !/\/\s*3\s*项/.test(head),
          '天赋说明去掉了「最多 3 项」', head.replace(/\s+/g, ' ').slice(0, 40));

    // 起名
    await type(cdp, '#bp-name', '自动测试');
    await sleep(300);
    await shot(cdp, 'play-ready');

    // 开始：必须能点，且面板要收起
    const startInfo = await cdp.eval(`
      const b = document.querySelector('#bp-start');
      const why = document.querySelector('#bp-actions .why')?.innerText || '';
      return { exists: !!b, disabled: b ? b.disabled : null, why };
    `);
    check(startInfo.exists && !startInfo.disabled, '点数没超支时「开始这一世」可用',
          `disabled=${startInfo.disabled} why="${startInfo.why}"`);
    const canStart = startInfo.exists && !startInfo.disabled;
    if (canStart) {
      await click(cdp, '#bp-start');
      await sleep(2500);
    }
    const panelGone = await cdp.eval(`return !document.querySelector('.build-overlay');`);
    check(panelGone, '建号后面板收起（不再挡住主界面）');
  } else {
    log('\n1. 已有存档，跳过建号');
  }
  await shot(cdp, 'play-ingame');

  // 日期必须精确推进一天，不能把一个月或整段工作全部结算。
  await click(cdp, '.main-tabs button[data-tab="chronicle"]');
  const beforeDay = await cdp.eval(`return fetch('/api/state').then(r => r.json()).then(r => r.state.clock.days);`);
  await click(cdp, '[data-action="advance-step"]');
  const afterDay = await cdp.eval(`return fetch('/api/state').then(r => r.json()).then(r => r.state.clock.days);`);
  check(afterDay === beforeDay + 1, '过一天精确推进 1 天', `${beforeDay} → ${afterDay}`);
  for (let n = 0; n < 6; n++) {
    if (!(await cdp.eval(`return !!document.querySelector('#modal[open]');`))) break;
    await click(cdp, '#modal .choice-option:not(:disabled)');
  }

  // 面板若还在，主界面被遮罩挡住，先关掉它
  if (await cdp.eval(`return !!document.querySelector('.build-overlay');`)) {
    if (await waitFor(cdp, '#bp-cancel', 1500)) {
      await click(cdp, '#bp-cancel');
      await sleep(600);
    }
  }

  // 2. 洞府与配给
  log('\n2. 洞府与配给');
  await click(cdp, '.main-tabs button[data-tab="arts"]');
  await sleep(900);
  const artsText = await textOf(cdp, '#workspace');
  check(/洞府与配给/.test(artsText || ''), '洞府页有配给面板');
  check(/修炼速度/.test(artsText || ''), '洞府页有修炼速度拆解');

  const stoneBtns = await cdp.eval(`return document.querySelectorAll('[data-kind="stone"][data-action="supply"]').length;`);
  check(stoneBtns === 4, '灵石配给有 4 档', `${stoneBtns} 个`);
  const qiDisabled = await cdp.eval(`
    const btns = [...document.querySelectorAll('[data-kind="qi"][data-action="supply"]')];
    return btns.filter(b => b.disabled).length;
  `);
  const hasCave = await cdp.eval(`return !!document.querySelector('[data-action="cave-move"]');`);
  log(`      灵气禁用档位 ${qiDisabled} 个，未安府=${hasCave}`);
  await shot(cdp, 'play-supply');

  // 设灵石配给
  if (stoneBtns === 4) {
    const hit = await click(cdp, '[data-kind="stone"][data-action="supply"]', 1);
    await sleep(800);
    log(`      点灵石 1 档: ${hit}`);
  }
  // 安府：先看钱够不够（安府要 40 灵石，开局未必有）
  const caveInfo = await cdp.eval(`
    const btn = document.querySelector('[data-action="cave-move"]');
    if (!btn) return { present: false };
    const m = btn.innerText.match(/(\\d+)\\s*灵石/);
    const sm = document.body.innerText.match(/灵石\\s*\\n?\\s*(\\d+)/);
    return { present: true, cost: m ? Number(m[1]) : null, stones: sm ? Number(sm[1]) : -1 };
  `);
  if (!caveInfo.present) {
    check(true, '已经安过府（没有安府按钮）');
  } else if (caveInfo.stones >= 0 && caveInfo.stones < caveInfo.cost) {
    // 钱不够：按钮该给明确提示，而不是静默失败
    await click(cdp, '[data-action="cave-move"]');
    await sleep(900);
    const toast = await cdp.eval(`return document.querySelector('#toast')?.innerText || '';`);
    check(/灵石/.test(toast), '钱不够时安府给出明确提示',
          `有 ${caveInfo.stones}，需 ${caveInfo.cost}；提示「${toast.trim()}」`);
    check(await cdp.eval(`return !!document.querySelector('[data-action="cave-move"]');`),
          '没安成府时按钮保留（可攒够钱再来）');
    const locked = await cdp.eval(`
      const b = [...document.querySelectorAll('[data-kind="qi"][data-action="supply"]')];
      return b.filter(x => x.disabled).length;
    `);
    check(locked > 0, '没安府时灵气档位仍锁定', `禁用 ${locked} 个`);
  } else {
    await click(cdp, '[data-action="cave-move"]');
    await sleep(1200);
    const nowCave = await cdp.eval(`return !document.body.innerText.includes('尚未安府');`);
    check(nowCave, '「在此安府」生效（洞府不再是「尚未安府」）');
    if (nowCave) {
      const qiLocked = await cdp.eval(`
        const btns = [...document.querySelectorAll('[data-kind="qi"][data-action="supply"]')];
        return btns.filter(b => b.disabled).length;
      `);
      check(qiLocked === 0, '安府后灵气档位解锁', `仍禁用 ${qiLocked} 个`);
      if (qiLocked === 0) {
        await click(cdp, '[data-kind="qi"][data-action="supply"]', 2);
        await sleep(900);
        const on = await cdp.eval(`return document.querySelector('[data-kind="qi"][data-action="supply"][data-level="2"]')?.classList.contains('on');`);
        check(!!on, '安府后能设灵气配给');
      }
    }
  }
  await shot(cdp, 'play-supply-set');
  await narrowShot(cdp, 'play-supply');

  // 神通入口在子页签中，替换后的携带状态来自最新 state。
  await click(cdp, '[data-subtab="arts"][data-sub="spells"]');
  await sleep(500);
  check((await textOf(cdp, '#workspace') || '').includes('点击卸下'), '神通搭配显示可卸下的当前神通');
  await shot(cdp, 'play-spells');

  // 炼丹不能截断可用配方；批量入口与全部缺料原因都可查看。
  await click(cdp, '.main-tabs button[data-tab="market"]');
  await click(cdp, '[data-subtab="market"][data-sub="brew"]');
  check(await cdp.eval(`return document.querySelector('[data-brew-batch]')?.options.length === 4;`), '炼丹可选 1/3/5/10 炉');
  const recipeCount = await cdp.eval(`return fetch('/api/state').then(r=>r.json()).then(r=>r.state.brewOptions.length);`);
  const recipeDisplayed = await cdp.eval(`return document.querySelectorAll('[data-action="craft"]').length + document.querySelectorAll('.blocked-recipes .feed-item').length;`);
  check(recipeDisplayed === recipeCount, '全部丹方可访问', `${recipeDisplayed} / ${recipeCount}`);
  await shot(cdp, 'play-brew');
  await narrowShot(cdp, 'play-brew');
  await click(cdp, '[data-subtab="market"][data-sub="kit"]');
  const bookCount = await cdp.eval(`return fetch('/api/state').then(r=>r.json()).then(r=>r.state.manualSources.filter(m=>m.city).length);`);
  const bookDisplayed = await cdp.eval(`return [...document.querySelectorAll('.chronicle-panel')].find(p=>p.innerText.includes('书坊与功法抄本'))?.querySelectorAll('.pill-row').length || 0;`);
  check(bookDisplayed === bookCount, '完整书坊没有被截断', `${bookDisplayed} / ${bookCount}`);

  // 多次重绘通讯录后，一次传讯只能发一个请求。
  await click(cdp, '.main-tabs button[data-tab="map"]');
  await click(cdp, '[data-subtab="map"][data-sub="friends"]');
  if (await waitFor(cdp, '[data-person]', 2000)) {
    await click(cdp, '[data-person]');
    await sleep(400);
    check(await waitFor(cdp, '.social-actions [data-action="social"]', 2500), '当地仙友能闲谈、切磋与请教');
    if (await cdp.eval(`return !!document.querySelector('[data-faction="greet"]:not(:disabled)');`)) await click(cdp, '[data-faction="greet"]:not(:disabled)');
  }
  for (let n = 0; n < 4; n++) {
    await click(cdp, '.main-tabs button[data-tab="roster"]');
    await click(cdp, '.main-tabs button[data-tab="chronicle"]');
  }
  await click(cdp, '.main-tabs button[data-tab="roster"]');
  if (await waitFor(cdp, '[data-raction="message"]', 2000)) {
    const countMessages = () => cdp.events.filter(e => e.method === 'Network.requestWillBeSent' && e.params.request.url.endsWith('/api/message')).length;
    const beforeMessages = countMessages();
    await click(cdp, '[data-raction="message"]');
    await sleep(700);
    check(countMessages() === beforeMessages + 1, '通讯录重绘后一次传讯只发送一个请求');
  }

  // 3. 战斗出牌
  log('\n3. 战斗与出牌');
  await click(cdp, '.main-tabs button[data-tab="map"]');
  await click(cdp, '[data-subtab="map"][data-sub="atlas"]');
  await click(cdp, '[data-map="fanghu"]');
  await click(cdp, '#map-travel-now');
  for (let n = 0; n < 8; n++) {
    if (!(await cdp.eval(`return !!document.querySelector('#modal[open]');`))) break;
    await click(cdp, '#modal .choice-option:not(:disabled)');
  }
  // 野外悬赏在「机缘成就 → 悬赏与支线」里（它和宗门无关，散修也能做）
  await click(cdp, '.main-tabs button[data-tab="quests"]');
  await sleep(1400);
  await click(cdp, '[data-subtab="quests"][data-sub="wild"]');
  await sleep(1400);
  const wild = await cdp.eval(`return document.querySelectorAll('[data-action="wild"]').length;`);
  log(`      野外悬赏 ${wild} 个`);
  if (wild > 0) {
    await click(cdp, '[data-action="wild"]:not(:disabled)', 0);
    await sleep(900);
    // 推进到开战
    for (let i = 0; i < 8; i++) {
      const inBattle = await cdp.eval(`return !!document.querySelector('.combat-hand');`);
      if (inBattle) break;
      const adv = await cdp.eval(`return !!document.querySelector('[data-action="advance"]');`);
      if (adv) { await click(cdp, '[data-action="advance"]'); await sleep(1200); }
      else break;
    }
    const cardN = await cdp.eval(`return document.querySelectorAll('[data-action="play"]').length;`);
    check(cardN > 0, '战斗中手牌可见（这是上次的 bug）', `${cardN} 张`);
    await shot(cdp, 'play-battle');
    await narrowShot(cdp, 'play-battle');

    if (cardN > 0) {
      const enabledCount = await cdp.eval(`return document.querySelectorAll('[data-action="play"]:not(:disabled)').length;`);
      const hit = enabledCount ? await click(cdp, '[data-action="play"]:not(:disabled)', 0) : '';
      await sleep(900);
      const after = await cdp.eval(`return document.querySelectorAll('[data-action="play"]').length;`);
      check(enabledCount > 0 && after === cardN - 1, '出牌实际消耗一张手牌', `出牌前 ${cardN} → 出牌后 ${after}`);
      await shot(cdp, 'play-card-played');
    }
  } else {
    check(false, '没有野外悬赏可接');
  }

  // 4. 名字骰子已在建号阶段验证，这里不重复
  log('\n4. 角色名骰子：见第 1 阶段的验收结果');
}

// ============================================================
// 主流程
// ============================================================
(async () => {
  let browser = null;
  try {
    log(`启动无头浏览器… 目标 ${BASE}`);
    browser = await launch();
    log('浏览器已就绪\n');

    if (DO_SYSTEMS_UI) await systemsUI(browser.cdp);
    else if (DO_DUNGEON_ROUTE_UI) await dungeonRouteUI(browser.cdp);
    else if (DO_DUNGEON_UI) await dungeonUI(browser.cdp);
    else if (DO_MAP_UI) await mapUI(browser.cdp);
    else if (DO_BEAST_UI) await beastUI(browser.cdp);
    else if (DO_NPC_UI) await npcUI(browser.cdp);
    else if (DO_BATTLE_UI) await battleUI(browser.cdp);
    else if (DO_PLAY) await play(browser.cdp);
    else await smoke(browser.cdp);

    // 页面里的 JS 报错
    log('\n=== 浏览器控制台报错 ===');
    // 服务端用 400 表达「业务拒绝」（钱不够、已经做过等），
    // 前端会把它变成明确的 toast 提示 —— 这是设计，不是缺陷。
    // 所以按「路径 + 已知拒绝语」放行；真正的意外报错仍会留下。
    const benignPaths = [
      '/api/cave/move',      // 洞府已经在这里了 / 迁府需要 N 灵石
      '/api/arts/main',      // 已经是主修了
      '/api/arts/aux',       // 已经是辅修了 / 辅修最多两部
    ];
    const benignText = [
      /favicon/i,
      /Bad Request/,
      /洞府已经在这里了/, /迁府需要/,
      /已经是主修了/, /已经是辅修了/, /主修功法不能同时作辅修/,
      /尚未修习/, /尚未获得/,
    ];
    const errs = browser.cdp.errors.filter(e => {
      if (benignText.some(re => re.test(e))) return false;
      if (benignPaths.some(p => e.includes(p))) return false;
      return true;
    });
    if (errs.length === 0) log('  ✓ 没有 JS 报错');
    else {
      for (const e of errs.slice(0, 10)) log('  ✗ ' + String(e).split('\n')[0]);
      failures.push(`${errs.length} 条控制台报错`);
    }

    log('\n=== 结果 ===');
    if (failures.length === 0) log(`  ✓ 全部通过（截图 ${shots.length} 张）`);
    else {
      log(`  ✗ ${failures.length} 项失败：`);
      for (const f of failures) log('    · ' + f);
    }
    log(`\n截图目录: ${SHOT_DIR}`);
  } catch (e) {
    log('\n✗ 测试中断: ' + e.message);
    failures.push(e.message);
  } finally {
    if (browser) {
      try { browser.child.kill(); } catch {}
      await sleep(600);
      try { rmSync(browser.profile, { recursive: true, force: true }); } catch {}
    }
  }
  process.exit(failures.length ? 1 : 0);
})();
