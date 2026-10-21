// 完整服发行包专项验收：必须传本轮release.json；从ZIP新解压后启动，禁止以源码web或旧包代替发行物。
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, statSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const workspace = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2);
assert.ok(args.includes('--release') && args[args.indexOf('--release') + 1], '必须显式传 --release 本轮release.json，不能默认检查旧包');
const releasePath = resolve(workspace, args[args.indexOf('--release') + 1]);
const release = JSON.parse(readFileSync(releasePath, 'utf8'));
assert.equal(release.mode, 'full-server');
const require = createRequire(import.meta.url);
const { chromium } = require('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const output = join(workspace, 'build/diagnostics/full-server-folder-browser-20261017'); mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'run-'));
const extractRoot = join(temporary, '完整服解压 中文目录'); mkdirSync(extractRoot);
const checks = [], timings = [], errors = [], serverLog = [], assets = [], musicResponses = [], imageResponses = [];
const report = { ok: false, releasePath, zip: release.zip, zipSha256: release.sha256, temporary,
  checks, timings, errors, serverLog, assets, musicResponses, imageResponses,
  scope: 'ZIP完整性/清单、最新单状态削层脚本及工坊清浊5/破护1真实文案、玄道男女高低特效目录与真实建角、五曲实际播放、舆图图片/炼器SVG、桌面注册保存重启续玩与390手机；不重跑战斗规则全量、外网联机或读取玩家服务。' };
const pause = milliseconds => new Promise(done => setTimeout(done, milliseconds));
let packageRoot, child, exited, browser, context, page, base, port;

// 用途：记录本轮发行断言；流程：解压和真实浏览器冒烟；返回：无，失败保留证据。
function check(condition, message) { assert.ok(condition, message); checks.push(message); console.log(`PASS ${message}`); }
// 用途：计算发行文件哈希；流程：ZIP/清单及关键资产比对；返回：大写SHA256。
function hash(path) { return createHash('sha256').update(readFileSync(path)).digest('hex').toUpperCase(); }
// 用途：枚举新解压包文件；流程：排除夹带数据及清单完整性；返回：包内相对路径，不跟随符号链接。
function files(directory, root = directory) {
  return readdirSync(directory, { withFileTypes: true }).flatMap(entry => {
    const path = join(directory, entry.name); assert.ok(!entry.isSymbolicLink(), `发行物不能包含链接：${path}`);
    return entry.isDirectory() ? files(path, root) : [relative(root, path).replaceAll('\\', '/')];
  });
}
// 用途：以字面量传递PowerShell路径；流程：只向全新目录解压；返回：单引号引用字符串，避免路径被解释成命令。
function quotePowerShell(value) { return `'${String(value).replaceAll("'", "''")}'`; }
// 用途：从解压目录启动完整模式；流程：首次运行及重启；返回：无，仅启动本工具拥有的随机端口服务。
async function start() {
  child = spawn(join(packageRoot, 'ningzhou_web.exe'), ['--host', '127.0.0.1', '--port', String(port), '--no-open',
    '--web', 'web', '--data', 'saves', '--feedback', 'feedback'], { cwd: packageRoot, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  exited = new Promise(done => child.once('exit', done));
  child.stdout.on('data', value => serverLog.push(String(value))); child.stderr.on('data', value => serverLog.push(String(value)));
  for (let attempt = 0; attempt < 240; attempt++) {
    try { if ((await fetch(base + '/api/mode')).ok) return; } catch { /* 等待本工具的完整服务。 */ }
    if (child.exitCode != null) throw new Error(`发行EXE提前退出 ${child.exitCode}`);
    await pause(50);
  }
  throw new Error('发行完整服务未就绪');
}
// 用途：只回收本工具进程；流程：重启和finally；返回：无，不枚举或终止现有玩家服务。
async function stop() {
  if (child && child.exitCode == null) { child.kill(); await Promise.race([exited, pause(5000)]); }
}

try {
  check(hash(release.zip) === release.sha256.toUpperCase() && statSync(release.zip).size === release.size, '本轮ZIP大小和SHA256与release.json一致');
  const extraction = spawnSync('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe', ['-NoLogo', '-NoProfile', '-NonInteractive', '-Command',
    `Expand-Archive -LiteralPath ${quotePowerShell(release.zip)} -DestinationPath ${quotePowerShell(extractRoot)}`], { windowsHide: true, encoding: 'utf8' });
  assert.equal(extraction.status, 0, extraction.stderr || extraction.stdout || 'ZIP解压失败');
  const topLevel = readdirSync(extractRoot, { withFileTypes: true });
  check(topLevel.length === 1 && topLevel[0].isDirectory(), 'ZIP从新临时目录解压为独立完整文件夹');
  packageRoot = join(extractRoot, topLevel[0].name); report.packageRoot = packageRoot;
  const manifest = JSON.parse(readFileSync(join(release.buildRoot, 'manifest.json'), 'utf8'));
  const actualFiles = files(packageRoot);
  check(manifest.length === release.files && actualFiles.length === manifest.length
    && actualFiles.every(path => manifest.some(item => item.path === path)), '解压文件集合与本轮manifest完全一致，无额外夹带文件');
  for (const item of manifest) {
    const file = resolve(packageRoot, item.path);
    assert.ok(file.startsWith(packageRoot + '\\') || file.startsWith(packageRoot + '/'), `清单路径越界：${item.path}`);
    assert.ok(existsSync(file) && statSync(file).size === item.length && hash(file) === item.sha256.toUpperCase(), `清单内容不匹配：${item.path}`);
  }
  report.manifestFiles = manifest.length;
  check(true, `清单${manifest.length}个文件的长度及哈希逐项一致`);
  check(!actualFiles.some(path => /(^|\/)(saves|players|feedback|node_modules|\.git)(\/|$)|(^|\/)battle-lab\./i.test(path)), '发行包不含存档、账号数据、意见箱、依赖目录或独立斗法实验页');
  check(hash(join(packageRoot, 'ningzhou_web.exe')) === release.gameSha256.toUpperCase(), '包内EXE与本轮Release记录哈希一致');
  const startup = readFileSync(join(packageRoot, '启动完整服务器.cmd'), 'ascii');
  check(startup.includes('--web web --data saves --feedback feedback') && !startup.includes('--local'), '随包启动器保持注册登录完整模式及相对资源布局');
  assert.ok(Number.isInteger(release.port) && release.port >= 1 && release.port <= 65535, '本轮release.json必须明确记录发行端口');
  for (const name of ['启动完整服务器.cmd', '启动局域网服务器.cmd']) {
    const script = readFileSync(join(packageRoot, name), 'ascii');
    check(script.includes(`--port ${release.port} `) && script.includes(`http://127.0.0.1:${release.port}/`)
      && script.includes('Get-NetTCPConnection') && /exit\s+\/b\s+1/i.test(script), `${name}使用本轮${release.port}端口并在占用时拒绝启动旧页面`);
  }
  const versionInfo = readFileSync(join(packageRoot, '版本信息.txt'), 'utf8');
  check(versionInfo.includes(`127.0.0.1:${release.port}/`) && versionInfo.includes(release.gameSha256)
    && versionInfo.includes('清浊') && versionInfo.includes('周身五行灵光'), '包内版本说明记录本轮入口、EXE哈希及新功能');
  for (const path of ['web/dungeon-card-preferences.mjs', 'web/app.mjs', 'web/artifact-status.mjs',
    'web/v2/app.mjs', 'web/v2/views/battle.mjs', 'web/forge.mjs',
    'web/music-controls.mjs', 'web/music-controls.css', 'web/weather-music.mjs',
    ...['sunny', 'rain', 'wind', 'mist', 'clouds'].map(id => `web/assets/music/${id}.mp3`),
    'web/assets/portraits/sect-xuandao-male-v1.webp', 'web/assets/portraits/sect-xuandao-female-v1.webp',
    'web/assets/portraits/sect-xuandao-male-effects-v1.webp', 'web/assets/portraits/sect-xuandao-female-effects-v1.webp',
    'web/assets/map/ningzhou-atlas.webp']) {
    const packagedHash = hash(join(packageRoot, path));
    check(packagedHash === hash(join(workspace, path)), `包内${path}与本轮冻结源码/成品哈希一致`);
    assets.push({ path, sha256: packagedHash, bytes: statSync(join(packageRoot, path)).size });
  }
  const cardModule = readFileSync(join(packageRoot, 'web/dungeon-card-preferences.mjs'), 'utf8');
  check(cardModule.includes('type="radio"') && cardModule.includes('指定派系（单选）')
    && cardModule.includes('原多系奖励偏好已重置为全部') && !cardModule.includes('type="checkbox"'), '包内为单系互斥及旧多系明确重置的新界面脚本');

  const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
  port = reservation.address().port; await new Promise(done => reservation.close(done));
  assert.ok(port > 8120, '随机端口不占用开发预览端口'); base = `http://127.0.0.1:${port}`; report.port = port;
  await start();
  const mode = await (await fetch(base + '/api/mode')).json();
  check(mode.ok && mode.local === false, '解压EXE从相对web和默认saves目录成功启动完整服');
  check((await fetch(base + '/api/state')).status === 401, '未登录访问状态被拒绝，发行包没有免注册旁路');
  check((await fetch(base + '/battle-lab.html')).status === 404, '独立斗法实验页未随发行网页暴露');
  for (const path of ['artifact-status.mjs', 'assets/portraits/sect-xuandao-male-effects-v1.webp', 'assets/portraits/sect-xuandao-female-effects-v1.webp']) {
    const response = await fetch(base + '/' + path), buffer = Buffer.from(await response.arrayBuffer());
    check(response.ok && createHash('sha256').update(buffer).digest('hex').toUpperCase() === hash(join(packageRoot, 'web', path)), `实际HTTP响应${path}与解压文件哈希一致`);
  }

  browser = await chromium.launch({ executablePath: 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', headless: true });
  const nickname = `ReleaseQa${Date.now().toString(36).slice(-6)}`, password = 'isolated-release-qa';
  // 用途：创建隔离浏览器上下文并记录真实媒体和图片请求；流程：桌面与手机共用；返回：无。
  async function openContext(viewport) {
    context = await browser.newContext({ viewport, locale: 'zh-CN', reducedMotion: 'reduce' });
    await context.addInitScript(() => {
      const media = new Set(), failures = [];
      window.__fullServerMusicQA = { media, failures };
      const actualPlay = HTMLMediaElement.prototype.play;
      HTMLMediaElement.prototype.play = function(...argumentsList) {
        if (!media.has(this)) {
          media.add(this);
          this.addEventListener('error', () => failures.push({ src: this.currentSrc || this.src, code: this.error?.code, message: this.error?.message }));
        }
        const result = actualPlay.apply(this, argumentsList);
        result?.catch(error => failures.push({ src: this.currentSrc || this.src, name: error.name, message: error.message }));
        return result;
      };
    });
    page = await context.newPage(); page.setDefaultTimeout(20000); page.on('pageerror', error => errors.push(error.message));
    page.on('response', response => {
      const path = new URL(response.url()).pathname;
      if (/^\/assets\/music\/[^/]+\.mp3$/.test(path)) musicResponses.push({ path, status: response.status() });
      if (/\.(webp|png|jpe?g|svg)$/.test(path)) imageResponses.push({ path, status: response.status() });
    });
  }
  // 用途：建立无历史Cookie的真实Edge上下文；流程：注册及进程重启后登录；返回：成功登录响应。
  async function openLogin() {
    await openContext({ width: 1440, height: 1000 });
    await page.goto(base + '/?release-qa=' + Date.now(), { waitUntil: 'domcontentloaded' });
    await page.locator('#nz-login').waitFor({ state: 'visible' });
    await page.locator('#nz-login [name="nickname"]').fill(nickname);
    await page.locator('#nz-login [name="password"]').fill(password);
    const started = performance.now();
    const pending = page.waitForResponse(response => new URL(response.url()).pathname === '/api/login' && response.request().method() === 'POST');
    await page.locator('#nz-login button[type="submit"]').click();
    const response = await pending, result = await response.json();
    assert.ok(response.ok() && result.ok, result.error || '完整服登录失败');
    await page.locator('#nz-login').waitFor({ state: 'hidden' });
    timings.push({ action: 'login', milliseconds: performance.now() - started });
    return result;
  }
  // 用途：读取临时账号权威状态；流程：保存续玩比较；返回：成功JSON，不产生角色动作。
  async function api(path, body = {}) {
    await pause(90);
    const response = await context.request.post(base + '/api/' + path, { data: body }); const result = await response.json();
    assert.ok(response.ok() && result.ok !== false && !result.error, `${path}: ${result.error || response.status()}`); return result;
  }
  const registration = await openLogin();
  check(registration.hasGame === false, '真实Edge新道号注册成功且没有预装角色存档');
  await page.locator('.build-panel').waitFor();
  await page.locator('#bp-name').fill('完整包验收');
  await page.locator('[data-gender="男"]').click();
  await page.waitForFunction(() => document.querySelector('#bp-lock')?.disabled === false);
  check(await page.locator('[data-build-portrait="xuandao-male-v1"]').count() === 1
    && await page.locator('[data-build-portrait]').count() === 15, '桌面创建目录保留玄道低特效男版且该性别共15幅');
  const portrait = page.locator('[data-build-portrait="xuandao-male-effects-v1"]');
  await portrait.scrollIntoViewIfNeeded(); await portrait.click();
  await page.waitForFunction(() => document.querySelector('#bp-lock')?.disabled === false);
  await page.locator('[data-build-portrait="xuandao-male-effects-v1"] img').evaluate(image => image.decode());
  check(await page.locator('[data-build-portrait="xuandao-male-effects-v1"]').getAttribute('aria-pressed') === 'true', '建角界面可真实选择并解码包内玄道高特效男立绘');
  const decoded = await page.evaluate(async () => Promise.all(['male', 'female'].flatMap(gender => [false, true].map(async effects => {
    const image = new Image(); image.src = `/assets/portraits/sect-xuandao-${gender}-${effects ? 'effects-' : ''}v1.webp`; await image.decode();
    return { gender, effects, width: image.naturalWidth, height: image.naturalHeight };
  }))));
  check(decoded.every(image => image.width > 0 && image.height > 0), '完整服浏览器实际解码玄道男女高低特效四张WebP'); report.decodedPortraits = decoded;
  await page.waitForFunction(() => document.querySelector('#bp-lock')?.disabled === false);
  await page.locator('#bp-lock').click();
  await page.waitForFunction(() => document.querySelector('#bp-start')?.disabled === false);
  const started = performance.now();
  const newResponse = page.waitForResponse(response => new URL(response.url()).pathname === '/api/new' && response.request().method() === 'POST');
  await page.locator('#bp-start').click();
  const created = await (await newResponse).json();
  await page.locator('.build-panel').waitFor({ state: 'detached' });
  check(created.ok && created.state.player.name === '完整包验收'
    && created.state.player.portrait.includes('sect-xuandao-male-effects-v1.webp'), '真实锁定方案并开局，完整服创建带高特效玄道立绘的角色');
  timings.push({ action: 'new', milliseconds: performance.now() - started });
  await page.locator('[data-music-open]').first().click();
  await page.locator('#music-dialog[open]').waitFor();
  check(!await page.locator('[data-music-enabled]').isChecked() && musicResponses.length === 0, '完整服新浏览器默认关闭音乐且未下载音源');
  check(await page.locator('[data-music-mode] option').count() === 6, '完整服音乐面板包含自动模式与五首白名单曲目');
  await page.locator('[data-music-mode]').selectOption('sunny');
  const musicPlayback = []; report.musicPlayback = musicPlayback;
  for (const id of ['sunny', 'rain', 'wind', 'mist', 'clouds']) {
    const musicStarted = performance.now();
    if (id === 'sunny') await page.locator('[data-music-enabled]').check();
    else await page.locator('[data-music-mode]').selectOption(id);
    await page.waitForFunction(trackId => [...window.__fullServerMusicQA.media].some(audio => audio.src.endsWith(`/assets/music/${trackId}.mp3`)
      && !audio.paused && audio.currentTime > 0.15 && audio.readyState >= 2 && !audio.error), id);
    const playback = await page.evaluate(trackId => {
      const audio = [...window.__fullServerMusicQA.media].find(candidate => candidate.src.endsWith(`/assets/music/${trackId}.mp3`) && !candidate.paused);
      return { id: trackId, currentTime: audio.currentTime, duration: audio.duration, readyState: audio.readyState };
    }, id);
    musicPlayback.push(playback);
    timings.push({ action: `music-${id}`, milliseconds: performance.now() - musicStarted });
    check((await page.locator('[data-music-status]').innerText()).includes('正在播放')
      && musicResponses.some(response => response.path === `/assets/music/${id}.mp3` && [200, 206].includes(response.status)), `完整服真实点击后解码播放包内${id}.mp3，音源响应成功`);
  }
  report.musicFailures = await page.evaluate(() => window.__fullServerMusicQA.failures);
  check(report.musicFailures.length === 0 && musicResponses.every(response => [200, 206].includes(response.status))
    && await page.locator('[data-music-error]').isHidden(), '完整服五曲播放无媒体失败或音源HTTP错误');
  await page.screenshot({ path: join(temporary, 'fresh-server-music.png'), fullPage: true });
  await page.locator('[data-music-enabled]').uncheck();
  check(await page.evaluate(() => [...window.__fullServerMusicQA.media].every(audio => audio.paused)), '完整服关闭音乐立即停止所有音频');
  await page.locator('[data-music-close]').click();
  await page.locator('.main-tabs [data-tab="dungeon"]').click();
  await page.locator('[data-dungeon-card-mode]').selectOption('selected');
  await page.locator('[data-dungeon-card-dao][value="metal"]').check();
  await page.locator('[data-dungeon-card-dao][value="water"]').check();
  check(await page.locator('[data-dungeon-card-dao]:checked').count() === 1
    && await page.locator('[data-dungeon-card-dao][value="water"]').isChecked(), '发行包真实页面只允许单系奖励选择');
  await page.screenshot({ path: join(temporary, 'fresh-server-single-dao.png'), fullPage: true });
  await page.locator('.toolbar [data-action="settings"]').click();
  const savedResponse = page.waitForResponse(response => new URL(response.url()).pathname === '/api/save' && response.request().method() === 'POST');
  await page.locator('#settings-modal [data-action="save"][data-slot="1"]').click();
  const saved = await (await savedResponse).json(); check(saved.ok, '真实点击完整服存档位1保存成功');
  const beforeRestart = (await api('export')).game;
  check(files(join(packageRoot, 'saves')).some(path => /players\/[^/]+\/slot1\.json$/.test(path)), '首次游玩数据只在新解压目录saves/players生成');
  await context.close(); await stop(); await start();
  const resumedLogin = await openLogin();
  await page.locator('.main-tabs').waitFor();
  check(resumedLogin.hasGame === true && (await api('export')).game === beforeRestart, '完整EXE重启后新Cookie登录可完整续玩，不重生成角色或世界');
  check((await api('state')).state.player.portrait.includes('sect-xuandao-male-effects-v1.webp'), '重启后已选高特效玄道外观保留');
  await page.screenshot({ path: join(temporary, 'server-restarted-resumed.png'), fullPage: true });
  await page.locator('.main-tabs [data-tab="map"]').click();
  await page.locator('.atlas-art').waitFor(); await page.locator('.atlas-art').evaluate(image => image.decode());
  check(await page.locator('.atlas-art').evaluate(image => image.naturalWidth > 0 && image.naturalHeight > 0), '包内宁州舆图图片在实际舆图页面成功解码');
  await page.screenshot({ path: join(temporary, 'fresh-server-atlas.png'), fullPage: true });

  // 只修改本工具注册账号的副本，通过正规import形成有材料的工坊验收状态；不依赖开发者存档夹具。
  const originalGame = (await api('export')).game, workshopGame = JSON.parse(originalGame);
  const catalog = await api('forge-options');
  const material = catalog.materials.find(item => item.grade === 1 && item.element === '金' && item.family === 'metal');
  assert.ok(material, '发行版正式材料目录包含一阶金矿');
  workshopGame.player.arts.materials[material.id] = 50;
  await api('import', { game: JSON.stringify(workshopGame) });
  await page.goto(base + '/#workshop/forge', { waitUntil: 'domcontentloaded' });
  await page.locator('[data-forge-workshop]').waitFor();
  await page.locator('[data-forge-field][data-field="shape"]').selectOption('bell');
  await page.locator('[data-action="forge-slot"]').first().click();
  await page.locator('[data-forge-filter][data-field="grade"]').selectOption('1');
  await page.locator('[data-forge-filter][data-field="element"]').selectOption('金');
  await page.locator('[data-forge-filter][data-field="family"]').selectOption('metal');
  await page.locator(`[data-action="forge-fill"][data-id="${material.id}"]`).click();
  check(await page.locator('.forge-vessel-art svg path').count() > 0, '实际炼器器型SVG与材料面板正确显示，未依赖缺失的材质位图');
  for (const effect of ['cleanse', 'dispel']) {
    const quoteStarted = performance.now();
    const quote = page.waitForResponse(response => new URL(response.url()).pathname === '/api/forge-preview'
      && response.request().postDataJSON()?.effect === effect);
    await page.locator('[data-forge-field][data-field="effect"]').selectOption(effect);
    assert.ok((await quote).ok());
    await page.waitForFunction(effect => document.querySelector('.forge-result h3')?.textContent === (effect === 'cleanse' ? '清浊' : '破护')
      && /层/.test(document.querySelector('.forge-result p')?.textContent || ''), effect);
    const text = await page.locator('.forge-result p').first().innerText();
    check(new RegExp(`${effect === 'cleanse' ? 5 : 1}\\s*层`).test(text)
      && !/清除自身中毒、灼烧、悬灵与临时削弱|清除对手临时增伤、守势、额外减伤与锐势/.test(text), `解压包实际工坊${effect === 'cleanse' ? '清浊削5层' : '破护削1层'}，不显示旧全清文案`);
    timings.push({ action: `workshop-${effect}`, milliseconds: performance.now() - quoteStarted });
    await page.locator('.forge-result').screenshot({ path: join(temporary, `fresh-server-${effect}.png`) });
  }
  await api('import', { game: originalGame }); await context.close();

  await openContext({ width: 390, height: 844 });
  await page.goto(base + '/v2.html?release-qa=' + Date.now(), { waitUntil: 'domcontentloaded' });
  await page.locator('#login-form').waitFor();
  const mobileNickname = `ReleaseM${Date.now().toString(36).slice(-6)}`;
  await page.locator('#nickname').fill(mobileNickname); await page.locator('#password').fill(password);
  const mobileLogin = page.waitForResponse(response => new URL(response.url()).pathname === '/api/login');
  await page.locator('#login-form button[type="submit"]').click();
  check((await (await mobileLogin).json()).hasGame === false, '390手机新版从解压发行资源真实注册新道号');
  await page.locator('[data-ui="new-life"]').click();
  const waitMobileBuild = () => page.waitForFunction(() => document.querySelector('[data-ui="build-lock"]')?.disabled === false);
  await waitMobileBuild();
  for (const gender of ['男', '女']) {
    await page.locator('#life-gender').selectOption(gender); await waitMobileBuild();
    const id = `xuandao-${gender === '男' ? 'male' : 'female'}-effects-v1`;
    const lowId = `xuandao-${gender === '男' ? 'male' : 'female'}-v1`;
    check(await page.locator('[data-build-portrait]').count() === 15 && await page.locator(`[data-build-portrait="${id}"]`).count() === 1
      && await page.locator(`[data-build-portrait="${lowId}"]`).count() === 1, `390手机${gender}性目录同时显示玄道高低特效版，共15幅`);
    await page.locator(`[data-build-portrait="${id}"]`).click(); await waitMobileBuild();
    await page.locator(`[data-build-portrait="${id}"] img`).evaluate(image => image.decode());
    check(await page.locator(`[data-build-portrait="${id}"]`).getAttribute('aria-pressed') === 'true', `390手机可真实选中并解码${gender}性高特效立绘`);
    check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 2), `390手机${gender}性创建界面无横向溢出`);
    await page.locator(`[data-build-portrait="${id}"]`).screenshot({ path: join(temporary, `mobile-${gender === '男' ? 'male' : 'female'}-effects.png`) });
  }
  check(await page.locator('[data-build-portrait="xuandao-male-effects-v1"]').count() === 0, '390手机切换女性后清除异性高特效ID');
  await page.locator('[data-ui="build-lock"]').click();
  await page.locator('.opening-preview').waitFor();
  const mobileCreate = page.waitForResponse(response => new URL(response.url()).pathname === '/api/new');
  const mobileStarted = performance.now(); await page.locator('[data-ui="start-life"]').click();
  const mobileCreated = await (await mobileCreate).json();
  check(mobileCreated.ok && mobileCreated.state.player.portrait.includes('sect-xuandao-female-effects-v1.webp'), '390手机真实锁定并创建玄道高特效女性角色');
  timings.push({ action: 'mobile-new', milliseconds: performance.now() - mobileStarted });
  await page.locator('[data-ui="character"]').first().click();
  await page.locator('.character-profile img').evaluate(image => image.decode());
  check((await page.locator('.character-profile img').getAttribute('src')).includes('sect-xuandao-female-effects-v1.webp'), '390手机人物卡实际显示包内高特效女立绘');
  await page.screenshot({ path: join(temporary, 'mobile-created-character.png'), fullPage: true });
  check(imageResponses.length > 0 && imageResponses.every(response => [200, 206].includes(response.status)), '本轮发行浏览器实际请求的图片全部成功，无缺失贴图');
  check(errors.length === 0, '完整包桌面和手机注册、建角、工坊、保存续玩无浏览器脚本异常');
  report.ok = true;
} catch (error) {
  report.error = error.stack || error.message;
  if (page) await page.screenshot({ path: join(temporary, 'failure.png'), fullPage: true }).catch(() => {});
  throw error;
} finally {
  if (browser) await browser.close(); await stop();
  writeFileSync(join(temporary, 'report.json'), JSON.stringify(report, null, 2));
  console.log(JSON.stringify({ ok: report.ok, checks: checks.length, evidence: temporary, port }, null, 2));
}
