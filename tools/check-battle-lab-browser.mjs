// 独立斗法页真实冒烟：临时端口/内存角色，真实自然抽牌与界面点击，不接入玩家服务。
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { copyFileSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { createServer } from 'node:net';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { cardKind, cardRule, battleStatusRows } from '../web/cards.mjs';
import { liveStatuses, battleStatusDetail } from '../web/v2/views/battle.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
// 用途：读取检查参数；流程：工具启动；传键与缺省值，返回参数值。
function argument(name, fallback) { return args.includes(name) ? args[args.indexOf(name) + 1] : fallback; }
// 用途：核对共享卡面和手机状态的持续范围；流程：浏览器前置/独立文案检查；只渲染测试快照。
function checkPoisonCopy() {
  const battle = { playerPoison: 8, playerPoisonTurns: 1, poison: 12, poisonTurns: 1, burn: 9, burnTurns: 2 };
  const poison = battleStatusRows(battle).find(row => row.label === '受毒');
  assert.match(poison.value, /本场持续/);
  assert.doesNotMatch(poison.value, /剩.*回合/);
  assert.match(cardRule({ kind: 'poison' }), /本场持续/);
  assert.equal(cardKind({ kind: 'poisonBloom' }), '滋毒经营');
  assert.equal(cardRule({ kind: 'poisonBloom', description: '后端权威说明' }), '后端权威说明');
  assert.equal(cardKind({ kind: 'poisonSiphon' }), '借毒治疗');
  assert.match(cardRule({ kind: 'poisonSiphon' }), /不消耗毒层/);
  assert.match(liveStatuses(battle), /受毒 8\/回 · 本场持续/);
  assert.match(liveStatuses(battle), /敌毒 12 层 · 本场持续/);
  assert.match(battleStatusDetail(battle), /敌方中毒 12 层 · 本场持续/);
  assert.match(battleStatusDetail(battle), /敌方灼烧 9\/回 · 剩 2 回合/);
  console.log('PASS 共享卡面与手机毒状态本场持续，灼烧仍显示剩余回合');
}
checkPoisonCopy();
if (args.includes('--copy-only')) process.exit(0);
const require = createRequire(import.meta.url);
const { chromium } = require(argument('--playwright-module', process.env.PLAYWRIGHT_MODULE || 'C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright'));
const directory = join(root, 'build/diagnostics/battle-lab-browser-20261015');
mkdirSync(directory, { recursive: true });
const temporary = mkdtempSync(join(directory, 'run-'));
const executable = join(temporary, 'battle-lab-qa.exe');
copyFileSync(resolve(root, argument('--exe', 'build-vs-share/Release/ningzhou_battle_lab.exe')), executable);
const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
const port = reservation.address().port; await new Promise(done => reservation.close(done));
assert.ok(![8106, 8107, 8108, 8109, 8110, 8111, 8112, 8113, 8114, 8115, 8116, 8117, 8118].includes(port));
const base = `http://127.0.0.1:${port}`;
const server = spawn(executable, ['--port', String(port), '--web', join(root, 'web'), '--no-open'],
  { cwd: temporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
const serverLog = [], checks = [], runs = [], timings = [], errors = [], posts = [];
const synergy = { highlightedHands: 0, retainedAcrossTurns: 0, retainedAcrossNeutral: 0, enhancementFrames: 0, maxHandSize: 0 };
const beastChecks = { frames: 0, multipleActions: 0, siphonSeen: 0, siphonPlayed: 0 };
const poisonSiphonChecks = { seen: 0, noPoisonDisabled: 0, fullHealthDisabled: 0, played: 0, cooldownDisabled: 0 };
const waterFlowChecks = { frames: 0, triggered: {}, cappedAttacks: 0, independentDraws: 0, roundResets: 0, readOnly: 0 };
const waterFlowAttacks = ['waterSeal', 'stormSword', 'wavehit', 'waterfall'];
const waterVitalityChecks = { previewFacts: 0, healingHits: 0, fullHealthHits: 0, blockedHits: 0, npcRounds: 0,
  npcDrawActions: 0, npcDrawCards: 0, maximumNpcActions: 0, terminalRoundsUnobservable: 0, rounds: [], hits: [], loadouts: [] };
const drawToChecks = { seen: 0, played: 0, preEightPlayed: 0, zeroDrawPlayed: 0, aboveTargetPlayed: 0,
  cooldownDisabled: 0, doubleClick: 0, readOnly: 0, reshuffles: 0, casts: [] };
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
  await context.addInitScript(() => {
    const original = crypto.getRandomValues.bind(crypto);
    window.__labSeedCalls = 0;
    crypto.getRandomValues = array => {
      if (array instanceof Uint32Array && array.length === 1) window.__labSeedCalls++;
      return original(array);
    };
  });
  const page = await context.newPage(); page.setDefaultTimeout(10000);
  page.on('pageerror', error => errors.push(error.message));
  page.on('request', request => { if (request.method() === 'POST') posts.push(request.url()); });
  await page.goto(base + '/battle-lab.html', { waitUntil: 'domcontentloaded' });
  await page.getByTestId('start-battle').waitFor();
  await page.waitForFunction(() => document.querySelector('#lab-stage')?.disabled === false);
  const config = await (await context.request.get(base + '/api/lab/config')).json();
  check(config.builds.length === 8 && config.realms.length === 5, '真实八流派五境界目录');
  check(config.tiers.map(tier => tier.id).join('/') === 'initial/advanced' && config.opponents.length === 16, '初始/完整传承与六宗对手目录');

  // 用途：只读抓取后端快照；流程：策略判断与断言；返回真实JSON，不改变战局。
  async function state() {
    await new Promise(done => setTimeout(done, 90));
    const response = await context.request.get(base + '/api/lab/state');
    assert.ok(response.ok(), `只读战况 HTTP ${response.status()}`);
    return response.json();
  }
  // 用途：从后端分段战报读取回春事实；流程：自然水功法专项；不在界面计算伤害或治疗。
  function waterVitalityFacts(text) {
    const facts = [...String(text || '').matchAll(/浪击诀回春：实际损血(\d+)，治疗潜力(\d+)，回复(\d+)/g)];
    assert.ok(facts.length <= 1, '一次主动直伤最多记录一次水功法回春，不递归');
    if (!facts.length) return null;
    return { direct: Number(facts[0][1]), potential: Number(facts[0][2]), healing: Number(facts[0][3]) };
  }
  // 用途：识别截断战报中本次新增条目；流程：敌方回合专项；保留原始前后战报用于复核。
  function appendedLogs(previous, latest) {
    let overlap = Math.min(previous.length, latest.length);
    while (overlap > 0 && JSON.stringify(previous.slice(-overlap)) !== JSON.stringify(latest.slice(0, overlap))) overlap--;
    return latest.slice(overlap);
  }
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
  // 用途：通过真实控件验证随机/固定种子边界；流程：seed与interaction冒烟；不接触玩家服务。
  async function checkSeedModes() {
    const start = page.getByTestId('start-battle');
    const fixed = page.getByTestId('fixed-seed-input');
    const seed = page.getByTestId('seed-input');
    const calls = () => page.evaluate(() => window.__labSeedCalls);
    check(!(await fixed.isChecked()) && await seed.isDisabled(), '试炼默认随机模式，手填种子需明确启用');
    check(await calls() === 0, '首次载入不生成种子或自动开战');
    const beforeReads = await state(), beforeReadPosts = posts.length;
    for (let index = 0; index < 3; index++) assert.deepEqual(await state(), beforeReads);
    check(posts.length === beforeReadPosts && await calls() === 0, '纯查询不生成种子、不提交开局、不改变快照');
    const beforeDouble = posts.length;
    let current = await act(start, '/api/lab/start', true);
    check(posts.length === beforeDouble + 1 && await calls() === 1, '随机开局快速双击仅生成一个种子并提交一次');
    const firstSeed = current.selection.seed;
    current = await act(start, '/api/lab/start');
    check(current.selection.seed !== firstSeed && await calls() === 2, '随机模式再次开局更换种子');
    const previousSeed = current.selection.seed;
    await page.getByTestId('stage-select').selectOption('6');
    current = await act(start, '/api/lab/start');
    check(current.selection.stage === 6 && current.selection.seed !== previousSeed && await calls() === 3, '修改参数后重开也在点击时生成新种子');
    await fixed.check();
    await seed.fill('0');
    const zero = await act(start, '/api/lab/start');
    const zeroAgain = await act(start, '/api/lab/start');
    check(zero.selection.seed === 0 && JSON.stringify(zero.battle.hand) === JSON.stringify(zeroAgain.battle.hand) && await calls() === 3, '固定种子0合法，重复开局复现自然手牌且不生成随机数');
    await seed.fill('4294967295');
    current = await act(start, '/api/lab/start');
    check(current.selection.seed === 4294967295, '固定种子支持uint32最大值');
    for (const invalid of ['', '-1', '4294967296', '1.5']) {
      const beforeInvalid = await state(), beforeInvalidPosts = posts.length;
      await seed.fill(invalid); await start.click();
      await page.locator('#lab-notice').filter({ hasText: '种子' }).waitFor();
      check(posts.length === beforeInvalidPosts && (await state()).revision === beforeInvalid.revision, `非法固定种子${invalid || '空白'}不提交、不改变战局`);
    }
    await fixed.uncheck();
    current = await act(start, '/api/lab/start');
    check(Number.isInteger(current.selection.seed) && await calls() === 4, '关闭固定模式后忽略残留非法输入，恢复随机开局');
    const beforeReload = await state(), beforeReloadPosts = posts.length;
    await page.reload({ waitUntil: 'domcontentloaded' }); await start.waitFor();
    await page.waitForFunction(() => document.querySelector('#lab-stage')?.disabled === false);
    check(posts.length === beforeReloadPosts && await calls() === 0 && JSON.stringify(await state()) === JSON.stringify(beforeReload), '刷新只恢复战局，不自动重开或生成种子');
    check(!(await fixed.isChecked()) && await seed.isDisabled(), '刷新后仍默认随机模式，当前战局种子保留供复现');
    await page.setViewportSize({ width: 600, height: 900 });
    await fixed.check();
    check(await seed.isEnabled() && await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '600px固定种子控件可操作且不横向溢出');
    await page.screenshot({ path: join(temporary, 'seed-controls-narrow.png'), fullPage: true });
    await page.setViewportSize({ width: 1440, height: 1000 });
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
    if (argument('--mode', 'all') === 'poison-siphon' && card.id === 'poisonSiphon') value += basis * 10;
    if (argument('--mode', 'all') === 'water-flow') {
      if (['waterSeal', 'stormSword'].includes(card.id)) value += basis * (battle.water < 4 ? 12 : 3);
      if (['wavehit', 'waterfall'].includes(card.id)) value += basis * (battle.water >= 4 ? 10 : .1);
      if (card.id === 'waterDraw') value = battle.waterFlowDraws === battle.waterFlowDrawLimit ? value + basis * 30 : -1;
      if (card.id === 'skim' && battle.waterFlowDraws < battle.waterFlowDrawLimit) value += basis * 4;
    }
    if (argument('--mode', 'all') === 'draw-to') {
      if (card.kind === 'draw') value += basis * 30;
      if (card.kind === 'drawTo') value += basis * 10;
    }
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
    await page.getByTestId('fixed-seed-input').check();
    await page.getByTestId('seed-input').fill(String(seed));
    let current = await act(page.getByTestId('start-battle'), '/api/lab/start');
    const opening = structuredClone(current.battle), trace = [];
    check(current.battle && current.selection.buildId === build.id && current.selection.tier === tier, `${build.name} ${tier} ${stage}阶真实开局`);
    if (argument('--mode', 'all') === 'water-vitality') {
      const main = current.loadout.manuals.find(manual => manual.role === '主修');
      assert.equal(main?.id, 'wave'); assert.equal(main.level, 5, '两档沿用既有真实五层配装，不虚称初始三层');
      assert.ok(current.loadout.spells.some(spell => spell.id === 'softwater'), '水构筑携带恢复为补牌辅助的柔水无痕');
      waterVitalityChecks.loadouts.push({ tier, manuals: current.loadout.manuals, spells: current.loadout.spells });
    }
    if (opponent === 'beast-body') await page.screenshot({ path: join(temporary, `${build.id}-${tier}-opening.png`), fullPage: true });
    for (let action = 0; current.battle && action < 500; action++) {
      const battle = current.battle;
      if (['draw-to', 'water-vitality'].includes(argument('--mode', 'all'))) {
        for (const card of battle.hand.filter(card => card.id === 'softwater')) {
          assert.equal(card.kind, 'drawTo');
          assert.equal(card.cost, current.loadout.nodes.some(node => node.id === '2-2') ? 1 : 2, '柔水沿用水道二层的既有减费');
          assert.equal(card.rawDamage, 0); assert.equal(card.damage, 0);
          assert.equal(card.draw, Math.max(0, 8 - (battle.hand.length - 1)), '柔水只按出牌后手牌补至8，不叠加抽牌加成');
          const element = page.locator(`[data-card-uid="${card.uid}"]`);
          assert.ok((await element.locator('.card-type').textContent()).includes('补牌'));
          assert.equal(await element.locator('.card-numbers').textContent(), `抽 ${card.draw} 张`);
          assert.equal(await element.isDisabled(), card.playable === false);
          drawToChecks.seen++;
          if (battle.cooldowns?.softwater > 0) {
            assert.equal(card.playable, false); assert.match(card.reason, /冷却/); drawToChecks.cooldownDisabled++;
          }
          if (!drawToChecks.readOnly) {
            const beforeRead = await state(), afterRead = await state();
            assert.deepEqual(afterRead, beforeRead, '柔水只读补牌预览不洗牌、不改手牌或RNG');
            drawToChecks.readOnly++;
          }
        }
      }
      if (argument('--mode', 'all') === 'water-vitality') {
        assert.equal(battle.enemyActionCount, battle.intentSpells.length);
        assert.ok(battle.enemyActionCount <= battle.enemyActionLimit);
        assert.ok((await page.getByTestId('npc-action-budget').textContent()).includes(`预计行动 ${battle.enemyActionCount} / ${battle.enemyActionLimit}`));
        for (const card of battle.hand) {
          assert.doesNotMatch(card.text || '', /溢疗转伤/, '已取消满血溢疗转伤');
          const facts = waterVitalityFacts(card.text);
          if (!facts) continue;
          assert.equal(facts.potential, Math.floor(facts.direct / 2), '权威分段事实按原命中实际损血50%向下取整');
          assert.equal(Math.min(battle.enemyHp, card.damage), facts.direct, '回复依据原命中实际损血，不含盾挡或超杀');
          if (card.damage < battle.enemyHp) assert.equal(card.damage, facts.direct, '非超杀预览仅含原命中，没有满血额外伤害');
          if (card.heal > 0) assert.ok((await page.locator(`[data-card-uid="${card.uid}"] .card-numbers`).textContent()).includes(`疗 ${card.heal}`));
          waterVitalityChecks.previewFacts++;
        }
      }
      if (argument('--mode', 'all') === 'water-flow') {
        assert.equal(battle.waterFlowDrawLimit, 2, '后端声明共享水攻击补牌预算为每轮两次');
        assert.ok(Number.isInteger(battle.waterFlowDraws) && battle.waterFlowDraws >= 0 && battle.waterFlowDraws <= battle.waterFlowDrawLimit);
        assert.ok(battle.hand.length <= battle.handLimit);
        waterFlowChecks.frames++;
        for (const card of battle.hand.filter(card => waterFlowAttacks.includes(card.id))) {
          const waterAdded = (card.statuses || []).some(status => status.id === 'water' && status.amount > 0);
          const qualifies = ['waterSeal', 'stormSword'].includes(card.id) ? waterAdded : battle.water >= 4;
          const expected = qualifies && battle.waterFlowDraws < battle.waterFlowDrawLimit ? 1 : 0;
          assert.equal(card.draw, expected, '原生水攻击预览按实际新增水势/释放前四层水势及共享预算补牌');
          const element = page.locator(`[data-card-uid="${card.uid}"]`);
          assert.equal(await element.isDisabled(), card.playable === false);
          if (card.draw > 0) assert.ok((await element.locator('.card-numbers').textContent()).includes(`抽 ${card.draw}`));
          if (battle.cooldowns?.[card.id]) assert.fail('四张原生水攻击没有输出冷却');
        }
        if (!waterFlowChecks.readOnly && battle.waterFlowDraws > 0) {
          const first = await state(), second = await state();
          assert.deepEqual(second, first, '水流预览与重复查询不消耗预算、抽牌或推进RNG');
          waterFlowChecks.readOnly++;
        }
      }
      if (argument('--mode', 'all') === 'poison-siphon') {
        for (const card of battle.hand.filter(card => card.id === 'poisonSiphon')) {
          const element = page.locator(`[data-card-uid="${card.uid}"]`);
          assert.equal(card.kind, 'poisonSiphon'); assert.equal(card.cost, 2);
          assert.equal(card.damage, 0); assert.equal(card.rawDamage, 0);
          assert.ok((await element.textContent()).includes('借毒治疗'));
          assert.ok((await element.locator('.card-numbers').textContent()).includes(`疗 ${card.heal}`));
          assert.doesNotMatch(await element.locator('.card-numbers').textContent(), /打出|气血/);
          assert.equal(await element.isDisabled(), card.playable === false);
          poisonSiphonChecks.seen++;
          if (!(battle.poison > 0)) {
            assert.equal(card.playable, false); assert.equal(card.heal, 0);
            assert.ok(card.reason); poisonSiphonChecks.noPoisonDisabled++;
          }
          if (battle.hp >= battle.max) {
            assert.equal(card.playable, false); assert.equal(card.heal, 0);
            assert.ok(card.reason); poisonSiphonChecks.fullHealthDisabled++;
          }
          if (battle.cooldowns?.poisonSiphon > 0) {
            assert.equal(card.playable, false); assert.match(card.reason, /冷却/);
            poisonSiphonChecks.cooldownDisabled++;
          }
        }
      }
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
      trace.push({ turn: battle.turn, mana: battle.mana, hp: battle.hp, enemyHp: battle.enemyHp, uid: chosen?.uid || 'end', spell: chosen?.id || 'end',
        ...(argument('--mode', 'all') === 'water-flow' ? { water: battle.water, waterFlowDraws: battle.waterFlowDraws, previewDraw: chosen?.draw || 0 } : {}) });
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
      } else {
        const doubleDrawTo = argument('--mode', 'all') === 'draw-to' && chosen.id === 'softwater' && !drawToChecks.doubleClick;
        const postCount = posts.length;
        if (chosen.id === 'softwater') await page.screenshot({ path: join(temporary, battle.hand.length >= 9 ? 'draw-to-zero-before.png' : 'draw-to-refill-before.png'), fullPage: true });
        current = await act(page.locator(`[data-card-uid="${chosen.uid}"]`), '/api/lab/turn', doubleDrawTo);
        if (doubleDrawTo) {
          assert.equal(posts.length, postCount + 1, '柔水快速双击只提交一次补牌动作'); drawToChecks.doubleClick++;
        }
      }
      if (chosen?.id === 'softwater' && current.battle) {
        assert.equal(current.battle.hand.length, battle.hand.length - 1 + chosen.draw, '柔水实际先移出本牌再补至8，与权威预览相同');
        assert.ok(!current.battle.hand.some(card => card.uid === chosen.uid), '本次施放的柔水不会被自己的补牌抽回');
        for (const held of battle.hand.filter(card => card.uid !== chosen.uid))
          assert.ok(current.battle.hand.some(card => card.uid === held.uid), '柔水保留其他原有手牌，不因超过8张而弃牌');
        assert.ok(current.battle.cooldowns?.softwater > 0, '柔水保留真实冷却');
        drawToChecks.played++;
        if (battle.hand.length === 8) drawToChecks.preEightPlayed++;
        if (chosen.draw === 0) drawToChecks.zeroDrawPlayed++;
        if (battle.hand.length > 9) drawToChecks.aboveTargetPlayed++;
        if (current.battle.shuffles > battle.shuffles) drawToChecks.reshuffles++;
        drawToChecks.casts.push({ tier, seed, turn: battle.turn, beforeHand: battle.hand, afterHand: current.battle.hand,
          beforeDraw: battle.draw, beforeDiscard: battle.discard, beforeShuffles: battle.shuffles,
          afterShuffles: current.battle.shuffles, preview: chosen, log: current.battle.log });
        await page.screenshot({ path: join(temporary, chosen.draw === 0 ? 'draw-to-zero-after.png' : 'draw-to-refill-after.png'), fullPage: true });
      }
      if (argument('--mode', 'all') === 'water-vitality') {
        if (chosen && current.battle) {
          assert.equal(current.battle.hp - battle.hp, chosen.heal, '自然出牌净回复与即时卡面预览一致');
          assert.equal(battle.enemyHp - current.battle.enemyHp, chosen.damage, '自然出牌实际伤害与预览一致且没有隐藏溢出段');
          const facts = waterVitalityFacts(chosen.text);
          if (facts) {
            const newLog = appendedLogs(battle.log || [], current.battle.log || []).join('\n');
            assert.deepEqual(waterVitalityFacts(newLog), facts, '实际回春分段战报与预览事实一致');
            waterVitalityChecks.hits.push({ tier, seed, turn: battle.turn, spell: chosen.id, hpBefore: battle.hp,
              hpAfter: current.battle.hp, enemyHpBefore: battle.enemyHp, enemyHpAfter: current.battle.enemyHp, preview: chosen, facts, log: newLog });
            if (facts.healing > 0) {
              waterVitalityChecks.healingHits++;
              if (!waterVitalityChecks.savedHealing) {
                await page.screenshot({ path: join(temporary, 'wave-actual-healing.png'), fullPage: true });
                waterVitalityChecks.savedHealing = true;
              }
            }
            if (battle.hp === battle.max && facts.healing === 0) {
              assert.equal(current.battle.hp, battle.hp); waterVitalityChecks.fullHealthHits++;
              if (!waterVitalityChecks.savedFullHealth) {
                await page.screenshot({ path: join(temporary, 'wave-full-health-no-extra-damage.png'), fullPage: true });
                waterVitalityChecks.savedFullHealth = true;
              }
            }
          } else if (chosen.rawDamage > 0 && chosen.damage === 0) waterVitalityChecks.blockedHits++;
        } else if (!chosen && current.battle) {
          const added = appendedLogs(battle.log || [], current.battle.log || []);
          const actions = added.filter(line => line.startsWith('对方施放'));
          assert.equal(actions.length, battle.enemyActionCount, '普通NPC真实续招动作数与出牌后最新预告一致');
          assert.ok(actions.length <= battle.enemyActionLimit, '普通NPC续招仍遵守原行动上限');
          const explicitDraws = actions.filter(line => /抽\s*\d+\s*张/.test(line));
          waterVitalityChecks.npcDrawActions += explicitDraws.length;
          waterVitalityChecks.npcDrawCards += explicitDraws.reduce((sum, line) => sum + Number(line.match(/抽\s*(\d+)\s*张/)[1]), 0);
          waterVitalityChecks.npcRounds++;
          waterVitalityChecks.maximumNpcActions = Math.max(waterVitalityChecks.maximumNpcActions, actions.length);
          waterVitalityChecks.rounds.push({ tier, seed, turn: battle.turn, hpBefore: battle.hp, shieldBefore: battle.shield,
            planned: battle.intentSpells, actionLimit: battle.enemyActionLimit, actions, previousLog: battle.log, latestLog: current.battle.log });
        } else if (!chosen && !current.battle) waterVitalityChecks.terminalRoundsUnobservable++;
      }
      if (argument('--mode', 'all') === 'beast' && chosen?.id === 'fireSiphon') {
        assert.ok(current.battle, '纯治疗不凭空击杀对手');
        assert.equal(current.battle.hp - battle.hp, chosen.heal, '自然手牌真实转换回复与预览一致');
        assert.equal(current.battle.enemyHp, battle.enemyHp); assert.equal(current.battle.burn, 0);
        assert.equal(current.battle.burnTurns, 0); beastChecks.siphonPlayed++;
      }
      if (argument('--mode', 'all') === 'poison-siphon' && chosen?.id === 'poisonSiphon') {
        assert.ok(current.battle, '借毒治疗不击杀对手');
        assert.equal(current.battle.hp - battle.hp, chosen.heal, '借毒治疗实际回复严格等于卡面预览，不额外触发毒疗');
        assert.equal(current.battle.enemyHp, battle.enemyHp, '借毒治疗无直接伤害');
        assert.equal(current.battle.poison, battle.poison, '借毒治疗不消耗敌方毒层');
        assert.ok(current.battle.cooldowns?.poisonSiphon > 0, '借毒治疗施放后进入真实冷却');
        poisonSiphonChecks.played++;
        await page.screenshot({ path: join(temporary, `poison-siphon-${poisonSiphonChecks.played}.png`), fullPage: true });
      }
      if (argument('--mode', 'all') === 'water-flow' && current.battle) {
        if (!chosen) {
          assert.equal(current.battle.waterFlowDraws, 0, '真实回合结束后本方水攻击补牌预算归零');
          waterFlowChecks.roundResets++;
        } else if (waterFlowAttacks.includes(chosen.id)) {
          assert.equal(current.battle.waterFlowDraws - battle.waterFlowDraws, chosen.draw, '水攻击实际兑现预算与抽牌预览一致');
          if (chosen.draw > 0) waterFlowChecks.triggered[chosen.id] = (waterFlowChecks.triggered[chosen.id] || 0) + 1;
          if (battle.waterFlowDraws === battle.waterFlowDrawLimit) waterFlowChecks.cappedAttacks++;
          if (battle.draw >= chosen.draw) assert.equal(current.battle.hand.length, Math.min(battle.handLimit, battle.hand.length - 1 + chosen.draw), '水攻击自然手牌实际补牌数与预览一致');
          if (chosen.draw > 0 && !waterFlowChecks.savedShot) {
            await page.screenshot({ path: join(temporary, 'water-attack-refill.png'), fullPage: true });
            waterFlowChecks.savedShot = true;
          }
        } else {
          assert.equal(current.battle.waterFlowDraws, battle.waterFlowDraws, '普通经营牌不占用水攻击补牌预算');
          if (chosen.id === 'waterDraw') {
            assert.ok(chosen.draw > 0);
            if (battle.draw >= chosen.draw) assert.equal(current.battle.hand.length, Math.min(battle.handLimit, battle.hand.length - 1 + chosen.draw));
            if (battle.waterFlowDraws === battle.waterFlowDrawLimit) {
              waterFlowChecks.independentDraws++;
              await page.screenshot({ path: join(temporary, 'water-independent-refill.png'), fullPage: true });
            }
          }
        }
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
    const policy = argument('--mode', 'all') === 'draw-to' ? 'DrawToNaturalCoverageV1' : argument('--mode', 'all') === 'water-flow' ? 'WaterFlowNaturalCoverageV1'
      : argument('--mode', 'all') === 'poison-siphon' ? 'PoisonSiphonNaturalCoverageV1' : 'LabVisibleGreedyV2';
    return { build: build.id, tier, stage, opponent, seed, policy, won: current.result.won, turns: current.result.turns, actions: trace.length, opening, trace };
  }
  if (['all', 'interaction', 'seed'].includes(argument('--mode', 'all'))) await checkSeedModes();
  if (argument('--mode', 'all') === 'draw-to') {
    const water = config.builds.find(build => build.id === 'water-wave');
    for (const seed of [31, 73, 137, 2012365509]) runs.push(await play(water, 12, 'beast-body', seed));
    check(drawToChecks.played > 0 && drawToChecks.readOnly > 0, '自然手牌施放柔水补至8，重复预览不改变牌堆');
    // 固定水构筑未必自然凑出8/9张且柔水未冷却的手牌；这两条边界由真实C++牌堆集成覆盖，前端零抽牌由check-web覆盖。
    // 浏览器只报告实际走到的分支，不能把未出现的自然牌型写成产品失败或已覆盖。
    check(drawToChecks.casts.every(cast => cast.afterHand.length === Math.max(8, cast.beforeHand.length - 1)),
      '所有自然柔水施放均按本牌离手后的手牌补至8，保留原有其他手牌');
    check(drawToChecks.cooldownDisabled > 0 && drawToChecks.doubleClick > 0, '柔水保留冷却且重复点击不重复结算');
  } else if (argument('--mode', 'all') === 'water-vitality') {
    const water = config.builds.find(build => build.id === 'water-wave');
    runs.push(await play(water, 12, 'xuandao-complete', 2012365509));
    runs.push(await play(water, 12, 'xuandao-complete', 31, 'advanced'));
    check(waterVitalityChecks.healingHits > 0, '五层浪击诀在自然受伤后通过真实主动直伤回血');
    check(waterVitalityChecks.fullHealthHits > 0, '自然满血攻击回复为零且不附加溢出伤害');
    check(waterFlowAttacks.some(id => waterVitalityChecks.hits.some(hit => hit.spell === id && hit.facts.healing > 0)
      && waterVitalityChecks.hits.some(hit => hit.spell === id && hit.facts.healing === 0 && hit.hpBefore === hit.hpAfter)), '同一原生水攻击自然覆盖满血零回复与受伤回复');
    check(waterVitalityChecks.npcRounds > 0 && waterVitalityChecks.npcDrawActions > 0, '普通玄道长回合自然使用抽牌并继续合法手牌，预告与真实动作一致');
  } else if (argument('--mode', 'all') === 'water-flow') {
    const water = config.builds.find(build => build.id === 'water-wave');
    runs.push(await play(water, 6, 'beast-body', 31));
    for (const seed of [31, 73, 137]) runs.push(await play(water, 9, 'beast-body', seed, 'advanced'));
    check(waterFlowAttacks.every(id => waterFlowChecks.triggered[id] > 0), '四张原生水攻击均在自然对局中触发补牌');
    check(waterFlowChecks.cappedAttacks > 0, '自然对局达到共享两次预算后继续攻击不再补牌');
    check(waterFlowChecks.independentDraws > 0, '共享预算用尽后回澜诀仍独立补牌');
    check(waterFlowChecks.roundResets > 0 && waterFlowChecks.readOnly > 0, '自然跨回合恢复预算，重复读取不消耗预算');
  } else if (argument('--mode', 'all') === 'poison-siphon') {
    const wood = config.builds.find(build => build.id === 'wood-poison');
    for (const seed of [1, 31, 73, 137]) runs.push(await play(wood, 3, 'beast-body', seed));
    check(poisonSiphonChecks.seen > 0, '自然手牌抽到借毒回春并展示治疗预览');
    check(poisonSiphonChecks.noPoisonDisabled > 0 && poisonSiphonChecks.fullHealthDisabled > 0, '自然无毒/满血战况中借毒治疗禁用');
    check(poisonSiphonChecks.played > 0 && poisonSiphonChecks.cooldownDisabled > 0, '自然受伤后施毒并借毒治疗，不清毒且冷却阻止连用');
  } else if (argument('--mode', 'all') === 'synergy') {
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
  } else if (!['interaction', 'seed'].includes(argument('--mode', 'all'))) {
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
  if (argument('--mode', 'all') === 'seed') {
    const resultSeed = (await state()).selection.seed;
    const random = await act(page.getByTestId('random-retry-battle'), '/api/lab/start', true);
    check(random.selection.seed !== resultSeed && !(await page.getByTestId('fixed-seed-input').isChecked()), '结果页主按钮随机再来一局并退出固定模式');
    runs.push(await play(config.builds.find(build => build.id === 'mind-cycle'), 6, 'beast-body', 31, 'advanced'));
  }
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
  // 用途：延迟读取返回旧快照时保持最新可见战局；流程：错误后刷新；只替换隔离浏览器一次读取响应。
  await page.route('**/api/lab/state', route => route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(retry) }), { times: 1 });
  await page.locator('[data-action="refresh"]').click();
  await page.waitForFunction(() => document.querySelector('[data-testid="end-turn"]')?.disabled === false);
  const visibleUids = await page.locator('.combat-hand [data-card-uid]').evaluateAll(cards => cards.map(card => card.dataset.cardUid));
  check(JSON.stringify(visibleUids) === JSON.stringify(beforeFailure.battle.hand.map(card => card.uid)), '过期读取响应不覆盖新手牌');
  check(await page.getByTestId('end-turn').isEnabled(), '故障后可刷新恢复真实操作');
  await page.setViewportSize({ width: 600, height: 900 });
  check(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), '600px独立页没有页面横向溢出');
  await page.screenshot({ path: join(temporary, 'narrow.png'), fullPage: true });
  check(errors.length === 0, '所有真实流派试玩无页面脚本错误');
  const sorted = timings.map(item => item.milliseconds).sort((left, right) => left - right);
  const p95 = sorted[Math.floor((sorted.length - 1) * .95)];
  console.log(`SUMMARY ${runs.length} fights; ${timings.length} real actions; p95=${p95.toFixed(1)}ms; evidence=${temporary}`);
  writeFileSync(join(temporary, 'report.json'), JSON.stringify({ checks, runs, synergy, beastChecks, poisonSiphonChecks, waterFlowChecks, waterVitalityChecks, drawToChecks, timings, p95, errors, serverLog }, null, 2));
} catch (error) {
  writeFileSync(join(temporary, 'failure.json'), JSON.stringify({ error: error.stack, checks, runs, synergy, beastChecks, poisonSiphonChecks, waterFlowChecks, waterVitalityChecks, drawToChecks, timings, errors, serverLog }, null, 2));
  throw error;
} finally {
  if (browser) await browser.close();
  server.kill();
}
