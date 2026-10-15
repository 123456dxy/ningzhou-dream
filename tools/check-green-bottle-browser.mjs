// 永久绿瓶/三年坊市/紧凑道法：真实完整服务、临时账号和端口，不使用玩家数据。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
// 用途：读取隔离检查选项；流程：工具启动；返回：指定参数或缺省值。
const argument = (key, fallback) => args.includes(key) ? args[args.indexOf(key) + 1] : fallback;
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = join(root, 'build/diagnostics/green-bottle-browser-20261015');
mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'green-bottle-qa.exe');
copyFileSync(resolve(root, argument('--exe', 'build-vs-share/Release/ningzhou_web.exe')), executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8107, 8108, 8109, 8110, 8111, 8112, 8113].includes(port));
const base = `http://127.0.0.1:${port}`;
const server = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
  '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const checks = [], timings = [], errors = [], serverLog = [], posts = [];
server.stdout.on('data', chunk => serverLog.push(String(chunk))); server.stderr.on('data', chunk => serverLog.push(String(chunk)));
let browser, page, context;
// 用途：记录真实断言；流程：界面冒烟；返回：无，失败立即保存并抛错。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
try {
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { ready = (await fetch(base + '/api/mode')).ok; } catch { /* 新服务启动中。 */ }
    if (ready) break;
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '隔离完整游戏服务启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') posts.push(new URL(request.url()).pathname); });
  // 用途：在独立测试账号调用真实API；流程：夹具、报价与结果核对；返回：权威JSON，失败抛错。
  async function api(path, body = {}) {
    await new Promise(done => setTimeout(done, 90));
    const response = await context.request.post(base + '/api/' + path, { data: body });
    const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
    return result;
  }
  // 用途：点击真实按钮并计时；流程：升阶/种植/开境；返回：动作结果及可再次操作的页面。
  async function action(locator, path, twice = false) {
    await new Promise(done => setTimeout(done, 90));
    const start = performance.now();
    const promise = page.waitForResponse(response => new URL(response.url()).pathname === '/api/' + path && response.request().method() === 'POST');
    const [response] = await Promise.all([promise, twice ? locator.evaluate(button => { button.click(); button.click(); }) : locator.click()]);
    const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, result.error || path);
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    timings.push({ path, milliseconds: performance.now() - start });
    return result;
  }
  // 用途：重载并确认当前界面可操作；流程：导入夹具后；返回：无，不假定旧DOM仍有效。
  async function open(hash) {
    await page.goto(base + '/?qa=' + Date.now() + '#' + hash, { waitUntil: 'domcontentloaded' });
    await page.locator('.main-tabs').waitFor();
  }
  await api('login', { nickname: `Green${Date.now().toString(36).slice(-8)}`, password: 'isolated-green-qa' });
  await api('new', { name: '绿瓶界面验收', gender: '男', portraitId: 'player-male', seed: 313373, traits: ['greenBottle'], allocation: {} });
  const initial = (await api('state')).state;
  check(initial.cave.garden.plantsPerPlotLimit === 12, '真实协议每格上限12份');
  const herb = initial.herbCatalog.find(item => item.id === '6001'); assert.ok(herb);
  const fixture = JSON.parse((await api('export')).game);
  fixture.player.cave = fixture.player.location = 'wuling'; fixture.player.caveTier = 2; fixture.player.qiSupply = 1;
  // 专属灵境按金丹/元婴/化神准入；综合升阶夹具具备最高入口资格，低境拒绝由独立专属秘境冒烟覆盖。
  fixture.player.stage = fixture.player.highestStage = 12; fixture.player.lifespan = 2000; fixture.player.xp = 0;
  fixture.player.renown = 100; fixture.player.stones = 500000;
  fixture.player.arts.herbs[herb.name] = 100; fixture.player.herbs = Object.values(fixture.player.arts.herbs).reduce((sum, amount) => sum + amount, 0);
  await api('import', { game: JSON.stringify(fixture) });
  if (argument('--mode', 'all') !== 'market') {
  await open('cave'); await page.locator('[data-green-bottle-panel]').waitFor();
  check((await page.locator('.cave-page').innerText()).includes('0 / 6'), '真实洞府显示瓶阶0与一次性升阶');
  check(await page.locator('[data-cave-reserve-amount],.cave-reserve').count() === 0
    && !(await page.locator('.cave-page').innerText()).match(/灵气方案|灵石\s*\/\s*天|按当前储备/), '删除储备控件、日费与灵田灵气方案');
  const before = (await api('export')).game; await api('state'); await api('state');
  check((await api('export')).game === before, '反复读取状态不成长不扣费不升级');
  const upgrade = page.locator('[data-action="green-bottle-upgrade"]');
  check((await upgrade.innerText()) === '投入大量灵石，为小绿瓶恢复些许灵性', '升阶按钮使用指定完整文案');
  const startPosts = posts.filter(path => path.endsWith('/green-bottle/upgrade')).length;
  let reply = await action(upgrade, 'cave/green-bottle/upgrade', true);
  check(reply.state.cave.greenBottle.currentGrade === 1 && reply.state.player.stones === 498000, '真实一次性支付2000并永久升到一阶');
  check(posts.filter(path => path.endsWith('/green-bottle/upgrade')).length === startPosts + 1, '双击升阶只提交一次');
  for (const cost of [5000, 8000]) {
    const balance = reply.state.player.stones;
    reply = await action(page.locator('[data-action="green-bottle-upgrade"]'), 'cave/green-bottle/upgrade');
    check(reply.state.player.stones === balance - cost, `真实升阶只扣本级${cost}灵石`);
  }
  check(reply.state.cave.greenBottle.currentGrade === 3 && reply.state.cave.greenBottle.currentUnitsPerDay === 210, '三阶聚灵仙府日增60+150=210');
  const portal = reply.state.cave.greenBottle.dungeons.find(dungeon => dungeon.grade === 4);
  check(portal?.enabled && !reply.state.cave.greenBottle.dungeons.some(dungeon => dungeon.grade === 5), '瓶三阶开放四阶灵材，五阶尚不出现');
  const plot = page.locator('[data-cave-plot="plot-1"]');
  await plot.locator('[data-cave-plant-herb]').selectOption(herb.id);
  await plot.locator('[data-cave-plant-count]').press('End');
  reply = await action(plot.locator('[data-action="cave-garden-plant"]'), 'cave/garden/plant');
  check(reply.state.cave.garden.plots[0].seedCount === 12 && reply.state.cave.garden.plots[0].estimatedEffectiveDays === 86, '真实播种12份与三阶86天预报一致');
  const stonesBeforeGrowth = reply.state.player.stones;
  await api('plan', { plan: 'rest' });
  await api('advance', { days: 1 });
  let afterGrowth = (await api('state')).state;
  check(afterGrowth.cave.garden.plots[0].cohorts[0].progressUnits === 210, '真实世界日按210成长');
  check(afterGrowth.player.stones === stonesBeforeGrowth, '休养推进一天灵田不再产生任何日费');
  await open('cave'); await page.locator('[data-green-bottle-dungeons] summary').click();
  reply = await action(page.locator('[data-action="green-bottle-dungeon"][data-material-grade="4"]'), 'cave/green-bottle/dungeon');
  check(reply.state.dungeons.run && reply.state.dungeons.run.siteId, '洞府按钮进入真实爬塔秘境界面');
  const materialBeforeLeave = reply.state.arts.materials['bottle-spirit-4'] || 0;
  await api('dungeon/leave');
  check(((await api('state')).state.arts.materials['bottle-spirit-4'] || 0) === materialBeforeLeave, '未通关离境不发保底灵材');
  const advanced = JSON.parse((await api('export')).game); advanced.job = null; advanced.pending = null;
  advanced.player.arts.materials['bottle-spirit-4'] = 50; advanced.player.arts.materials['bottle-spirit-5'] = 100; advanced.player.arts.materials['bottle-spirit-6'] = 200;
  await api('import', { game: JSON.stringify(advanced) }); await open('cave');
  writeFileSync(join(temporary, 'upgrade-fixture-state.json'), JSON.stringify((await api('state')).state.cave.greenBottle, null, 2));
  for (const grade of [4, 5, 6]) {
    reply = await action(page.locator('[data-action="green-bottle-upgrade"]'), 'cave/green-bottle/upgrade');
    check(reply.state.cave.greenBottle.currentGrade === grade && reply.state.arts.materials[`bottle-spirit-${grade}`] === 0, `升${grade}阶真实消耗对应专属灵材`);
  }
  check(reply.state.cave.greenBottle.currentUnitsPerDay === 760
    && reply.state.cave.greenBottle.dungeons.some(dungeon => dungeon.grade === 6 && dungeon.enabled), '六阶日增760且六阶秘境入口继续开放');
  check(!(await page.locator('[data-action="green-bottle-upgrade"]').isEnabled()), '满阶不能继续扣费');
  await page.screenshot({ path: join(temporary, 'bottle-max.png'), fullPage: true });
  const noBottle = JSON.parse((await api('export')).game);
  noBottle.player.traits = []; noBottle.player.talent = ''; noBottle.player.arts.greenBottleGrade = 0;
  await api('import', { game: JSON.stringify(noBottle) }); await open('cave');
  check(await page.locator('[data-green-bottle-panel]').count() === 0 && !(await page.locator('.cave-page').innerText()).includes('小绿瓶'), '无天赋时绿瓶体系完全不可见');
  const normal = (await api('state')).state;
  check(normal.cave.garden.plots[0].effectiveUnitsPerDay === 60, '无天赋只有洞府自然60日增');
  check(normal.marketRefreshCycleDays === 1080 && normal.marketSaleCountUnlimited === true, '真实状态公布三年账期与取消件数限制');
  check(normal.poison.detoxPerYear === 2, '真实面板自然化毒每年2点');
  const teachers = normal.championTeachers;
  check(new Set(teachers.map(teacher => teacher.sect)).size === 6 && normal.advancedBuilds.length === 8, '常态世界六宗老师和八完整传承齐全');
  await open('arts'); await page.locator('[data-sub="builds"]').click();
  await page.locator('[data-action="art-build-tier"][data-id="advanced"]').click();
  check(await page.locator('.transmission-teachers').count() >= 6, '完整传承显示常态老师位置');
  await page.locator('[data-sub="practice"]').click();
  const manual = page.locator('.manual-card .compact-art-details').first();
  check(await manual.getAttribute('open') === null, '功法详情初始折叠');
  await manual.locator('summary').click(); check(await manual.getAttribute('open') !== null, '点击可展开完整功法属性');
  await page.locator('[data-sub="spells"]').click();
  const spell = page.locator('.compact-spell-card .compact-art-details').first();
  check(await spell.getAttribute('open') === null, '神通数值初始折叠');
  await spell.locator('summary').click(); check(await spell.locator('[data-numeric-preview]').isVisible(), '点击神通展开真实数值');
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px桌面道法界面不横向溢出');
  await page.screenshot({ path: join(temporary, 'compact-arts-850.png'), fullPage: true });
  }
  const tradeState = (await api('state')).state;
  const price = tradeState.pillCatalog.find(item => item.id === '5001').pricing.sellPrice;
  const trade = JSON.parse((await api('export')).game);
  trade.player.arts.potions['5001'] = 100; trade.player.stones = 1000;
  trade.economy.marketCash = price * 100; trade.economy.sold['potion:5001'] = 1000000000;
  await api('import', { game: JSON.stringify(trade) }); await open('inventory');
  const item = page.locator('[data-inventory-item="5001"]');
  await item.locator('.inventory-pill-details > summary').click();
  check(await item.locator('[data-action="sell-max"]').getAttribute('data-count') === '100', '真实行囊报价允许售100枚，不受历史月件数账限制');
  let sold = await action(item.locator('[data-action="sell-max"]'), 'sell');
  check((sold.state.arts.potions['5001'] || 0) === 0 && sold.state.world.marketCash === 0, '真实一键售出100枚并恰扣完本期收购资金');
  const ledger = JSON.parse((await api('export')).game);
  ledger.clock.days = 1079; ledger.year = 2; ledger.player.age = ledger.clock.ageBase + 2;
  ledger.economy.month = 35; ledger.economy.marketCycleIndex = 0; ledger.economy.marketCash = 7;
  ledger.job = null; ledger.pending = null; ledger.plan = 'rest'; ledger.npcs = [];
  await api('import', { game: JSON.stringify(ledger) });
  const beforeDay = (await api('state')).state;
  check(beforeDay.world.marketCash === 7 && beforeDay.marketDaysUntilRefresh === 1, '真实第1079日查询保留7灵石、预报一天后刷新');
  await api('advance', { days: 1 });
  const boundary = (await api('state')).state;
  check(boundary.world.marketCash === boundary.marketBudget && boundary.world.marketCash > 7, '真实推进到1080日发放本期资金');
  const frozen = (await api('export')).game; await api('state'); await api('state');
  check((await api('export')).game === frozen, '账期刷新后重复查询不再次补款');
  check(errors.length === 0, '真实页面流程无脚本错误');
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, timings, errors, serverLog }, null, 2));
  console.log(`SUMMARY ${checks.length} checks, evidence=${temporary}`);
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, timings, errors, serverLog }, null, 2));
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally { if (browser) await browser.close(); server.kill(); }
