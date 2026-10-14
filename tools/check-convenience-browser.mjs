// 便利功能真实桌面冒烟：独立临时角色、目录与端口，不连接玩家 8108/8109。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(dirname(fileURLToPath(import.meta.url)), '..'), args = process.argv.slice(2);
const arg = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const binary = resolve(root, arg('--exe', 'build-vs-share/Release/ningzhou_web.exe'));
assert.ok(existsSync(binary), '需要本轮编译完整 Web 程序');
const directory = join(root, 'build', 'convenience-20261014'); mkdirSync(directory, { recursive: true });
const temporary = mkdtempSync(join(directory, 'browser-')), isolated = join(temporary, 'convenience-qa.exe'); copyFileSync(binary, isolated);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8107, 8108, 8109].includes(port), '不得连接玩家端口');
const base = `http://127.0.0.1:${port}`, require = createRequire(import.meta.url);
const { chromium } = require(arg('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
const results = [], timings = [], errors = [], serverLog = [], requests = [];
let server, browser, context, page;
// 用途：保存本轮断言；参与真实浏览器冒烟；传条件和说明；返回无，失败立即抛出。
function check(value, label) { assert.ok(value, label); results.push({ label, passed: true }); console.log(`PASS ${label}`); }
try {
  server = spawn(isolated, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'), '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
    { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', chunk => serverLog.push(chunk.toString())); server.stderr.on('data', chunk => serverLog.push(chunk.toString()));
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    if (server.exitCode !== null) throw new Error('隔离服务器提前退出');
    try { ready = (await fetch(base + '/api/mode')).ok; if (ready) break; } catch {}
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '独立完整服务器启动');
  browser = await chromium.launch({ executablePath: arg('--browser-path', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'), headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN' });
  page = await context.newPage(); page.setDefaultTimeout(20000); page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') requests.push({ path: new URL(request.url()).pathname, body: request.postDataJSON() }); });
  // 用途：通过真实HTTP准备隔离角色与读状态；传接口和可选正文；返回成功JSON，拒绝即失败。
  async function api(path, body) {
    const response = body === undefined ? await context.request.get(`${base}/api/${path}`) : await context.request.post(`${base}/api/${path}`, { data: body });
    const result = await response.json(); assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`); return result;
  }
  await api('login', { nickname: `Cnv${Date.now().toString(36)}`, password: 'isolated-convenience-qa' });
  await api('new', { name: '便利验收', gender: '男', seed: 514052, traits: [], allocation: {} });
  const initial = JSON.parse((await api('export')).game), initialState = (await api('state')).state;
  check(initialState.serviceRoutes?.market && initialState.serviceRoutes?.intel && initialState.bonds?.stipendAction, '本轮程序提供导航与俸禄权威字段');
  const away = initialState.locations.find(location => location.id !== 'tianji' && location.type === 'wild' && location.id !== 'sea');
  assert.ok(away, '存在隔离野外地点');
  const fixture = structuredClone(initial);
  Object.assign(fixture.player, { location: away.id, stage: 3, xp: 0, lifespan: 250, stones: 100000 });
  fixture.player.cave = away.id; fixture.player.arts.herbs = {}; fixture.player.herbs = 0;
  fixture.plan = 'rest'; fixture.job = null; fixture.pending = null; fixture.player.arts.battle = null;
  fixture.clock.days = 10; fixture.year = 0; fixture.clock.ageBase = fixture.player.age;
  if (fixture.tide) fixture.tide.active = false;
  await api('import', { game: JSON.stringify(fixture) });
  let awayState = (await api('state')).state;
  check(awayState.serviceRoutes.market.enabled && !awayState.serviceRoutes.market.alreadyHere, '真实野外可查询最近城市路线');
  await page.goto(base + '/?qa=market#market', { waitUntil: 'domcontentloaded' });
  const marketButton = page.locator('[data-action="service-travel"][data-service-route="market"]'); await marketButton.waitFor();
  check((await marketButton.innerText()).includes(awayState.serviceRoutes.market.destinationName), '异地普通坊市显示最近城市');
  let failFirstTravel = true;
  await page.route('**/api/travel', async route => {
    if (failFirstTravel) { failFirstTravel = false; await route.fulfill({ status: 503, contentType: 'application/json', body: JSON.stringify({ ok: false, error: '隔离验收临时503' }) }); }
    else await route.continue();
  });
  await marketButton.click(); await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  check((await api('state')).state.player.location === away.id && await marketButton.isEnabled(), '导航技术失败不推进且按钮恢复');
  const before = requests.filter(request => request.path === '/api/travel').length, start = performance.now();
  const travelResponse = page.waitForResponse(response => response.url() === base + '/api/travel');
  await marketButton.evaluate(button => { button.click(); button.click(); }); const travelResult = await (await travelResponse).json();
  assert.ok(!travelResult.error && travelResult.ok !== false);
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  timings.push({ label: '一键城市导航至界面恢复', milliseconds: performance.now() - start });
  check(requests.filter(request => request.path === '/api/travel').length - before === 1, '双击一键导航只提交一份 travel');
  const latest = (await api('state')).state;
  if (latest.player.location === awayState.serviceRoutes.market.destination && !latest.pending && !latest.battle && !latest.job) {
    check(new URL(page.url()).hash === '#market' && !await page.locator('[data-service-journey]').count(), '真实抵达城市自动回坊市且清除导航意图');
  } else {
    check(!!latest.job || !!latest.pending || !!latest.battle, '途中遭遇仍保留活动状态');
    check(await page.locator('[data-service-journey]').count() === 1, '途中暂停保留服务旅程提示');
  }
  check(requests.filter(request => ['/api/buy', '/api/craft/materials/buy', '/api/dungeon/intel'].includes(request.path)).length === 0, '导航不自动购物、不买情报');
  // 通过真实 begin 生成未完成行程，再恢复同一合法客户端意图，验证刷新后的继续赶路。
  await api('import', { game: JSON.stringify(fixture) }); awayState = (await api('state')).state;
  const begun = await api('travel', { to: awayState.serviceRoutes.market.destination, mode: awayState.serviceRoutes.market.mode, begin: true });
  check(begun.state.job?.type === 'travel', '真实启程工作可供恢复测试');
  await page.evaluate(({ state, route }) => import('/service-navigation.mjs').then(module => module.saveServiceIntent(module.createServiceIntent(state, route, { tab: 'market', sub: '' }))),
    { state: awayState, route: awayState.serviceRoutes.market });
  await page.goto(base + '/?qa=resume#chronicle', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-service-journey]').waitFor();
  check(await page.locator('[data-service-journey] [data-action="travel-resume"]').isEnabled(), '刷新后服务旅途显示继续赶路');
  await page.reload({ waitUntil: 'domcontentloaded' }); await page.locator('[data-service-journey]').waitFor();
  check(await page.locator('[data-service-journey]').count() === 1, '重复刷新不丢失导航也不自动推进');
  await page.locator('[data-action="service-navigation-dismiss"]').click();
  check((await api('state')).state.job?.type === 'travel' && !await page.locator('[data-service-journey]').count(), '收起导航只清客户端提示，真实行程保留');
  // 情报与工坊异地入口使用真实报价；本轮保持材料秘境玩法。
  await api('import', { game: JSON.stringify(fixture) });
  await page.goto(base + '/?qa=intel#dungeon', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-dungeon-view="intel"]').click();
  check(await page.locator('[data-service-route="intel"]').isVisible(), '天机阁情报异地有一键前往');
  await page.goto(base + '/?qa=craft#workshop', { waitUntil: 'domcontentloaded' });
  const quoteButton = page.locator('[data-action="craft-supply-quote"]').first(); await quoteButton.waitFor();
  const quoteStart = performance.now(), quotePosts = requests.filter(request => request.path === '/api/craft/materials/quote').length;
  const quoteResponse = page.waitForResponse(response => response.url() === base + '/api/craft/materials/quote');
  await quoteButton.evaluate(button => { button.click(); button.click(); });
  const quoted = await (await quoteResponse).json(); assert.ok(!quoted.error, '工坊材料报价成功');
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  timings.push({ label: '补料报价至路线显示', milliseconds: performance.now() - quoteStart });
  check(requests.filter(request => request.path === '/api/craft/materials/quote').length - quotePosts === 1, '双击补料报价只发送一次只读请求');
  check(!!quoted.quote?.travel && await page.locator('[data-service-route="craft"]').count() > 0, '真实一键补料报价显示对应可购城市路线');
  // 远程俸禄与重复点击验证。
  const stipendFixture = structuredClone(fixture); stipendFixture.player.sect = 'jinhong';
  stipendFixture.bonds.payroll = { sect: 'jinhong', lastMonth: 0, balance: 123, credited: 1 };
  stipendFixture.bonds.stipend = -1; stipendFixture.bonds.notoriety = 0;
  await api('import', { game: JSON.stringify(stipendFixture) }); const stipendState = (await api('state')).state;
  check(stipendState.bonds.stipendAction.enabled && stipendState.player.location !== stipendState.player.sect, '野外真实俸禄报价允许远程领取');
  await page.goto(base + '/?qa=stipend#chronicle', { waitUntil: 'domcontentloaded' });
  const stipendButton = page.locator('[data-action="stipend"]').first(); await stipendButton.waitFor();
  const stipendPosts = requests.filter(request => request.path === '/api/stipend').length;
  const stipendStart = performance.now();
  const stipendResponse = page.waitForResponse(response => response.url() === base + '/api/stipend'); await stipendButton.evaluate(button => { button.click(); button.click(); });
  const stipend = await (await stipendResponse).json(); assert.ok(!stipend.error && stipend.ok !== false);
  await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  timings.push({ label: '远程俸禄至界面恢复', milliseconds: performance.now() - stipendStart });
  check(stipend.state.player.stones === stipendState.player.stones + stipendState.bonds.stipendAction.paid
    && requests.filter(request => request.path === '/api/stipend').length - stipendPosts === 1, '远程俸禄按报价到账且双击只领取一次');
  check(await page.locator('[data-action="stipend"]').first().isDisabled(), '本月已领取后按钮禁用');
  // 使用真实 NPC 配装字段筛选，构筑不改动。
  const friendFixture = structuredClone(fixture), candidates = friendFixture.npcs.filter(npc => npc.alive && npc.gender !== '妖兽').slice(0, 3);
  assert.ok(candidates.length === 3);
  candidates.forEach((npc, index) => { npc.name = ['验收甲', '验收乙', '验收丙'][index]; npc.location = away.id; npc.stage = [3, 5, 6][index]; npc.lifespan = 600;
    friendFixture.player.relations[npc.id] = [55, 90, -10][index]; });
  await api('import', { game: JSON.stringify(friendFixture) }); const friendState = (await api('state')).state;
  const selected = friendState.nearby.find(npc => npc.id === candidates[0].id); assert.ok(selected?.buildId && selected?.buildName);
  await page.goto(base + '/?qa=friends#map', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-subtab="map"][data-sub="friends"]').click();
  await page.locator('[data-friend-filter="realm"]').selectOption('1');
  await page.locator('[data-friend-filter="relation"]').selectOption('friendly');
  await page.locator('[data-friend-filter="build"]').selectOption(selected.buildId);
  await page.locator('[data-friend-search]').fill('验收甲');
  check(await page.locator('.friend-card').count() === 1 && await page.locator('.friend-card').getAttribute('data-person') === selected.id, '真实流派+境界+好感+姓名组合仅保留目标NPC');
  const search = page.locator('[data-friend-search]'); await search.focus();
  await search.evaluate(input => { window.__friendSearchBefore = input; input.dispatchEvent(new CompositionEvent('compositionstart', { bubbles: true })); input.value = '验'; input.dispatchEvent(new InputEvent('input', { bubbles: true, isComposing: true })); });
  check(await search.evaluate(input => input === window.__friendSearchBefore && document.activeElement === input), '中文组词期间不重建搜索框');
  await search.evaluate(input => { input.value = '验收甲'; input.dispatchEvent(new CompositionEvent('compositionend', { bubbles: true })); });
  check(await page.locator('[data-friend-search]').evaluate(input => document.activeElement === input && input.value === '验收甲'), '中文组词完成后保留输入焦点与文本');
  await page.locator('[data-friend-search]').fill('不存在的仙友');
  check(await page.locator('.friend-card').count() === 0 && (await page.locator('.fs-list').innerText()).includes('没有符合条件的仙友'), '浏览器无匹配不显示全部');
  await page.locator('[data-friend-filter-reset]').click(); await page.locator('[data-friend-search]').fill('验收');
  await page.locator('[data-friend-filter="sort"]').selectOption('stage-desc');
  const sorted = await page.locator('.friend-card').evaluateAll(cards => cards.map(card => card.dataset.person));
  check(sorted.join() === [candidates[2].id, candidates[1].id, candidates[0].id].join(), '境界降序保持真实ID');
  await page.reload({ waitUntil: 'domcontentloaded' });
  await page.locator('[data-subtab="map"][data-sub="friends"]').click();
  check(await page.locator('[data-friend-filter="sort"]').inputValue() === 'stage-desc' && await page.locator('[data-friend-search]').inputValue() === '验收', '刷新保留本地筛选与排序偏好');
  await page.setViewportSize({ width: 850, height: 900 }); await page.locator('[data-friend-filters]').scrollIntoViewIfNeeded();
  await page.screenshot({ path: join(temporary, 'nearby-filters-850.png'), fullPage: false });
  check(await page.locator('[data-friend-filter="build"]').isVisible() && await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px桌面筛选可操作且无整页横向溢出');
  await page.route('**/api/npc-bag', async route => { await new Promise(done => setTimeout(done, 350)); await route.continue(); });
  const bagResponse = page.waitForResponse(response => response.url() === base + '/api/npc-bag');
  await page.locator(`[data-person="${selected.id}"]`).click(); await page.locator('.friend-panel').waitFor();
  check((await page.locator('.friend-panel').innerText()).includes(selected.name), '筛选后点击仍进入原NPC详情');
  await page.locator('[data-friend-search]').focus();
  await page.locator('[data-friend-search]').evaluate(input => { window.__compositionWhileBag = input; input.dispatchEvent(new CompositionEvent('compositionstart', { bubbles: true })); });
  await bagResponse; await page.waitForTimeout(100);
  check(await page.locator('[data-friend-search]').evaluate(input => input === window.__compositionWhileBag && document.activeElement === input), 'NPC背包迟到响应不打断筛选框中文组词');
  check(errors.length === 0, '本轮浏览器无未处理脚本异常');
} finally {
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ binary, port, results, timings, errors }, null, 2));
  writeFileSync(join(temporary, 'server.log'), serverLog.join(''));
  await browser?.close(); server?.kill();
  console.log(`报告: ${join(temporary, 'report.json')}`);
}
