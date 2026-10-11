// Real-browser forge regression. Mutates ONLY isolated 18106 / 18110 game servers.
// node tools/forge-ui-test.mjs --port 18110
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { join, dirname, resolve, relative, isAbsolute } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const argv = process.argv.slice(2);
const arg = (key, fallback) => argv.includes(key) ? argv[argv.indexOf(key) + 1] : fallback;
const PORT = arg('--port', '18110');
if (!['18106', '18110'].includes(PORT)) throw new Error('炼器验收只允许隔离端口 18106 或 18110。');
const BASE = `http://127.0.0.1:${PORT}/`;
const SHOTS = join(ROOT, 'build', 'shots', 'forge-ui');
const BROWSERS = [
  'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
  'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
  'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
];
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const results = [], shots = [];
function check(passed, name, details) {
  results.push({ passed: !!passed, name, ...(details === undefined ? {} : { details }) });
  console.log(`${passed ? 'PASS' : 'FAIL'} ${name}${details === undefined ? '' : ': ' + JSON.stringify(details)}`);
}
async function request(path, body = {}) {
  const response = await fetch(BASE + 'api/' + path, {
    method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
  });
  const result = await response.json();
  if (!response.ok || result.error || result.ok === false) throw new Error(`${path}: ${result.error || response.status}`);
  return result;
}
const state = async () => (await fetch(BASE + 'api/state').then(r => r.json())).state;
const exported = async () => JSON.parse((await request('export')).game);

class Cdp {
  constructor(ws) {
    this.ws = ws; this.id = 0; this.pending = new Map(); this.errors = [];
    ws.addEventListener('message', event => {
      const message = JSON.parse(event.data);
      if (message.id !== undefined) {
        const pending = this.pending.get(message.id);
        if (!pending) return;
        this.pending.delete(message.id); clearTimeout(pending.timer);
        message.error ? pending.reject(new Error(JSON.stringify(message.error))) : pending.resolve(message.result);
      } else if (message.method === 'Runtime.exceptionThrown') {
        this.errors.push(message.params.exceptionDetails.exception?.description || message.params.exceptionDetails.text);
      } else if (message.method === 'Runtime.consoleAPICalled' && message.params.type === 'error') {
        this.errors.push(message.params.args.map(a => a.value ?? a.description ?? '').join(' '));
      } else if (message.method === 'Network.responseReceived' && message.params.response.status >= 400) {
        this.errors.push(`${message.params.response.status} ${message.params.response.url}`);
      }
    });
  }
  send(method, params = {}) {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error('CDP timeout: ' + method)); }, 20000);
      this.pending.set(id, { resolve, reject, timer });
      this.ws.send(JSON.stringify({ id, method, params }));
    });
  }
  async eval(expression) {
    const response = await this.send('Runtime.evaluate', {
      expression: `(function(){${expression}})()`, returnByValue: true, awaitPromise: true,
    });
    if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text);
    return response.result.value;
  }
}

async function launchBrowser() {
  const executable = BROWSERS.find(existsSync);
  if (!executable) throw new Error('需要 Chrome 或 Edge 进行真实浏览器验收。');
  const profile = join(tmpdir(), 'nz-forge-uitest-' + Date.now());
  const debugPort = 9400 + Math.floor(Math.random() * 300);
  const child = spawn(executable, [
    '--headless=new', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars',
    '--window-size=1920,1100', 'about:blank',
  ], { stdio: 'ignore', windowsHide: true });
  let page;
  for (let attempt = 0; attempt < 80 && !page; attempt++) {
    await sleep(150);
    try { page = (await fetch(`http://127.0.0.1:${debugPort}/json/list`).then(r => r.json())).find(p => p.type === 'page'); } catch { /* starting */ }
  }
  if (!page) { child.kill(); throw new Error('浏览器 DevTools 未启动。'); }
  const socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve); socket.addEventListener('error', reject); });
  const cdp = new Cdp(socket);
  for (const domain of ['Page', 'Runtime', 'Network']) await cdp.send(domain + '.enable');
  return { cdp, child, profile };
}
async function waitFor(cdp, predicate, description) {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (await cdp.eval(predicate)) return;
    await sleep(80);
  }
  throw new Error('页面未达到预期状态: ' + description);
}
async function click(cdp, selector) {
  const point = await cdp.eval(`
    const button = document.querySelector(${JSON.stringify(selector)});
    if (!button) throw new Error('Missing ' + ${JSON.stringify(selector)});
    button.scrollIntoView({block:'center',behavior:'instant'});
    const r = button.getBoundingClientRect();
    const x=r.left+r.width/2, y=r.top+r.height/2;
    if (r.width < 1 || r.height < 1 || !button.contains(document.elementFromPoint(x,y))) throw new Error('Invisible or blocked button');
    return { x, y };
  `);
  const event = { ...point, button: 'left', clickCount: 1 };
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...event });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...event });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...event });
  await sleep(250);
}
async function screenshot(cdp, name, focus = '.forge-costs') {
  await cdp.eval(`document.querySelector(${JSON.stringify(focus)})?.scrollIntoView({block:'center',behavior:'instant'});return true;`);
  const image = await cdp.send('Page.captureScreenshot', { format: 'png' });
  const file = join(SHOTS, name + '.png');
  writeFileSync(file, Buffer.from(image.data, 'base64')); shots.push(file);
}
async function openForge(cdp) {
  await cdp.send('Page.navigate', { url: BASE });
  await waitFor(cdp, 'return document.querySelector("#player-card .player-name")?.innerText === "炼器浏览器验收" && !!document.querySelector(\'.main-tabs [data-tab="economy"]\') && !document.querySelector("dialog[open]");', '主角状态已加载与工坊页签');
  await click(cdp, '.main-tabs [data-tab="economy"]');
  await waitFor(cdp, 'return !!document.querySelector(\'.forge-costs [data-forge-material="iron"]\') && !document.querySelector(\'.forge-readiness\')?.innerText.includes("推演");', '完整炼器资源预览');
}
async function changeRecipe(cdp, values) {
  for (const [key, value] of Object.entries(values)) {
    await cdp.eval(`
      const select=document.querySelector('[data-forge="${key}"]');
      select.value=${JSON.stringify(value)}; select.dispatchEvent(new Event('change',{bubbles:true})); return true;
    `);
    await waitFor(cdp, 'return !!document.querySelector(\'.forge-preview-heading\') && !!document.querySelector(\'.forge-costs [data-forge-material]\') && !document.querySelector(\'.forge-readiness\')?.innerText.includes("推演");', '换配方后的推演');
  }
  await sleep(150);
}
async function resourceRows(cdp) {
  return cdp.eval(`return [...document.querySelectorAll('.forge-costs [data-forge-material],.forge-costs [data-forge-stones]')].map(row=>({
    id: row.dataset.forgeMaterial || 'stones', text: row.innerText,
    cells:[...row.querySelectorAll('td')].map(cell=>cell.innerText),
    missing:row.querySelector('td:last-child').classList.contains('missing'), sufficient:row.querySelector('td:last-child').classList.contains('sufficient')
  }));`);
}
const numeric = text => Number(String(text).replace(/[^\d]/g, ''));
function verifyRows(rows, info, label) {
  check(info.materials.every(material => {
    const row = rows.find(row => row.id === material.id);
    return row && row.text.includes(material.name) && row.cells.length === 4
      && numeric(row.cells[1]) === material.required && numeric(row.cells[2]) === material.owned
      && (material.missing ? numeric(row.cells[3]) === material.missing : /足|齐|^0\D*$/.test(row.cells[3]))
      && row.missing === (material.missing > 0) && row.sufficient === (material.missing === 0);
  }), `${label}: 每种材料的所需/已有/缺少与实际后端一致`, rows);
  const stones = rows.find(row => row.id === 'stones');
  check(stones?.cells.length === 4 && numeric(stones.cells[1]) === info.stonesRequired
    && numeric(stones.cells[2]) === info.stonesOwned
    && (info.stonesMissing ? numeric(stones.cells[3]) === info.stonesMissing : /足|齐|^0\D*$/.test(stones.cells[3]))
    && stones.missing === (info.stonesMissing > 0), `${label}: 灵石需求与缺口独立显示`);
}
async function buttonView(cdp) {
  return cdp.eval(`
    const b=document.querySelector('[data-action="forge-run"]'); b.scrollIntoView({block:'center',behavior:'instant'});
    const r=b.getBoundingClientRect(), s=getComputedStyle(b), rgb=text=>(text.match(/[0-9.]+/g)||[]).map(Number);
    const lum=color=>{const c=rgb(color).slice(0,3).map(v=>{v/=255;return v<=.04045?v/12.92:((v+.055)/1.055)**2.4});return .2126*c[0]+.7152*c[1]+.0722*c[2]};
    const a=lum(s.color), z=lum(s.backgroundColor);
    return {text:b.innerText, disabled:b.disabled,bg:s.backgroundColor,color:s.color,contrast:(Math.max(a,z)+.05)/(Math.min(a,z)+.05),
      visible:r.width>50&&r.height>20&&r.left>=0&&r.right<=innerWidth+1&&r.top>=0&&r.bottom<=innerHeight+1,
      hit:b.contains(document.elementFromPoint(r.left+r.width/2,r.top+r.height/2)),
      overflow:document.documentElement.scrollWidth>innerWidth+2};
  `);
}

let browser;
try {
  mkdirSync(SHOTS, { recursive: true });
  await request('new', { seed: 121011, name: '炼器浏览器验收', gender: '女', traits: [], allocation: {} });
  const baseline = await exported();
  const load = async (materials = { iron: 4, jade: 3, wood: 3 }, stones = 100) => {
    const fixture = structuredClone(baseline);
    Object.assign(fixture.player, { location: 'wuling', stones, health: 100 });
    fixture.player.arts.materials = materials;
    fixture.player.arts.gear = []; fixture.player.arts.equipped = {};
    fixture.rng = 0;
    await request('import', { game: JSON.stringify(fixture) });
  };
  browser = await launchBrowser();
  const cdp = browser.cdp;
  const defaultRecipe = { main: 'iron', aux: 'jade', core: 'wood', shape: 'sword' };
  for (const width of [1920, 1440, 390]) {
    await load();
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height: width === 390 ? 844 : 1100, deviceScaleFactor: 1, mobile: false });
    await openForge(cdp);
    await changeRecipe(cdp, defaultRecipe);
    const before = await exported();
    const info = (await request('forge-preview', defaultRecipe)).info;
    check(info.valid && info.enabled && info.materials.length === 3 && info.materials.every(m => m.missing === 0), `${width}px: 截图中的精确4/3/3材料与100灵石可以炼器`);
    check(JSON.stringify(await exported()) === JSON.stringify(before), `${width}px: 只读推演不改变存档`);
    verifyRows(await resourceRows(cdp), info, `${width}px 足料`);
    const view = await buttonView(cdp);
    check(view.text.includes('开炉铸炼') && !view.disabled && view.visible && view.hit && !view.overflow,
      `${width}px: 开炉按钮在视野内且真实鼠标可命中，页面不横向溢出`, view);
    check(!/rgba\([^)]*,\s*0\)/.test(view.bg) && view.bg !== 'transparent' && view.color === 'rgb(255, 255, 255)' && view.contrast >= 4.5,
      `${width}px: 开炉按钮有深色背景和清晰白字`, view);
    await screenshot(cdp, `${width}-sufficient`, '.forge-actions');
    await click(cdp, '[data-action="forge-run"]');
    let after;
    for (let attempt = 0; attempt < 100; attempt++) {
      after = await state();
      if (after.clock.days > before.clock.days) break;
      await sleep(80);
    }
    check(after.clock.days === before.clock.days + info.days && after.player.stones === before.player.stones - info.cost
      && Object.values(after.arts.materials).every(n => n === 0), `${width}px: 点击真实开炉消耗4/3/3材料、33灵石并推进炼器时间`);
    const gear = after.gears.find(g => g.crafted && g.shape === 'sword');
    check(!!gear?.ability, `${width}px: 实际成器含法宝神通`, gear && { name: gear.name, ability: gear.ability.name });
    await waitFor(cdp, 'return document.querySelector(\'.forge-costs [data-forge-material="iron"]\')?.innerText.includes("0");', '炼后库存刷新');
    check(await cdp.eval('return [...document.querySelectorAll("[data-forge=main],[data-forge=aux],[data-forge=core]")].every(s=>s.selectedOptions[0].innerText.includes("有 0"));'),
      `${width}px: 炼后配料下拉框库存立即刷新`);
    verifyRows(await resourceRows(cdp), (await request('forge-preview', defaultRecipe)).info, `${width}px 炼后`);
    await screenshot(cdp, `${width}-after-forge`);
    await click(cdp, '.main-tabs [data-tab="market"]');
    await click(cdp, '[data-subtab="market"][data-sub="kit"]');
    check(!!gear && await cdp.eval(`return [...document.querySelectorAll('[data-action="equip"]')].some(button=>button.dataset.id===${JSON.stringify(gear?.id)});`),
      `${width}px: 新法器进入行囊并可装备`);
    await screenshot(cdp, `${width}-crafted-gear`, '[data-action="equip"]');
  }
  await load({ iron: 1, jade: 0, wood: 2 }, 100);
  await openForge(cdp); await changeRecipe(cdp, defaultRecipe);
  let info = (await request('forge-preview', defaultRecipe)).info;
  check(!info.enabled && info.materials.map(m=>m.missing).join(',') === '3,3,1' && info.blockers.length === 3, '三种材料同时不足时完整返回所有缺口');
  verifyRows(await resourceRows(cdp), info, '全部材料不足');
  check((await buttonView(cdp)).disabled && await cdp.eval('return document.querySelectorAll(".forge-costs tr .missing").length === 3;'), '所有缺料行同时标红并禁用开炉');
  await screenshot(cdp, '390-all-materials-missing');

  await load({ iron: 7, jade: 3, wood: 3 }, 100);
  await openForge(cdp); await changeRecipe(cdp, { main: 'iron', aux: 'iron', core: 'iron', shape: 'sword' });
  info = (await request('forge-preview', { main: 'iron', aux: 'iron', core: 'iron', shape: 'sword' })).info;
  check(info.materials.length === 1 && info.materials[0].required === 10 && info.materials[0].owned === 7 && info.materials[0].missing === 3
    && info.materials[0].roles.length === 3 && !info.enabled, '同一材料占三个位置时合计10份，拥有7份不能重复借用');
  verifyRows(await resourceRows(cdp), info, '重复材料');
  check((await buttonView(cdp)).disabled && (await resourceRows(cdp)).find(row=>row.id==='iron')?.text.includes('主料'), '重复材料合并显示角色和真实总缺口');
  await screenshot(cdp, '390-repeated-materials');

  await load({ iron: 4, jade: 3, wood: 3 }, 10);
  await openForge(cdp); await changeRecipe(cdp, defaultRecipe);
  info = (await request('forge-preview', defaultRecipe)).info;
  check(!info.enabled && info.materials.every(m=>m.missing === 0) && info.stonesMissing === 23 && info.blockers.length === 1 && info.blockers[0].includes('灵石'), '材料够而灵石不足独立列出23枚缺口');
  verifyRows(await resourceRows(cdp), info, '灵石不足');
  check((await buttonView(cdp)).disabled && await cdp.eval('return !!document.querySelector("[data-forge-stones] .missing") && !document.querySelector("[data-forge-material] .missing");'), '只有灵石行标红并禁用开炉');
  await screenshot(cdp, '390-stones-missing');
  const errors = cdp.errors.filter(error => !/favicon/.test(error));
  check(errors.length === 0, '全程没有浏览器脚本错误或失败请求', errors);
} catch (error) {
  check(false, '炼器浏览器验收完成', error.stack || error.message);
  if (browser) {
    try {
      const page = await browser.cdp.eval('return {body:document.body.innerText, url:location.href, html:document.querySelector("#workspace")?.innerHTML};');
      writeFileSync(join(SHOTS, 'failure-page.json'), JSON.stringify({ page, errors: browser.cdp.errors }, null, 2), 'utf8');
      await screenshot(browser.cdp, 'failure', '#workspace');
    } catch { /* Preserve the original failure. */ }
  }
} finally {
  if (browser) {
    try { await browser.cdp.send('Browser.close'); } catch { browser.child.kill(); }
    await sleep(500);
    // Delete only this test's freshly generated temporary browser profile.
    const path = resolve(browser.profile), rel = relative(resolve(tmpdir()), path);
    if (!rel.startsWith('..') && !isAbsolute(rel) && rel.startsWith('nz-forge-uitest-')) {
      try { rmSync(path, { recursive: true, force: true }); } catch { /* browser may still release a lock */ }
    }
  }
  const failed = results.filter(result => !result.passed);
  const report = { port: Number(PORT), checks: results.length, passed: results.length - failed.length, failed: failed.length, results, shots };
  mkdirSync(SHOTS, { recursive: true });
  writeFileSync(join(SHOTS, 'results.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`Forge UI: ${report.passed}/${report.checks} passed; ${report.failed} failed.`);
  if (failed.length) process.exitCode = 1;
}
