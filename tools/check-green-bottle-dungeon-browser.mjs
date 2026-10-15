// 小绿瓶固定难度与完整升阶材料：真实完整服务、临时端口和角色；不使用现有服务或玩家存档。
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
const executableInput = resolve(root, args.includes('--exe') ? args[args.indexOf('--exe') + 1] : 'build-vs-share/Release/ningzhou_web.exe');
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = join(root, 'build/diagnostics/green-bottle-dungeon-browser-20261015');
mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const executable = join(temporary, 'green-bottle-dungeon-qa.exe');
copyFileSync(executableInput, executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
const child = spawn(executable, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', join(root, 'web'),
  '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')], { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const exited = new Promise(done => child.once('exit', done));
const checks = [], timings = [], errors = [], serverLog = [], posts = [];
const report = { ok: false, executableInput, port, temporary, checks, timings, errors, serverLog,
  scope: '真实入口、对手难度、UI、预览、存读与离境；终点领奖使用正规导入的合法已击败首领夹具，不代表自然战斗胜率。' };
child.stdout.on('data', chunk => serverLog.push(String(chunk))); child.stderr.on('data', chunk => serverLog.push(String(chunk)));
child.on('exit', (code, signal) => { report.serverExit = { code, signal }; });
let browser, page, context;
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
// 用途：记录真实断言；流程：本轮独立浏览器与API验证；返回：无，失败立即抛错保存证据。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }

try {
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { ready = (await fetch(base + '/api/mode')).ok; } catch { /* 等待本工具创建的服务。 */ }
    if (ready) break;
    await pause(50);
  }
  check(ready, '隔离完整游戏服务启动');
  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  page = await context.newPage(); page.setDefaultTimeout(15000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') posts.push(new URL(request.url()).pathname); });
  // 用途：调用临时角色的实际动作；流程：建角、导入与结果核对；返回：真实JSON，允许调用方显式检查领域拒绝。
  async function request(path, body = {}, allowedFailure = false) {
    await pause(90);
    const response = await context.request.post(base + '/api/' + path, { data: body });
    const result = await response.json();
    if (!allowedFailure) assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`);
    return { response, result };
  }
  async function api(path, body = {}) { return (await request(path, body)).result; }
  // 用途：在夹具导入后打开真实页面；流程：洞府与秘境展示；返回：已解除忙碌的DOM。
  async function open(hash) {
    await page.goto(`${base}/?qa=${Date.now()}#${hash}`, { waitUntil: 'domcontentloaded' });
    await page.locator('.main-tabs').waitFor();
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
  }
  // 用途：执行真实按钮并计时；流程：双击防重与入口打开；返回：对应动作的权威状态。
  async function action(locator, path, twice = false) {
    const start = performance.now();
    const pending = page.waitForResponse(response => new URL(response.url()).pathname === `/api/${path}` && response.request().method() === 'POST');
    await (twice ? locator.evaluate(button => { button.click(); button.click(); }) : locator.click());
    const response = await pending, result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, result.error || path);
    await page.waitForFunction(() => document.body.getAttribute('aria-busy') !== 'true');
    timings.push({ path, milliseconds: performance.now() - start });
    return result;
  }
  await api('login', { nickname: `BottleFix${Date.now().toString(36).slice(-7)}`, password: 'isolated-bottle-fix' });
  await api('new', { name: '绿瓶秘境验收', gender: '男', portraitId: 'player-male', seed: 71377, traits: ['greenBottle'], allocation: {} });
  const original = JSON.parse((await api('export')).game);
  // 用途：建立独立洞府夹具；流程：各阶入口与边界；返回：合法候选存档，仍经正常import校验。
  function fixture(stage = 12, bottleGrade = 5) {
    const game = structuredClone(original);
    game.pending = null; game.job = null; game.npcs = [];
    game.player.stage = game.player.highestStage = stage; game.player.xp = 0; game.player.lifespan = 2000;
    game.player.cave = game.player.location = 'wuling'; game.player.caveTier = 2; game.player.qiSupply = 0;
    game.player.renown = 100; game.player.stones = 1000000; game.player.health = 100;
    game.player.arts.greenBottleGrade = bottleGrade; game.player.arts.battle = null;
    return game;
  }
  // 用途：形成沿合法连线已击败首领的测试快照；流程：真实领奖/离境与战利品名称验证；返回：仍需正规import校验的候选档。
  function completedFixture(entrySave, count) {
    const completed = structuredClone(entrySave), finalRun = completed.dungeons.run;
    for (const node of finalRun.nodes) node.visited = false;
    let node = finalRun.nodes.find(entry => entry.row === 0 && entry.lane === 1); assert.ok(node);
    while (true) { node.visited = true; if (!node.next.length) break; node = finalRun.nodes.find(entry => entry.id === node.next[0]); assert.ok(node); }
    finalRun.current = node.id; finalRun.row = node.row; finalRun.phase = 'reward'; finalRun.cleared = true;
    finalRun.elapsedDays = 1; finalRun.targetGranted = true; finalRun.materials[finalRun.targetId] = count;
    finalRun.rewards = structuredClone(finalRun.bossRewards); completed.dungeons.sites.find(entry => entry.id === finalRun.siteId).cleared = true;
    return completed;
  }
  if (args.includes('--mode') && args[args.indexOf('--mode') + 1] === 'loot-name') {
    await api('import', { game: JSON.stringify(fixture()) });
    await api('cave/green-bottle/dungeon', { grade: 6, expectedGrade: 5 });
    const completed = completedFixture(JSON.parse((await api('export')).game), 200);
    await api('import', { game: JSON.stringify(completed) });
    const material = (await api('state')).state.dungeons.run.lootItems.find(item => item.id === 'bottle-spirit-6');
    check(material?.name === '造化碧玉' && material.count === 200, '实际API专属灵材lootItems.name读取权威中文名并保留200数量');
    await open('dungeon');
    const lootText = await page.locator('.dungeon-loot').innerText();
    check(lootText.includes('造化碧玉 ×200') && !lootText.includes('bottle-spirit-6'), '真实战利品包显示中文专属灵材及数量，不暴露裸ID');
    await page.locator('.dungeon-loot').screenshot({ path: join(temporary, 'bottle-loot-name.png') });
  } else {
  const early = fixture(0, 3); early.player.arts.potions['5002'] = 1;
  await api('import', { game: JSON.stringify(early) });
  const earlyState = (await api('state')).state;
  check(earlyState.player.xpNeed === 1100, '炼气初期真实修为门槛由110扩大为1100');
  const cultivationFactor = earlyState.arts.mods.cult > 0 ? earlyState.arts.mods.cult : 1;
  check(Math.abs(earlyState.player.cultivation.manual - 180 * cultivationFactor) < 0.000001,
    '正常年度功法基础修炼使用十倍单位，其他既有乘数保持');
  const pill = earlyState.pillCatalog.find(entry => entry.id === '5002');
  check(pill.effectValue === 28 && pill.baseEffect.includes('本小境单次上限 28') && !pill.baseEffect.includes('25%'),
    '修为丹当前吸收仍为28，说明显示数值上限而非错误百分比');
  await open('inventory');
  const pillItem = page.locator('[data-inventory-item="5002"]');
  await pillItem.locator('.inventory-pill-details > summary').click();
  const pillText = await pillItem.innerText();
  check(/本小境单次上限\s*[（(]?\s*28/.test(pillText) && !pillText.includes('25%'), '真实行囊服丹面板同步显示数值吸收上限');
  check((await page.locator('.progress-note').innerText()).replace(/[\s,]/g, '').includes('0/1100'), '真实人物侧栏显示新修为门槛1100');
  const consumed = await action(pillItem.locator('[data-action="use-pill"]'), 'use-pill');
  check(consumed.state.player.xp === 28 && (consumed.state.arts.potions['5002'] || 0) === 0,
    '真实点击服丹仍只增加28修为并消耗一枚，不乘正常修炼倍率');
  await page.screenshot({ path: join(temporary, 'cultivation-pill-28.png'), fullPage: true });
  await api('import', { game: JSON.stringify(fixture()) });
  const expected = [
    { grade: 4, stage: 6, realm: '金丹初期', count: 50 },
    { grade: 5, stage: 9, realm: '元婴初期', count: 100 },
    { grade: 6, stage: 12, realm: '化神', count: 200 },
  ];
  let current = (await api('state')).state;
  for (const item of expected) {
    const portal = current.cave.greenBottle.dungeons.find(entry => entry.grade === item.grade);
    check(portal?.enabled && portal.stage === item.stage && portal.realmName === item.realm && portal.rewardCount === item.count,
      `${item.grade}阶入口报价固定${item.realm}且通关${item.count}份`);
  }
  const frozen = (await api('export')).game; await api('state'); await api('state');
  check((await api('export')).game === frozen, '入口查询不生成秘境、不扣款、不推进随机数或日期');
  await open('cave'); await page.locator('[data-green-bottle-dungeons] summary').click();
  const caveText = await page.locator('[data-green-bottle-dungeons]').innerText();
  check(expected.every(item => caveText.includes(`固定难度：${item.realm}`) && caveText.includes(`×${item.count}`)), '真实洞府展示三档固定难度和足量升阶材料');
  await page.screenshot({ path: join(temporary, 'bottle-portals-1440.png'), fullPage: true });
  await page.setViewportSize({ width: 850, height: 1000 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '850px洞府入口无横向溢出');
  await page.screenshot({ path: join(temporary, 'bottle-portals-850.png'), fullPage: true });
  await page.setViewportSize({ width: 1440, height: 1000 });

  await api('import', { game: JSON.stringify(fixture(0, 3)) }); await open('cave');
  await page.locator('[data-green-bottle-dungeons] summary').click();
  const lowPortal = (await api('state')).state.cave.greenBottle.dungeons.find(entry => entry.grade === 4);
  check(lowPortal?.stage === 6 && !lowPortal.enabled && lowPortal.reason.includes('金丹'), '低境可查看已瓶阶解锁入口但明确提示金丹门槛');
  check(await page.locator('[data-action="green-bottle-dungeon"][data-material-grade="4"]').isDisabled(), '真实低境入口禁用');
  const beforeRejected = (await api('export')).game;
  const rejected = await request('cave/green-bottle/dungeon', { grade: 4, expectedGrade: 3 }, true);
  check(!rejected.response.ok() && (await api('export')).game === beforeRejected, '绕过UI的低境开境也拒绝且不改存档');

  for (const item of expected) {
    await api('import', { game: JSON.stringify(fixture(12, item.grade - 1)) });
    let entered;
    if (item.grade === 4) {
      await open('cave'); await page.locator('[data-green-bottle-dungeons] summary').click();
      const postBefore = posts.filter(path => path === '/api/cave/green-bottle/dungeon').length;
      entered = await action(page.locator('[data-action="green-bottle-dungeon"][data-material-grade="4"]'), 'cave/green-bottle/dungeon', true);
      check(posts.filter(path => path === '/api/cave/green-bottle/dungeon').length === postBefore + 1, '双击开境仅发一次POST');
    } else entered = await api('cave/green-bottle/dungeon', { grade: item.grade, expectedGrade: item.grade - 1 });
    const run = entered.state.dungeons.run;
    const site = entered.state.dungeons.sites.find(entry => entry.id === run.siteId);
    check(site.stage === item.stage && run.stage === 12 && run.difficultyStage === item.stage && run.difficultyRealmName === item.realm,
      `${item.grade}阶保留玩家牌组境界而固定实际秘境难度`);
    check(run.targetRewardCount === item.count && site.targetRewardCount === item.count
      && run.nodes.length === (9 + item.stage / 3 - 1) * 3 + 1, `${item.grade}阶冻结足量保底与固定难度路线层数`);
    const entrySave = JSON.parse((await api('export')).game);
    await api('import', { game: JSON.stringify(entrySave) });
    check((await api('state')).state.dungeons.run.targetRewardCount === item.count, `${item.grade}阶在途存读保留冻结保底`);
    await open('dungeon');
    check((await page.locator('.dungeon-run-heading .dungeon-eyebrow').innerText()).includes(item.realm)
      && (await page.locator('.dungeon-run-target').innerText()).includes(`×${item.count}`), `${item.grade}阶真实探索页展示固定难度与保底数量`);
    if (item.grade === 4) {
      const combat = run.nodes.find(node => node.available && ['battle', 'elite'].includes(node.type)); assert.ok(combat);
      const battleReply = await api('dungeon/choose', { id: combat.id });
      check(battleReply.state.battle?.enemyStage === item.stage, '化神玩家进入四阶瓶境普通对手也固定金丹初期，不降低小境界');
      await api('import', { game: JSON.stringify(entrySave) });
    }
    await api('dungeon/leave');
    check(!((await api('state')).state.arts.materials[`bottle-spirit-${item.grade}`] > 0), `${item.grade}阶未通关退出不提前发灵材`);

    // 仅注入“沿合法路线击败首领”的领域快照，真实结算仍经API；此项不声称玩家自然实战通关。
    const completed = completedFixture(entrySave, item.count), finalRun = completed.dungeons.run;
    await api('import', { game: JSON.stringify(completed) });
    const targetLoot = (await api('state')).state.dungeons.run.lootItems.find(entry => entry.id === finalRun.targetId);
    check(targetLoot?.name === finalRun.targetName && targetLoot.count === item.count, `${item.grade}阶API战利品名称与权威灵材名一致`);
    const valid = (await api('export')).game;
    const corrupt = structuredClone(completed); corrupt.dungeons.run.materials[`bottle-spirit-${item.grade}`]++;
    const badImport = await request('import', { game: JSON.stringify(corrupt) }, true);
    check(!badImport.response.ok() && (await api('export')).game === valid, `${item.grade}阶伪造多一份灵材被拒且原档不变`);
    const stoneReward = finalRun.bossRewards.find(reward => reward.kind === 'stones'); assert.ok(stoneReward);
    await api('dungeon/claim', { id: stoneReward.id }); await open('dungeon');
    check((await page.locator('.dungeon-complete').innerText()).includes(`×${item.count}`), `${item.grade}阶通关结算显示冻结完整数量`);
    if (item.grade === 6) await page.screenshot({ path: join(temporary, 'bottle-complete-200.png'), fullPage: true });
    const returned = await api('dungeon/leave');
    check(returned.state.arts.materials[`bottle-spirit-${item.grade}`] === item.count, `${item.grade}阶真实离境完整返还${item.count}份`);
    check(returned.state.cave.greenBottle.upgrade.enabled
      && returned.state.cave.greenBottle.upgrade.materials[0].required === item.count,
      `${item.grade}阶一次通关灵材恰好满足升阶，不需额外探索`);
    const upgraded = await api('cave/green-bottle/upgrade', { expectedGrade: item.grade - 1 });
    check(upgraded.state.cave.greenBottle.currentGrade === item.grade
      && upgraded.state.arts.materials[`bottle-spirit-${item.grade}`] === 0, `${item.grade}阶真实升级恰扣通关灵材`);
  }
  }
  check(errors.length === 0, '真实页面流程没有脚本异常');
  report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close();
  if (child.exitCode == null) { child.kill(); await Promise.race([exited, pause(5000)]); }
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, port, evidence: temporary }, null, 2));
}
