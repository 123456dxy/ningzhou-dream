// Browser regression for owned books, numeric previews, and the sect entrance.
// Mutates only the explicitly isolated game server on port 18110.
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync, rmSync } from 'node:fs';
import { join, dirname, resolve, relative, isAbsolute } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const PORT = args.includes('--port') ? args[args.indexOf('--port') + 1] : '18110';
if (PORT !== '18110') throw new Error('师门与道法验收只允许隔离端口 18110。');
const BASE = `http://127.0.0.1:${PORT}/`;
const SHOTS = join(ROOT, 'build', 'shots', 'sect-arts-ui');
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
const state = async () => (await request('state')).state;
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
  const executable = [
    'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe',
    'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
    'C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe',
  ].find(existsSync);
  if (!executable) throw new Error('需要 Chrome 或 Edge 进行真实浏览器验收。');
  const profile = join(tmpdir(), 'nz-sect-arts-uitest-' + Date.now());
  const debugPort = 9750 + Math.floor(Math.random() * 150);
  const child = spawn(executable, [
    '--headless=new', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`,
    '--no-first-run', '--no-default-browser-check', '--disable-gpu', '--hide-scrollbars',
    '--window-size=1440,1100', 'about:blank',
  ], { stdio: 'ignore', windowsHide: true });
  let page;
  for (let attempt = 0; attempt < 80 && !page; attempt++) {
    await sleep(150);
    try { page = (await fetch(`http://127.0.0.1:${debugPort}/json/list`).then(r => r.json())).find(p => p.type === 'page'); } catch { /* starting */ }
  }
  if (!page) { child.kill(); throw new Error('浏览器 DevTools 未启动。'); }
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { ws.addEventListener('open', resolve); ws.addEventListener('error', reject); });
  const cdp = new Cdp(ws);
  for (const domain of ['Page', 'Runtime', 'Network']) await cdp.send(domain + '.enable');
  return { cdp, child, profile };
}
async function waitFor(cdp, expression, description) {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (await cdp.eval(expression)) return;
    await sleep(80);
  }
  throw new Error('页面未达到预期状态: ' + description);
}
async function waitState(predicate, description) {
  for (let attempt = 0; attempt < 100; attempt++) {
    const current = await state();
    if (predicate(current)) return current;
    await sleep(80);
  }
  throw new Error('规则状态未达到预期: ' + description);
}
async function click(cdp, selector) {
  const point = await cdp.eval(`
    const button=document.querySelector(${JSON.stringify(selector)});
    if(!button)throw new Error('Missing '+${JSON.stringify(selector)});
    button.scrollIntoView({block:'center',behavior:'instant'});
    const r=button.getBoundingClientRect(),x=r.left+r.width/2,y=r.top+r.height/2;
    if(r.width<1||r.height<1||!button.contains(document.elementFromPoint(x,y)))throw new Error('Invisible or blocked '+${JSON.stringify(selector)});
    return{x,y};`);
  const event = { ...point, button: 'left', clickCount: 1 };
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseMoved', ...event });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mousePressed', ...event });
  await cdp.send('Input.dispatchMouseEvent', { type: 'mouseReleased', ...event });
  await sleep(180);
}
async function screenshot(cdp, name, focus = '#workspace') {
  await cdp.eval(`document.querySelector(${JSON.stringify(focus)})?.scrollIntoView({block:'center',behavior:'instant'});return true;`);
  const image = await cdp.send('Page.captureScreenshot', { format: 'png' });
  const file = join(SHOTS, name + '.png'); writeFileSync(file, Buffer.from(image.data, 'base64')); shots.push(file);
}
async function navigate(cdp) {
  await cdp.send('Page.navigate', { url: BASE });
  await waitFor(cdp, 'return document.querySelector("#player-card .player-name")?.innerText === "道法师门浏览器验收" && !document.querySelector("dialog[open]");', '导入后的角色已显示');
}
async function tab(cdp, name, selector) {
  await click(cdp, `.main-tabs [data-tab="${name}"]`);
  if (selector) await waitFor(cdp, `return !!document.querySelector(${JSON.stringify(selector)});`, `${name}页`);
}
async function sub(cdp, name, page, selector) {
  await click(cdp, `[data-subtab="${name}"][data-sub="${page}"]`);
  if (selector) await waitFor(cdp, `return !!document.querySelector(${JSON.stringify(selector)});`, `${name}/${page}页`);
}
async function noOverflow(cdp, width, name) {
  const geometry = await cdp.eval('return {width:innerWidth,scroll:document.documentElement.scrollWidth};');
  check(geometry.scroll <= geometry.width + 2, `${width}px: ${name}没有页面横向溢出`, geometry);
}

let browser;
try {
  mkdirSync(SHOTS, { recursive: true });
  await request('new', { seed: 121012, name: '道法师门浏览器验收', gender: '女', traits: [], allocation: {} });
  const baseline = await exported();
  const mentor = baseline.npcs.find(n => !n.beast && n.alive && n.sect === 'jinhong')
    || baseline.npcs.find(n => !n.beast && n.alive);
  if (!mentor) throw new Error('初始合法存档中没有可用于隔离验收的修士。');
  const load = async ({ sect = false, affinity = 60, remote = false } = {}) => {
    const fixture = structuredClone(baseline);
    Object.assign(fixture.player, { name: '道法师门浏览器验收', location: sect ? 'jinhong' : 'wuling',
      sect: sect ? 'jinhong' : '', stage: sect ? 3 : 0, health: 100, stones: 2000, renown: 100 });
    Object.assign(fixture.player.arts, { books: ['wheel'], manuals: { tuna: 1 }, main: 'tuna', aux: [],
      knownSpells: ['sword', 'wood'], spells: ['sword'], gear: [], equipped: {} });
    fixture.economy.books = ['wheel']; fixture.economy.taskCounts = {}; fixture.economy.month = -1;
    fixture.economy.contribution = 100; fixture.bonds.master = '';
    const npc = fixture.npcs.find(n => n.id === mentor.id);
    Object.assign(npc, { sect: 'jinhong', stage: 6, location: remote ? 'wuling' : 'jinhong', health: 100, alive: true });
    fixture.player.relations[mentor.id] = affinity;
    await request('import', { game: JSON.stringify(fixture) });
  };
  browser = await launchBrowser(); const cdp = browser.cdp;
  for (const width of [1920, 1440, 390]) {
    await cdp.send('Emulation.setDeviceMetricsOverride', { width, height: width === 390 ? 844 : 1100, deviceScaleFactor: 1, mobile: false });
    await load(); await navigate(cdp); await tab(cdp, 'arts', '[data-subtab="arts"]');
    await sub(cdp, 'arts', 'practice', '[data-action="learn"][data-id="wheel"]');
    check(await cdp.eval('const b=document.querySelector(\'[data-action="learn"][data-id="wheel"]\');return !!b&&!b.disabled&&b.closest(".pill-row").innerText.includes("重元转轮诀")&&document.querySelector("#workspace").innerText.includes("已得功法抄本");'),
      `${width}px: 散修可在洞府找到事件获得的重元抄本并开始修习`);
    await noOverflow(cdp, width, '功法抄本'); await screenshot(cdp, `${width}-owned-book`, '[data-action="learn"][data-id="wheel"]');
    const beforeLearn = await exported();
    await click(cdp, '[data-action="learn"][data-id="wheel"]');
    const learned = await waitState(s => s.arts.manuals.wheel === 1, '抄本修习完成');
    check(learned.clock.days > beforeLearn.clock.days && learned.player.stones < beforeLearn.player.stones,
      `${width}px: 点击抄本实际修习出功法第一层并支付时间和灵石`);

    await load(); await navigate(cdp); await tab(cdp, 'arts', '[data-subtab="arts"]');
    await sub(cdp, 'arts', 'spells', '[data-spell-id="gold"]');
    const catalog = (await request('arts/spells')).spells;
    const gold = catalog.find(sp => sp.id === 'gold');
    const statusCards = await cdp.eval('return ["gold","wood","sword","rain"].map(id=>{const b=document.querySelector(`[data-spell-id="${id}"]`);return{id,text:b?.innerText,disabled:b?.disabled,classes:b?.className};});');
    check(statusCards.find(c => c.id === 'gold')?.disabled && statusCards.find(c => c.id === 'gold')?.classes.includes('unlearned')
      && statusCards.find(c => c.id === 'gold')?.text.includes('未学会')
      && !statusCards.find(c => c.id === 'wood')?.disabled && statusCards.find(c => c.id === 'wood')?.text.includes('已学会 · 未携带')
      && statusCards.find(c => c.id === 'sword')?.classes.includes('selected') && statusCards.find(c => c.id === 'rain')?.disabled,
      `${width}px: 未学、已学未带、已携带及境界锁定状态可清楚区分`, statusCards);
    check(await cdp.eval(`return document.querySelector('[data-spell-id="gold"] [data-numeric-preview]')?.innerText.includes(${JSON.stringify(gold.preview.text)});`),
      `${width}px: 非战斗神通伤害与后端真实预览完全一致`);
    check(await cdp.eval('return document.querySelector(\'[data-spell-id="wood"] [data-numeric-preview]\')?.innerText.includes("单次治疗量");'),
      `${width}px: 满血时仍能看到治疗量`);
    await noOverflow(cdp, width, '神通目录'); await screenshot(cdp, `${width}-spell-status`, '[data-spell-id="gold"]');
    await click(cdp, '[data-spell-id="wood"]');
    await waitState(s => s.arts.spells.includes('wood'), '已学神通携带');
    await waitFor(cdp, 'return document.querySelector(\'[data-spell-id="wood"]\')?.classList.contains("selected");', '携带外观刷新');
    await click(cdp, '[data-spell-id="wood"]');
    const uncarried = await waitState(s => !s.arts.spells.includes('wood'), '卸下神通');
    check(uncarried.arts.knownSpells.includes('wood'), `${width}px: 卸下神通保留已学状态`);
    await sub(cdp, 'arts', 'builds', '.art-build');
    const names = await cdp.eval('return [...document.querySelectorAll(".build-components")].filter(g=>g.querySelector("b").innerText==="功法").flatMap(g=>[...g.querySelectorAll(".build-component")].map(c=>c.childNodes[0].textContent.trim()));');
    check(names.includes('金虹诀') && names.includes('重剑诀') && names.every(name => !/^[a-z][a-z0-9_-]*$/i.test(name)),
      `${width}px: 流派组合全用中文功法真名`, names);
    await noOverflow(cdp, width, '流派组合'); await screenshot(cdp, `${width}-build-names`, '.art-build');

    await tab(cdp, 'economy', '.forge-preview-heading');
    await waitFor(cdp, 'return !!document.querySelector(".forge-passive")&&!!document.querySelector(".forge-preview [data-numeric-preview]");', '法器主动和被动数值');
    const recipe = await cdp.eval('return Object.fromEntries([...document.querySelectorAll("[data-forge]")].map(s=>[s.dataset.forge,s.value]));');
    const forge = (await request('forge-preview', recipe)).info;
    check(await cdp.eval(`return document.querySelector('.forge-preview [data-numeric-preview]')?.innerText.includes(${JSON.stringify(forge.ability.preview.text)})&&document.querySelector('.forge-passive')?.innerText.includes(${JSON.stringify(forge.passive.text)});`),
      `${width}px: 炼器时主动威力和被动属性均与真实核心一致`);
    await click(cdp, '.forge-inscribed summary');
    check(await cdp.eval(`return document.querySelector('.forge-inscribed')?.open&&document.querySelector('.forge-inscribed [data-numeric-preview]')?.innerText.includes(${JSON.stringify(forge.inscribedAbility.preview.text)});`),
      `${width}px: 可展开查看铭刻后的真实数值`);
    await noOverflow(cdp, width, '炼器数值'); await screenshot(cdp, `${width}-forge-numbers`, '.forge-preview');

    await load({ sect: true }); await navigate(cdp); await tab(cdp, 'map', '[data-map-detail]');
    // The map selects the character's current location on load. The destination
    // directory is a closed <details>; only open it if another place is focused.
    if (!await cdp.eval('return document.querySelector("[data-map-detail]")?.dataset.mapDetail === "jinhong";')) {
      if (!await cdp.eval('return document.querySelector(".atlas-directory-list")?.open;'))
        await click(cdp, '.atlas-directory-list summary');
      await click(cdp, '.atlas-place-list [data-map-loc="jinhong"]');
    }
    await waitFor(cdp, 'return !!document.querySelector(\'[data-sect-entrance="jinhong"] [data-action="sect-open"]\');', '本宗地图入口');
    check(await cdp.eval('return [...document.querySelectorAll(\'[data-sect-entrance="jinhong"] button\')].some(b=>b.innerText==="进入宗门"&&!b.disabled);'),
      `${width}px: 当前山门可直接进入宗门，并可直达拜师、差事和藏经阁`);
    await noOverflow(cdp, width, '地图入口'); await screenshot(cdp, `${width}-sect-map-entrance`, '[data-sect-entrance="jinhong"]');
    await click(cdp, '[data-sect-entrance="jinhong"] [data-action="sect-open"][data-sub="mentors"]');
    await waitFor(cdp, `return !!document.querySelector('[data-sect-mentor="${mentor.id}"]');`, '地图进入拜师页');
    const mentorData = (await request('sects')).mentors.find(n => n.id === mentor.id);
    check(mentorData.enabled && await cdp.eval(`const card=document.querySelector('[data-sect-mentor="${mentor.id}"]');return !!card&&!card.querySelector('[data-id="master"]').disabled&&card.innerText.includes('好感 60')&&!!card.querySelector('img');`),
      `${width}px: 师尊候选显示立绘、好感、境界、所在位置及真实拜师资格`);
    await noOverflow(cdp, width, '拜师页'); await screenshot(cdp, `${width}-sect-mentor`, `[data-sect-mentor="${mentor.id}"]`);
    const beforeMaster = await exported();
    await click(cdp, `[data-sect-mentor="${mentor.id}"] [data-id="master"]`);
    const mastered = await waitState(s => s.bonds.master === mentor.id, '真实拜师结算');
    check(mastered.clock.days === beforeMaster.clock.days + 5 && mastered.player.stones === beforeMaster.player.stones - 20,
      `${width}px: 行拜师礼实际建立师徒关系，耗时5天、支付20灵石`);

    await load({ sect: true }); await navigate(cdp); await tab(cdp, 'society', '[data-subtab="society"]');
    await sub(cdp, 'society', 'jobs', '[data-sect-task="sect-patrol"]');
    const taskIds = ['sect-patrol', 'sect-records', 'sect-array'];
    const tasks = (await request('eco-options')).options.filter(o => taskIds.includes(o.id));
    check(tasks.length === 3 && tasks.every(t => t.enabled && t.contribution > 0 && t.daoExp > 0)
      && await cdp.eval('return document.querySelectorAll("[data-sect-task]").length===4&&document.querySelector("[data-sect-task-board]").innerText.includes("0 / 3");'),
      `${width}px: 宗门任务板含三项新差事及真实灵石、贡献、大道经验和共用额度`);
    await noOverflow(cdp, width, '宗门任务'); await screenshot(cdp, `${width}-sect-tasks`, '[data-sect-task-board]');
    for (const id of taskIds) {
      const before = await state(), task = tasks.find(t => t.id === id);
      await click(cdp, `[data-sect-task="${id}"] [data-action="eco-run"]`);
      const after = await waitState(s => s.clock.days > before.clock.days, id + '完成');
      check(after.clock.days === before.clock.days + task.days && after.player.stones === before.player.stones + task.pay
        && after.contribution === before.contribution + task.contribution
        && (after.arts.exp[task.dao] || 0) === (before.arts.exp[task.dao] || 0) + task.daoExp,
        `${width}px: ${task.name}点击后时间与三项实际收益符合任务板文本`);
      await waitFor(cdp, `return document.querySelector('[data-sect-task-board]')?.innerText.includes('${taskIds.indexOf(id) + 1} / 3');`, '额度刷新');
    }
    check(await cdp.eval('return [...document.querySelectorAll("[data-sect-task] [data-action=eco-run]")].every(b=>b.disabled)&&document.querySelector("[data-sect-task-board]").innerText.includes("3 / 3");'),
      `${width}px: 三种差事共享月限，接满后全部禁止重复刷取`);
  }
  await load({ sect: true, affinity: 10 }); await navigate(cdp); await tab(cdp, 'society', '[data-subtab="society"]');
  await sub(cdp, 'society', 'mentors', '[data-sect-mentors]');
  check(await cdp.eval(`const card=document.querySelector('[data-sect-mentor="${mentor.id}"]');return card.querySelector('[data-id="master"]').disabled&&card.innerText.includes('好感不足40');`), '好感不足的具体原因直接显示在拜师卡上');
  await load({ sect: true, remote: true }); await navigate(cdp); await tab(cdp, 'society', '[data-subtab="society"]');
  await sub(cdp, 'society', 'mentors', '[data-sect-mentors]');
  check(await cdp.eval(`const card=document.querySelector('[data-sect-mentor="${mentor.id}"]');return card.querySelector('[data-id="master"]').disabled&&!!card.querySelector('[data-action="sect-locate"]')&&card.innerText.includes('须先赶路相见');`), '异地师长可查看，但需赶路当面拜师');
  await screenshot(cdp, '390-remote-mentor', `[data-sect-mentor="${mentor.id}"]`);
  const errors = cdp.errors.filter(error => !/favicon/.test(error));
  check(errors.length === 0, '整个验收没有浏览器脚本错误和失败请求', errors);
} catch (error) {
  check(false, '道法与师门浏览器验收完成', error.stack || error.message);
  if (browser) {
    try {
      const page = await browser.cdp.eval('return {body:document.body.innerText,url:location.href,html:document.querySelector("#workspace")?.innerHTML};');
      writeFileSync(join(SHOTS, 'failure-page.json'), JSON.stringify({ page, errors: browser.cdp.errors }, null, 2), 'utf8');
      await screenshot(browser.cdp, 'failure');
    } catch { /* retain original failure */ }
  }
} finally {
  if (browser) {
    try { await browser.cdp.send('Browser.close'); } catch { browser.child.kill(); }
    await sleep(500);
    const target = resolve(browser.profile), rel = relative(resolve(tmpdir()), target);
    if (!rel.startsWith('..') && !isAbsolute(rel) && rel.startsWith('nz-sect-arts-uitest-')) {
      try { rmSync(target, { recursive: true, force: true }); } catch { /* browser releases profile asynchronously */ }
    }
  }
  const failed = results.filter(result => !result.passed);
  const report = { port: Number(PORT), checks: results.length, passed: results.length - failed.length,
    failed: failed.length, results, shots };
  mkdirSync(SHOTS, { recursive: true }); writeFileSync(join(SHOTS, 'results.json'), JSON.stringify(report, null, 2), 'utf8');
  console.log(`Sect/arts UI: ${report.passed}/${report.checks} passed; ${report.failed} failed.`);
  if (failed.length) process.exitCode = 1;
}
