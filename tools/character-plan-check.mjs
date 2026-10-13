// Unit contracts by default; --binary PATH also exercises both UIs against an isolated server.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { createServer } from 'node:net';
import { existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { buildRequest, buildKey, lockBuildPlan, matchesBuildPlan,
  renderBuildPortraits, renderOpeningPreview } from '../web/build-preview.mjs';
import { renderCreate } from '../web/v2/views/create.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
let checks = 0;
function check(condition, message) { assert.ok(condition, message); checks++; console.log('PASS ' + message); }
const draft = { name: '  初始修士  ', gender: '女', portraitId: 'rogue-female', seed: 20261013,
  traits: ['alchemy'], allocation: { insight: 2 } };
const preview = { seed: draft.seed, player: { name: '初始修士', apt: 91, insight: 52, stones: 48,
  portrait: '/assets/portraits/rogue-female.webp', talents: [{ name: '炼丹童子', desc: '成功成丹 +1' }] },
  arts: { mods: { brewYield: 1 } }, gears: [] };
const valid = { valid: true, preview, budget: 10, remain: 4 };
const plan = lockBuildPlan(draft, valid);
check(matchesBuildPlan(plan, draft), '锁定保存完整草稿');
check(plan.request.name === '初始修士' && plan.request.seed === draft.seed, '固定种子与规范化道号进入请求');
check(plan.request.traits !== draft.traits && plan.request.allocation !== draft.allocation, '快照不共享可变天赋/属性容器');
for (const patch of [{ name: '改名' }, { gender: '男' }, { portraitId: 'player-female' },
  { seed: 6 }, { traits: [] }, { allocation: { insight: 3 } }]) {
  check(!matchesBuildPlan(plan, { ...draft, ...patch }), '修改 ' + Object.keys(patch)[0] + ' 后旧锁定失效');
}
check(lockBuildPlan(draft, { ...valid, valid: false }) === null, '非法方案不能锁定');
check(lockBuildPlan(draft, { valid: true }) === null, '缺失实际开局预览不能锁定');
check(lockBuildPlan(draft, { ...valid, preview: { ...preview, seed: 7 } }) === null, '其他种子的预览不能锁定');
assert.throws(() => { plan.request.allocation.insight = 99; }, TypeError); checks++;
check(buildKey(draft) === buildKey(buildRequest(draft)), '序列化/复制后方案标识稳定');
const options = { budget: 10, talents: [], stats: [], innate: {}, portraits: [
  { id: 'rogue-female', gender: '女', src: '/assets/portraits/rogue-female.webp', name: '散修' },
  { id: 'player-male', gender: '男', src: '/assets/portraits/player-male.webp', name: '修士' },
] };
const disabled = html => /\sdisabled(?:\s|>|=)/.test(html.match(/<button\b[^>]*data-ui="start-life"[^>]*>/)?.[0] || ' disabled');
check(disabled(renderCreate({ build: draft }, { buildOptions: options, buildCheck: valid })), '有效未锁定草稿仍不能开始');
check(!disabled(renderCreate({ build: draft, buildPlan: plan }, { buildOptions: options, buildCheck: valid })), '锁定有效方案可开始');
check(disabled(renderCreate({ build: draft, buildPlan: plan, buildChecking: true }, { buildOptions: options, buildCheck: valid })), '最终校验中不能重复开始');
check(!renderBuildPortraits(options, 'rogue-female', '女').includes('player-male'), '只显示当前性别立绘');
check(renderOpeningPreview(preview).includes('91') && renderOpeningPreview(preview).includes('成功成丹 +1'), '预览显示权威属性与天赋效果');
const hostile = '<img src=x onerror=alert(1)>';
check(!renderOpeningPreview({ ...preview, player: { ...preview.player, name: hostile } }).includes(hostile), '预览转义姓名');

const args = process.argv.slice(2);
const binaryIndex = args.indexOf('--binary');
if (binaryIndex >= 0) {
  const binary = resolve(root, args[binaryIndex + 1] || '');
  assert.ok(existsSync(binary), '测试需要本轮编译的 ningzhou_web');
  const output = join(root, 'build-character-economy', 'character-qa');
  mkdirSync(output, { recursive: true });
  const temporary = mkdtempSync(join(output, 'isolated-'));
  const require = createRequire(process.execPath);
  const { chromium } = require('playwright');
  const executablePath = process.env.CHROMIUM_PATH || [
    'C:/Program Files/Google/Chrome/Application/chrome.exe',
    'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
  ].find(existsSync);
  const reservation = createServer();
  reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
  const port = reservation.address().port;
  await new Promise(resolve => reservation.close(resolve));
  const base = `http://127.0.0.1:${port}`;
  let server, browser;
  const diagnostics = [];
  try {
    server = spawn(binary, ['--host', '127.0.0.1', '--port', String(port), '--no-open',
      '--web', join(root, 'web'), '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback')],
    { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
    server.stdout.on('data', chunk => diagnostics.push(chunk.toString()));
    server.stderr.on('data', chunk => diagnostics.push(chunk.toString()));
    let ready = false;
    for (let attempt = 0; attempt < 200; attempt++) {
      if (server.exitCode !== null) throw new Error('隔离服务提前退出');
      try { const response = await fetch(base + '/api/mode'); await response.arrayBuffer(); ready = true; break; }
      catch { await new Promise(resolve => setTimeout(resolve, 50)); }
    }
    assert.ok(ready, '隔离服务启动');
    browser = await chromium.launch({ executablePath, headless: true });
    const errors = [];
    for (const [entry, width] of [['classic', 1440], ['classic', 390], ['mobile', 390]]) {
      const context = await browser.newContext({ viewport: { width, height: 900 } });
      const page = await context.newPage();
      page.on('pageerror', error => errors.push(`${entry}: ${error.message}`));
      let lastBuildRequest;
      let creationCount = 0;
      page.on('request', request => {
        if (request.url().endsWith('/api/build-check')) lastBuildRequest = request.postDataJSON();
        if (request.url().endsWith('/api/new')) creationCount++;
      });
      const api = async (path, body) => {
        const response = body === undefined ? await context.request.get(base + '/api/' + path)
          : await context.request.post(base + '/api/' + path, { data: body });
        const result = await response.json();
        assert.ok(response.ok() && !result.error, `${path}: ${result.error || response.status()}`);
        return result;
      };
      await api('login', { nickname: `Plan${entry}${width}`, password: 'isolated-character-test' });
      await page.goto(base + (entry === 'classic' ? '/' : '/v2.html'));
      const nameSelector = entry === 'classic' ? '#bp-name' : '#life-name';
      if (entry === 'classic') {
        // Fresh accounts open the builder asynchronously after state hydration.
        await page.locator('#bp-name').waitFor();
      } else await page.locator('[data-ui="new-life"]').click();
      await page.locator(nameSelector).fill('开局验收');
      const lockSelector = entry === 'classic' ? '#bp-lock' : '[data-ui="build-lock"]';
      const unlockSelector = entry === 'classic' ? '#bp-unlock' : '[data-ui="build-unlock"]';
      const startSelector = entry === 'classic' ? '#bp-start' : '[data-ui="start-life"]';
      const traitSelector = entry === 'classic' ? '[data-talent="alchemy"]' : '[data-ui="build-trait"][data-value="alchemy"]';
      const insightSelector = entry === 'classic' ? '[data-alloc="insight"][data-d="-1"]' : '[data-ui="build-alloc"][data-value="insight"][data-delta="1"]';
      await page.locator(lockSelector + ':not([disabled])').waitFor();
      check(await page.locator(startSelector).isDisabled(), `${entry}/${width}: 未锁定不能开始`);
      const initialSeed = lastBuildRequest.seed;
      await page.locator('[data-build-portrait="lihuo-female"]').click();
      if (entry === 'mobile') {
        const details = page.locator('details').filter({ has: page.locator(traitSelector) });
        if (!await details.getAttribute('open').then(value => value !== null)) await details.locator('summary').click();
      }
      await page.locator(traitSelector).click();
      await page.locator(insightSelector).click();
      await page.locator(lockSelector + ':not([disabled])').waitFor();
      await page.locator(lockSelector).click();
      await page.locator('.opening-preview').waitFor();
      check(await page.locator(nameSelector).isDisabled() && await page.locator(insightSelector).isDisabled(), `${entry}/${width}: 锁定后输入和加点不可改`);
      check(await page.locator('[data-build-portrait="lihuo-female"]').isDisabled(), `${entry}/${width}: 立绘随方案锁定`);
      check(lastBuildRequest.seed === initialSeed, `${entry}/${width}: 选择天赋与加点不重掷资质悟性`);
      const lockedCheck = await api('build-check', lastBuildRequest);
      const alchemyTalent = lockedCheck.preview.player.talents.find(talent => talent.id === 'alchemy');
      check(alchemyTalent && (await page.locator('.opening-preview').innerText()).includes(alchemyTalent.name), `${entry}/${width}: 预览天赋实际说明`);
      await page.locator(unlockSelector).click();
      check(await page.locator(startSelector).isDisabled(), `${entry}/${width}: 解锁立即取消开始资格`);
      await page.locator(insightSelector).click();
      await page.locator(lockSelector + ':not([disabled])').waitFor();
      await page.locator(lockSelector).click();
      await page.locator('.opening-preview').waitFor();
      const submitted = structuredClone(lastBuildRequest);
      const expected = await api('build-check', submitted);
      check(expected.valid && submitted.allocation.insight === 2, `${entry}/${width}: 再锁定使用最新属性`);
      check(expected.preview.player.portraitId === 'lihuo-female', `${entry}/${width}: 后端确认选中立绘`);
      check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `${entry}/${width}: 无横向溢出`);
      await page.screenshot({ path: join(output, `${entry}-${width}-locked.png`), fullPage: true });
      await page.locator(startSelector).click();
      await page.locator('.opening-preview').waitFor({ state: 'detached' });
      const result = await api('state');
      const player = result.state.player;
      for (const key of ['name', 'gender', 'portraitId', 'portrait', 'apt', 'insight', 'luck', 'stones', 'alchemy', 'body'])
        assert.deepEqual(player[key], expected.preview.player[key], `${entry}/${width}: ${key} 预览=开局`);
      checks += 10;
      check(creationCount === 1, `${entry}/${width}: 只创建一次人生`);
      await page.reload();
      const reloaded = await api('state');
      check(reloaded.state.player.portraitId === 'lihuo-female', `${entry}/${width}: 刷新仍保留所选立绘`);
      if (entry === 'mobile') {
        await page.locator('.home-control').waitFor();
        await page.locator('[data-ui="character"]:visible').first().click();
        const portrait = page.locator('.character-profile img');
        await portrait.waitFor();
        check(await portrait.getAttribute('src') === expected.preview.player.portrait,
          '手机人物详情显示本次所选立绘');
        await portrait.evaluate(element => element.decode());
        check(await portrait.evaluate(element => element.naturalWidth > 0), '已选立绘资源可正常加载');
        await page.screenshot({ path: join(output, 'mobile-character-portrait.png'), fullPage: true });
      }
      await context.close();
    }
    check(errors.length === 0, '经典与手机建角无脚本异常');
  } finally {
    await browser?.close();
    if (server && server.exitCode === null) {
      const closed = once(server, 'exit');
      server.kill();
      await closed;
    }
    writeFileSync(join(output, 'server.log'), diagnostics.join(''), 'utf8');
  }
}
console.log(`Character plan: ${checks} checks passed.`);
