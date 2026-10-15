// 独立斗法页真实冒烟：临时端口/内存角色，真实自然抽牌与界面点击，不接入玩家服务。
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
// 用途：读取检查参数；流程：工具启动；传键与缺省值，返回参数值。
function argument(name, fallback) { return args.includes(name) ? args[args.indexOf(name) + 1] : fallback; }
const require = createRequire(import.meta.url);
const { chromium } = require(argument('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
const directory = join(root, 'build/diagnostics/battle-lab-browser-20261015');
mkdirSync(directory, { recursive: true });
const temporary = mkdtempSync(join(directory, 'run-'));
const executable = join(temporary, 'battle-lab-qa.exe');
copyFileSync(resolve(root, argument('--exe', 'build-vs-share/Release/ningzhou_battle_lab.exe')), executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8107, 8108, 8109, 8110, 8111].includes(port));
const base = `http://127.0.0.1:${port}`;
const server = spawn(executable, ['--port', String(port), '--web', join(root, 'web'), '--no-open'],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const serverLog = [], checks = [], runs = [], timings = [], errors = [], posts = [];
const synergy = { highlightedHands: 0, retainedAcrossTurns: 0, retainedAcrossNeutral: 0, enhancementFrames: 0, maxHandSize: 0 };
const beastChecks = { frames: 0, multipleActions: 0, siphonSeen: 0, siphonPlayed: 0 };
server.stdout.on('data', data => serverLog.push(String(data))); server.stderr.on('data', data => serverLog.push(String(data)));
let browser;
// 用途：保存真实断言；流程：浏览器冒烟；传条件和说明，返回无，失败抛错。
function check(condition, label) { assert.ok(condition, label); checks.push(label); console.log(`PASS ${label}`); }
try {
  let ready = false;
  for (let attempt = 0; attempt < 200; attempt++) {
    try { ready = (await fetch(base + '/api/lab/config')).ok; } catch { /* 服务刚启动时继续短轮询。 */ }
    if (ready) break;
    if (server.exitCode !== null) throw new Error('独立服务提前退出');
    await new Promise(done => setTimeout(done, 50));
  }
  check(ready, '独立内存服务器启动');
  browser = await chromium.launch({ executablePath: argument('--browser-path', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'), headless: true });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1000 }, locale: 'zh-CN', reducedMotion: 'reduce' });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') posts.push(request.url()); });
  await page.goto(base + '/battle-lab.html', { waitUntil: 'domcontentloaded' });
  await page.getByTestId('start-battle').waitFor();
  const config = await (await context.request.get(base + '/api/lab/config')).json();
  check(config.builds.length === 8 && config.realms.length === 5, '真实八流派五境界目录');
  check(config.tiers.map(tier => tier.id).join('/') === 'initial/advanced' && config.opponents.length === 16, '初始/完整传承与六宗对手目录');

  // 用途：只读抓取后端快照；流程：策略判断与断言；返回真实JSON，不改变战局。
  async function state() { return (await context.request.get(base + '/api/lab/state')).json(); }
  // 用途：点击真实控件并等待动作/页面恢复；流程：所有施术；传控件与接口，返回权威动作快照。
  async function act(locator, path = '/api/lab/turn', doubleClick = false) {
    // 输入节奏保持低于共享HTTP每秒15次限流，等待不计入界面响应时间。
    await new Promise(done => setTimeout(done, 90));
    const started = performance.now();
    const responsePromise = page.waitForResponse(response => new URL(response.url()).pathname === path && response.request().method() === 'POST');
    const [response] = await Promise.all([responsePromise, doubleClick ? locator.evaluate(button => { button.click(); button.click(); }) : locator.click()]);
    const result = await response.json();
    await page.waitForFunction(() => document.querySelector('#lab-stage')?.disabled === false);
    timings.push({ path, milliseconds: performance.now() - started });
    assert.ok(response.ok() && !result.error, result.error || `HTTP ${response.status()}`);
    return result;
  }
  // 用途：仅用可见预览估计出牌收益；流程：LabVisibleGreedyV2；传手牌和当前战况，返回评分，不重写伤害规则。
  function score(card, battle) {
    const neededShield = Math.max(0, battle.intentDamage);
    const basis = Math.max(20, ...battle.hand.map(entry => entry.damage || 0));
    let value = card.damage + card.heal * 1.1 + Math.min(neededShield, card.shield) * .9;
    value += Math.max(0, card.manaGain) * basis * .35;
    value += Math.min(card.draw || 0, Math.max(0, 8 - battle.hand.length)) * basis * .18;
    for (const effect of card.statuses || []) {
      if (effect.amount <= 0) continue;
      if (effect.id === 'burn') value += effect.amount * 1.5;
      else if (effect.id === 'poison') value += effect.perTurn * .65;
      else if (['water', 'charge', 'qi', 'soul'].includes(effect.id)) value += effect.amount * basis * .12;
    }
    if (card.kind === 'redraw' && battle.hand.length > 2) value *= .12;
    if (card.kind === 'store') value = battle.hand.length <= 2 ? basis * .1 : -1;
    if (card.requiresTarget) value = (battle.removalOptions || []).some(option => option.id === 'defend') ? basis * .25 : -1;
    const swordDaos = { metalSword: '金', waterSword: '水', woodSword: '木', fireSword: '火', earthSword: '土' };
    const nextDao = { '土': '金', '金': '水', '水': '木', '木': '火', '火': '土' };
    if (swordDaos[card.id] && battle.elementalSwordSequence > 0 && nextDao[battle.lastDao] === swordDaos[card.id]) value += basis * 1.5;
    return value / (1 + card.cost * .35);
  }
  // 用途：在自然手牌上点击完整对局；流程：八流派冒烟；传构筑、境界、敌人和种子，返回真实胜负及动作轨迹。
  async function play(build, stage, opponent, seed, tier = 'initial') {
    await page.locator(`[data-tier-id="${tier}"]`).click();
    await page.locator(`[data-build-id="${build.id}"]`).click();
    await page.getByTestId('stage-select').selectOption(String(stage));
    await page.getByTestId('opponent-select').selectOption(opponent);
    await page.getByTestId('seed-input').fill(String(seed));
    let current = await act(page.getByTestId('start-battle'), '/api/lab/start');
    const opening = structuredClone(current.battle), trace = [];
    check(current.battle && current.selection.buildId === build.id && current.selection.tier === tier, `${build.name} ${tier} ${stage}阶真实开局`);
    if (opponent === 'beast-body') await page.screenshot({ path: join(temporary, `${build.id}-${tier}-opening.png`), fullPage: true });
    for (let action = 0; current.battle && action < 500; action++) {
      const battle = current.battle;
      if (argument('--mode', 'all') === 'beast') {
        if (battle.enemyBeast) {
          assert.equal(await page.locator('[data-beast-intent]').count(), 1, '真实妖兽意图使用逐段面板');
          assert.equal(await page.locator('[data-beast-action]').count(), battle.beastActions.length, '逐段DOM数量对应真实冻结计划');
          const rows = page.locator('[data-beast-action]');
          for (let index = 0; index < battle.beastActions.length; index++) {
            const action = battle.beastActions[index], row = rows.nth(index);
            assert.equal(await row.getAttribute('data-beast-action'), action.id);
            assert.equal(await row.getAttribute('data-executed'), String(action.executed !== false));
            if (action.executed !== false && action.rawDamage > 0)
              assert.ok((await row.textContent()).includes(`打出 ${action.rawDamage} · 气血伤害 ${action.damage}`));
          }
          beastChecks.frames++;
          if (battle.beastActions.length > 1) beastChecks.multipleActions++;
        } else {
          assert.equal(battle.enemyActionCount, battle.intentSpells.length, 'NPC总行动数包含当前真实预演的补牌/防御/攻击');
          assert.ok(battle.enemyActionCount <= battle.enemyActionLimit, 'NPC预演不突破境界行动上限');
          assert.ok((await page.getByTestId('npc-action-budget').textContent()).includes(`预计行动 ${battle.enemyActionCount} / ${battle.enemyActionLimit}`));
        }
        for (const card of battle.hand.filter(card => card.id === 'fireSiphon')) {
          assert.equal(card.cost, 3); assert.equal(card.damage, 0); assert.equal(card.rawDamage, 0);
          assert.ok((await page.locator(`[data-card-uid="${card.uid}"]`).textContent()).includes('噬焰回生'));
          beastChecks.siphonSeen++;
        }
      }
      if (argument('--mode', 'all') === 'synergy') {
        assert.equal(battle.handLimit, 12, '真实服务声明扩大后的十二张容量');
        assert.ok(battle.hand.length <= battle.handLimit, '实际抽牌不超过后端容量');
        synergy.maxHandSize = Math.max(synergy.maxHandSize, battle.hand.length);
        assert.ok(battle.elementalSwordEnhancementLayers >= 0 && battle.elementalSwordEnhancementLayers <= 5, '本场强化不越五层');
        if (battle.elementalSwordEnhancementLayers > 0) {
          const chip = await page.locator('.status-chip').filter({hasText: /^相生强化 /}).textContent();
          assert.ok(chip.includes(`${battle.elementalSwordEnhancementLayers} / ${battle.elementalSwordEnhancementLimit}`));
          assert.ok(chip.includes(`+${battle.elementalSwordEnhancementPercent}%`), '界面直接显示真实功法倍率');
          if (!synergy.enhancementFrames) await page.screenshot({path: join(temporary, 'elemental-enhancement.png'), fullPage: true});
          synergy.enhancementFrames++;
        }
        const highlighted = battle.hand.filter(card => card.elementalGenerationReady === true);
        assert.equal(await page.locator('[data-card-uid][data-elemental-generation-ready="true"]').count(), highlighted.length, '相生圈与后端资格逐张一致');
        if (highlighted.length) {
          if (!synergy.highlightedHands) await page.screenshot({ path: join(temporary, 'elemental-generation-ring.png'), fullPage: true });
          synergy.highlightedHands++;
          assert.equal(await page.locator('.elemental-generation-badge').count(), highlighted.length, '每张相生牌都有文字提示');
        }
      }
      const candidates = battle.hand.filter(card => card.playable).map(card => ({ card, score: score(card, battle) })).filter(entry => entry.score > 0).sort((left, right) => right.score - left.score);
      const chosen = candidates[0]?.card;
      trace.push({ turn: battle.turn, mana: battle.mana, hp: battle.hp, enemyHp: battle.enemyHp, uid: chosen?.uid || 'end', spell: chosen?.id || 'end' });
      if (!chosen) current = await act(page.getByTestId('end-turn'));
      else if (chosen.requiresTarget) {
        const target = battle.removalOptions.find(option => option.id === 'defend' && option.uid);
        assert.ok(target);
        await page.locator(`[data-card-uid="${chosen.uid}"]`).click();
        await page.locator(`[data-target-id="${target.uid}"]`).click();
        current = await act(page.locator('[data-action="target-confirm"]'));
        if (current.battle) {
          check(current.battle.exhausted === battle.exhausted + 1, '忘机咒只移除单张UID');
          check(current.battle.removalOptions.filter(option => option.id === target.id).length === battle.removalOptions.filter(option => option.id === target.id).length - 1, '同种神通其他副本仍可用');
        }
      } else current = await act(page.locator(`[data-card-uid="${chosen.uid}"]`));
      if (argument('--mode', 'all') === 'beast' && chosen?.id === 'fireSiphon') {
        assert.ok(current.battle, '纯治疗不凭空击杀对手');
        assert.equal(current.battle.hp - battle.hp, chosen.heal, '自然手牌真实转换回复与预览一致');
        assert.equal(current.battle.enemyHp, battle.enemyHp); assert.equal(current.battle.burn, 0);
        assert.equal(current.battle.burnTurns, 0); beastChecks.siphonPlayed++;
      }
      if (argument('--mode', 'all') === 'synergy' && current.battle && battle.elementalSwordSequence > 0) {
        if (!chosen || !['金', '木', '水', '火', '土'].includes(chosen.dao)) {
          assert.equal(current.battle.elementalSwordSequence, battle.elementalSwordSequence, '中性牌与回合边界保持五剑进度');
          assert.equal(current.battle.lastDao, battle.lastDao, '中性牌与回合边界保留相生根基');
          assert.equal(current.battle.elementalSwordEnhancementLayers, battle.elementalSwordEnhancementLayers, '中性牌与回合边界保留本场强化');
          if (!chosen) synergy.retainedAcrossTurns++; else synergy.retainedAcrossNeutral++;
        }
      }
    }
    check(!current.battle && current.result, `${build.name} 对 ${opponent} 可正常完成并展示结果`);
    await page.getByTestId('battle-result').waitFor();
    return { build: build.id, tier, stage, opponent, seed, policy: 'LabVisibleGreedyV2', won: current.result.won, turns: current.result.turns, actions: trace.length, opening, trace };
  }
  if (argument('--mode', 'all') === 'synergy') {
    const build = config.builds.find(build => build.id === 'five-elements');
    runs.push(await play(build, 6, 'beast-body', 31));
    runs.push(await play(build, 12, 'xuandao-complete', 31, 'advanced'));
    check(synergy.highlightedHands > 0, '真实自然手牌出现相生光圈与文字');
    check(synergy.retainedAcrossTurns > 0, '真实跨回合保持五剑连段');
    check(synergy.retainedAcrossNeutral > 0, '真实中性牌穿插不破五剑连段');
    check(synergy.enhancementFrames > 0, '真实相生积累强化并展示层数与功法倍率');
  } else if (argument('--mode', 'all') === 'beast') {
    const fire = config.builds.find(build => build.id === 'fire-stack');
    runs.push(await play(fire, 12, 'beast-fire', 73, 'advanced'));
    runs.push(await play(fire, 9, 'beast-body', 137, 'advanced'));
    runs.push(await play(config.builds.find(build => build.id === 'five-elements'), 12, 'beast-mind', 31, 'advanced'));
    runs.push(await play(config.builds.find(build => build.id === 'wood-poison'), 12, 'xuandao-complete', 73));
    check(beastChecks.multipleActions > 0 && beastChecks.frames > 0, '真实高境妖兽逐段预告持续对应后端');
    check(beastChecks.siphonSeen > 0, '自然抽到3费噬焰回生并展示零伤害与转换回复');
    check(beastChecks.siphonPlayed > 0, '真实自然手牌施放噬焰回生且实际治疗/清火与预览一致');
    await page.screenshot({ path: join(temporary, 'beast-result.png'), fullPage: true });
  } else if (argument('--mode', 'all') !== 'interaction') {
  for (const build of config.builds) runs.push(await play(build, 6, 'beast-body', 31, 'advanced'));
  for (const build of config.builds) {
    for (const opponent of ['beast-body', 'lihuo']) {
      const run = await play(build, 6, opponent, 31); runs.push(run);
      console.log(`RUN ${build.id} ${opponent} ${run.won ? 'win' : 'loss'} ${run.turns} turns ${run.actions} clicks`);
    }
  }
  runs.push(await play(config.builds[0], 0, 'beast-body', 104729));
  runs.push(await play(config.builds[0], 12, 'beast-mind', 104729));
  for (const id of ['wood-poison', 'mind-cycle']) {
    const build = config.builds.find(entry => entry.id === id);
    runs.push(await play(build, 6, 'zhushan', 31));
    runs.push(await play(build, 6, 'zhushan-complete', 31));
  }
  } else runs.push(await play(config.builds.find(build => build.id === 'mind-cycle'), 6, 'beast-body', 31, 'advanced'));
  const retry = await act(page.getByTestId('retry-battle'), '/api/lab/start');
  const original = runs.at(-1).opening;
  check(JSON.stringify(retry.battle.hand) === JSON.stringify(original.hand), '同种子重试真实开局相同');
  const playable = retry.battle.hand.find(card => card.playable && !card.requiresTarget);
  assert.ok(playable);
  const postCount = posts.length;
  await act(page.locator(`[data-card-uid="${playable.uid}"]`), '/api/lab/turn', true);
  check(posts.length === postCount + 1, '真实快速双击只提交一次施术');
  let failOnce = true;
  await page.route('**/api/lab/turn', async route => {
    if (failOnce) { failOnce = false; await route.fulfill({ status: 503, contentType: 'application/json', body: '{"error":"隔离测试临时故障"}' }); }
    else await route.continue();
  });
  const beforeFailure = await state();
  const failedResponse = page.waitForResponse(response => response.url().endsWith('/api/lab/turn'));
  await page.getByTestId('end-turn').click(); await failedResponse;
  await page.locator('[data-action="refresh"]').waitFor();
  check((await state()).revision === beforeFailure.revision, '网络故障不结算回合');
  await page.locator('[data-action="refresh"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="end-turn"]')?.disabled === false);
  check(await page.getByTestId('end-turn').isEnabled(), '故障后可刷新恢复真实操作');
  await page.setViewportSize({ width: 600, height: 900 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '600px独立页没有页面横向溢出');
  await page.screenshot({ path: join(temporary, 'narrow.png'), fullPage: true });
  check(errors.length === 0, '所有真实流派试玩无页面脚本错误');
  const sorted = timings.map(item => item.milliseconds).sort((left, right) => left - right);
  const p95 = sorted[Math.floor((sorted.length - 1) * .95)];
  console.log(`SUMMARY ${runs.length} fights; ${timings.length} real actions; p95=${p95.toFixed(1)}ms; evidence=${temporary}`);
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, runs, synergy, beastChecks, timings, p95, errors, serverLog }, null, 2));
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, runs, timings, errors, serverLog }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  server.kill();
}
