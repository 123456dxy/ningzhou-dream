// Real browser/audio acceptance against an isolated C++ server and temporary save.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { once } from 'node:events';
import { copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const value = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const sourceBinary = resolve(root, value('--binary', 'build-vs-share/Release/ningzhou_web.exe'));
assert.ok(existsSync(sourceBinary), '本轮Web程序必须存在');
const webRoot = resolve(root, value('--webroot', 'web'));
assert.ok(existsSync(join(webRoot, 'index.html')), '前端资源目录必须存在');
const binarySha256 = createHash('sha256').update(readFileSync(sourceBinary)).digest('hex');
const require = createRequire(import.meta.url);
const { chromium } = require(value('--playwright', process.env.PLAYWRIGHT_MODULE || 'playwright'));
const executablePath = value('--browser', process.env.CHROMIUM_PATH || [
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
  'C:/Program Files/Microsoft/Edge/Application/msedge.exe',
].find(existsSync));
assert.ok(executablePath && existsSync(executablePath), '需要实际Chromium浏览器');
const output = join(root, 'build', 'weather-music-integration-20261017');
mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'browser-'));
const binary = join(temporary, 'weather-music-qa.exe');
copyFileSync(sourceBinary, binary);
const reservation = createServer();
reservation.listen(0, '127.0.0.1');
await once(reservation, 'listening');
const port = reservation.address().port;
await new Promise(done => reservation.close(done));
const base = `http://127.0.0.1:${port}`;
let server, browser, checks = 0;
const errors = [], logs = [], evidence = [];
function check(condition, message) {
  assert.ok(condition, message);
  checks++;
  console.log(`PASS ${message}`);
}

try {
  server = spawn(binary, ['--local', '--host', '127.0.0.1', '--port', String(port),
    '--web', webRoot, '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback'), '--no-open'],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', data => logs.push(data.toString()));
  server.stderr.on('data', data => logs.push(data.toString()));
  let ready = false;
  for (let attempt = 0; attempt < 150; attempt++) {
    if (server.exitCode !== null) throw new Error('隔离服务提前退出');
    try { const response = await fetch(base + '/api/mode'); await response.arrayBuffer(); ready = response.ok; if (ready) break; }
    catch { /* wait for this owned process only */ }
    await new Promise(done => setTimeout(done, 100));
  }
  check(ready, '独立临时端口与存档的C++服务启动');
  browser = await chromium.launch({ executablePath, headless: true });
  const entries = [['classic', '/', 1440], ['classic-narrow', '/', 390], ['mobile', '/v2.html', 320]];
  for (const [name, entry, width] of entries) {
    const context = await browser.newContext({ viewport: { width, height: 900 } });
    const interactionTimes = [];
    let weather = null;
    await context.route('**/api/**', async route => {
      const response = await route.fetch();
      if (!weather || !response.headers()['content-type']?.includes('json')) return route.fulfill({ response });
      const payload = await response.json();
      // Only the weather label is substituted for deterministic mapping checks.
      // All actual game data and operations still come from the isolated engine.
      if (payload.state?.world) payload.state.world.weather = weather;
      if (payload.world) payload.world.weather = weather;
      return route.fulfill({ response, json: payload });
    });
    await context.addInitScript(() => {
      const actualPlay = HTMLMediaElement.prototype.play;
      const media = new Set();
      window.__weatherMusicQA = { media, playCalls: [], failures: [], analysers: [] };
      HTMLMediaElement.prototype.play = function(...args) {
        media.add(this);
        window.__weatherMusicQA.playCalls.push({ src: this.src, time: performance.now() });
        const result = actualPlay.apply(this, args);
        result?.catch(error => window.__weatherMusicQA.failures.push({ src: this.src, name: error.name }));
        return result;
      };
      const Constructor = window.AudioContext || window.webkitAudioContext;
      if (Constructor) {
        const actualSource = Constructor.prototype.createMediaElementSource;
        Constructor.prototype.createMediaElementSource = function(...args) {
          const source = actualSource.apply(this, args);
          const analyser = this.createAnalyser();
          analyser.fftSize = 1024;
          source.connect(analyser);
          window.__weatherMusicQA.analysers.push(analyser);
          return source;
        };
      }
    });
    const page = await context.newPage();
    page.on('pageerror', error => errors.push(`${name}: ${error.message}`));
    const musicRequests = [];
    page.on('request', request => { if (request.url().includes('/assets/music/') && request.url().endsWith('.mp3')) musicRequests.push(request.url()); });
    const start = performance.now();
    await page.goto(base + entry);
    await page.locator('[data-music-open]').first().waitFor();
    await page.waitForTimeout(200);
    check(musicRequests.length === 0, `${name} 默认关闭不下载音乐`);
    const dialogStarted = performance.now();
    await page.locator('[data-music-open]').first().click();
    await page.locator('#music-dialog[open]').waitFor();
    interactionTimes.push({ action: 'open-dialog', milliseconds: Math.round(performance.now() - dialogStarted) });
    check(await page.locator('[data-music-mode] option').count() === 6, `${name} 自动模式和五首白名单选项`);
    await page.locator('[data-music-enabled]').check();
    await page.waitForFunction(() => [...window.__weatherMusicQA.media].some(audio => !audio.paused && audio.currentTime > 0.15 && audio.readyState >= 2));
    check((await page.locator('[data-music-status]').innerText()).includes('正在播放'), `${name} 真实用户手势启动实际音频解码播放`);
    const decoded = await page.evaluate(() => window.__weatherMusicQA.analysers.some(analyser => {
      const values = new Float32Array(analyser.fftSize); analyser.getFloatTimeDomainData(values);
      return values.some(value => Math.abs(value) > 0.00001);
    }));
    check(decoded, `${name} WebAudio管线含实际非零音频采样`);
    await page.locator('[data-music-volume]').focus();
    await page.locator('[data-music-volume]').press('Home');
    check((await page.locator('[data-music-volume-label]').innerText()).includes('0%'), `${name} 音量可设为零`);
    for (let step = 0; step < 18; step++) await page.locator('[data-music-volume]').press('ArrowRight');
    for (const [slug, title] of [['rain', '听雨'], ['wind', '长风'], ['mist', '江雾'], ['clouds', '云霞'], ['sunny', '晴山']]) {
      const switchStarted = performance.now();
      await page.locator('[data-music-mode]').selectOption(slug);
      await page.waitForFunction(id => [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith(`/assets/music/${id}.mp3`) && !audio.paused && audio.currentTime > 0.05), slug);
      interactionTimes.push({ action: `switch-${slug}`, milliseconds: Math.round(performance.now() - switchStarted) });
      check((await page.locator('[data-music-title]').innerText()).includes(title), `${name} 手动切换${title}并真实播放`);
      if (name === 'classic') {
        // Exercise the actual decoded loop boundary without waiting two minutes.
        await page.evaluate(id => {
          const audio = [...window.__weatherMusicQA.media].find(media => media.src.endsWith(`/assets/music/${id}.mp3`) && !media.paused);
          if (!Number.isFinite(audio?.duration)) throw new Error('音频时长尚不可用');
          audio.currentTime = Math.max(0, audio.duration - 0.12);
        }, slug);
        await page.waitForFunction(id => [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith(`/assets/music/${id}.mp3`) && !audio.paused && audio.currentTime < 1 && !audio.ended), slug);
        check(true, `classic ${title}实际播放越过末尾并循环`);
      }
    }
    await page.waitForTimeout(4300);
    check(await page.evaluate(() => [...window.__weatherMusicQA.media].filter(audio => !audio.paused).length) === 1, `${name} 渐变结束仅保留一首播放`);
    const noOverflow = await page.locator('#music-dialog').evaluate(dialog => dialog.scrollWidth <= dialog.clientWidth + 2);
    check(noOverflow, `${name} 音乐弹窗无横向溢出`);
    const triggerBounds = await page.locator('[data-music-open]').first().boundingBox();
    check(triggerBounds && triggerBounds.x >= 0 && triggerBounds.x + triggerBounds.width <= width + 1, `${name} 音乐入口在可视范围内`);
    const shot = join(temporary, `${name}-music.png`);
    await page.screenshot({ path: shot });
    await page.locator('[data-music-enabled]').uncheck();
    check(await page.evaluate(() => [...window.__weatherMusicQA.media].every(audio => audio.paused)), `${name} 关闭立即停止全部音频`);
    await page.locator('[data-music-enabled]').check();
    await page.waitForFunction(() => [...window.__weatherMusicQA.media].some(audio => !audio.paused));
    await page.reload();
    await page.locator('[data-music-open]').first().click();
    await page.locator('#music-dialog[open]').waitFor();
    check(await page.locator('[data-music-mode]').inputValue() === 'sunny' && await page.locator('[data-music-volume]').inputValue() === '18', `${name} 刷新保留选曲与音量`);
    await page.locator('[data-music-mode]').selectOption('auto');
    weather = '灵雨润山';
    await page.reload();
    await page.locator('[data-music-open]').first().click();
    await page.waitForFunction(() => [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith('/assets/music/rain.mp3') && !audio.paused));
    check((await page.locator('[data-music-weather]').innerText()) === '灵雨润山', `${name} 实际state链路按天气自动选听雨`);
    if (name === 'classic') {
      for (const [label, slug] of [['长风过境','wind'], ['薄雾锁江','mist'], ['丹霞映天','clouds'], ['秋高气爽','sunny']]) {
        weather = label;
        await page.reload();
        await page.locator('[data-music-open]').first().click();
        await page.waitForFunction(id => [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith(`/assets/music/${id}.mp3`) && !audio.paused), slug);
        await page.waitForFunction(label => document.querySelector('[data-music-weather]')?.textContent === label, label);
        check((await page.locator('[data-music-weather]').innerText()) === label, `classic 天气映射实际播放 ${label}`);
      }
      await page.route('**/assets/music/clouds.mp3', route => route.abort());
      await page.locator('[data-music-mode]').selectOption('clouds');
      await page.waitForFunction(() => document.querySelector('[data-music-error]')?.hidden === false);
      check(await page.locator('[data-music-retry]').isVisible(), '音频失败显示可恢复重试入口');
      await page.unroute('**/assets/music/clouds.mp3');
      await page.locator('[data-music-retry]').click();
      await page.waitForFunction(() => document.querySelector('[data-music-status]')?.textContent.includes('正在播放')
        && [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith('/assets/music/clouds.mp3') && !audio.paused && audio.currentTime > 0.05));
      check((await page.locator('[data-music-status]').innerText()).includes('正在播放'), '恢复资源后重试成功');
      await page.locator('[data-music-mode]').selectOption('auto');
      weather = '灵雨润山';
      await page.locator('[data-music-close]').click();
      await page.locator('[data-action="advance-step"][data-days="1"]').first().click();
      await page.locator('[data-music-open]').first().click();
      await page.waitForFunction(() => document.querySelector('[data-music-weather]')?.textContent === '灵雨润山'
        && [...window.__weatherMusicQA.media].some(audio => audio.src.endsWith('/assets/music/rain.mp3') && !audio.paused));
      check(true, '真实推进一天后的状态重绘自动切换天气音乐，无需刷新页面');
    }
    evidence.push({ name, width, elapsedMilliseconds: Math.round(performance.now() - start), screenshot: shot,
      interactionTimes, assetRequests: musicRequests.length, failures: await page.evaluate(() => window.__weatherMusicQA.failures) });
    await context.unrouteAll({ behavior: 'ignoreErrors' });
    await context.close();
  }
  check(errors.length === 0, '两端无未处理页面异常');
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, base, sourceBinary, binarySha256, webRoot, evidence, errors }, null, 2));
  console.log(JSON.stringify({ passed: checks, report: join(temporary, 'report.json') }));
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, errors, logs }, null, 2));
  throw error;
} finally {
  if (browser) await Promise.all(browser.contexts().map(context => context.unrouteAll({ behavior: 'ignoreErrors' })));
  await browser?.close();
  if (server && server.exitCode === null) { server.kill(); await once(server, 'exit').catch(() => {}); }
}
