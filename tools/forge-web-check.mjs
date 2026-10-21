// Isolated forge and targeted redraw contracts. Does not contact a server or use player saves.
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { mkdir } from 'node:fs/promises';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import vm from 'node:vm';
import { hasNewForge, forgeRequest, forgeEffects, normalizeForgeSelection, editForgeSelection, renderForgeWorkshop } from '../web/forge.mjs';
import { isArtifactRedraw, artifactRedrawSnapshot, artifactRedrawIsCurrent, toggleArtifactRedraw, artifactRedrawRequest, renderArtifactRedraw } from '../web/artifact-redraw.mjs';
import { renderInventory } from '../web/v2/views/inventory.mjs';
import { renderBattleDetail } from '../web/v2/views/battle.mjs';
import { battleStatusRows } from '../web/cards.mjs';
import { renderInventoryPanel } from '../web/inventory-panel.mjs';

let checks = 0;
const check = (condition, message) => { assert.ok(condition, message); checks++; };
const catalog = { version: 2, materials: Array.from({ length: 10 }, (_, index) => ({ id: `material-${index}`, name: `材料${index}`, owned: index ? 1 : 10, grade: 1, element: '金', polarity: '天阳', family: '金属' })),
  shapes: [{ id: 'box', name: '灵匣', category: 'attack' }], effects: [{ id: 'redraw', name: '等量换牌', categories: ['attack'] }], inscriptions: [{ id: 'azure', name: '青龙' }] };
const state = { player: { stones: 100 }, arts: {}, gears: [] };
let selection = normalizeForgeSelection({}, catalog);
check(selection.materials.length === 10 && selection.materials.every(id => id === '') && selection.shape === 'box', '新炉十个独立空位，不擅自消耗或拼装配方');
selection = editForgeSelection(selection, catalog, 'fill', 'material-0');
check(selection.materials.every(id => id === 'material-0') && selection.main === 'material-0', '同名材料允许投入十份，主材包含于投料');
selection = editForgeSelection(selection, catalog, 'clear');
for (let index = 0; index < 10; index++) selection = editForgeSelection(selection, catalog, 'add', `material-${index}`, index);
check(new Set(selection.materials).size === 10, '十种ID均可投入，不误用五个材类限制物品ID');
const sourceCopy = JSON.stringify(catalog);
const unavailable = editForgeSelection(selection, catalog, 'add', 'material-1', 2);
check(unavailable.materials[2] === 'material-2' && JSON.stringify(catalog) === sourceCopy, '库存不足不能在别槽重复投入，不修改材料目录');
selection = editForgeSelection(selection, catalog, 'remove', '0');
check(selection.materials[0] === '' && selection.main !== 'material-0', '单格取回同步修复引灵主材');
const request = forgeRequest(selection, { quoteToken: 'frozen-quote', cost: 1 });
check(request.quoteToken === 'frozen-quote' && !Object.hasOwn(request, 'cost') && !Object.hasOwn(request, 'chance'), '提交选择和报价令牌，不提交客户端成本公式');
const quote = { info: { valid: true, enabled: true, quoteToken: 'quote', grade: 1, chance: .9, days: 8, cost: 30, dimensions: [500, 300, 500, 500, 450], dimensionScore: 2250, energy: 10, utilization: 1, effectText: '先弃牌，再抽取等量牌', availableEffects: catalog.effects } };
const desktop = renderForgeWorkshop(state, catalog, selection, quote);
check((desktop.match(/class="forge-portion[ "']/g) || []).length === 10 && desktop.includes('data-action="forge-run-v2"') && !/data-action="forge-run-v2"[^>]*disabled/.test(desktop), '桌面展示十份独立投料与已报价提交入口');
check(/data-action="forge-run-v2"[^>]*disabled/.test(renderForgeWorkshop(state, catalog, selection, { info: { ...quote.info, quoteToken: '' } })), '缺少当前报价令牌不得开炉');
const combatCostPreview = renderForgeWorkshop(state, catalog, selection, { info: { ...quote.info, ability: { cost: 3, healthCost: 12, cooldown: 2 }, inscriptionText: '白虎以真实气血减费' } });
check(combatCostPreview.includes('战斗消耗：3 灵力 · 12 真实气血 · 冷却 2 回合') && combatCostPreview.includes('白虎以真实气血减费'), '成器前同时显示服务端实际战斗灵力、实血、冷却与灵纹效果，炉费独立展示');
const pendingCatalog = { ...catalog, materials: [{ ...catalog.materials[0], name: '<img onerror=alert(1)>', acquisitionPending: true }] };
const picker = renderForgeWorkshop(state, pendingCatalog, {}, null, { view: { slot: 0 } });
check(picker.includes('&lt;img onerror=alert(1)&gt;') && picker.includes('获取后续开放') && !/data-(?:action|ui)="(?:buy|material-buy)/.test(picker), '目录文案转义，待开放材料没有新增采购动作');
const extensiveCatalog = { ...catalog, materials: Array.from({ length: 575 }, (_, index) => ({ ...catalog.materials[0], id: `many-${index}`, owned: index < 3 ? 1 : 0, family: 'hide', familyName: '皮鳞', part: 'skin', partName: '兽皮鳞' })) };
const ownedPicker = renderForgeWorkshop(state, extensiveCatalog, {}, null, { view: { slot: 0, filters: { owned: true } } });
const wholePicker = renderForgeWorkshop(state, extensiveCatalog, {}, null, { view: { slot: 0, filters: {}, page: 1 } });
check((ownedPicker.match(/class="forge-material(?: |")/g) || []).length === 3 && ownedPicker.includes('皮鳞') && ownedPicker.includes('兽皮鳞'), '目录默认只渲染持有项并显示材类部位中文名');
check((wholePicker.match(/class="forge-material(?: |")/g) || []).length === 48 && wholePicker.includes('第 2 / 12 页'), '查看575材料时每页最多48张，不整页创建数百材料DOM');
const categorized = { ...catalog, shapes: [{ id: 'pearl', name: '珠', slot: 'accessory', category: 'support' }], effects: [{ id: 'draw', slot: 'weapon', category: 'attack' }, { id: 'turn_mana', slot: 'accessory', category: 'support', passive: true }] };
const support = normalizeForgeSelection({ shape: 'pearl', effect: 'draw', inscription: 'azure' }, categorized);
check(forgeEffects(categorized, support).length === 1 && support.effect === 'turn_mana' && support.inscription === '', '真实接口的单数category与slot正确过滤，被动辅助不携带主动灵纹');
const mobile = renderInventory(state, { inventoryTab: 'workshop', workshopTab: 'forge', forge: selection }, { forge: catalog, forgePreview: quote });
check(mobile.includes('forge-mobile') && mobile.includes('data-ui="forge-run-v2"') && mobile.includes('先弃牌，再抽取等量牌'), '掌中界面复用真实十槽预览与操作');
const supportState = { ...state, gears: [{ id: 'new-support', name: '养锋珠', slot: 'accessory', grade: 2, forgeVersion: 2, effectText: '每回合获得1剑气', inscription: '', passive: { text: '每回合获得1剑气' }, ability: null, actions: { equip: { enabled: true } } }] };
const supportDesktop = renderInventoryPanel(supportState, { kind: 'gear' });
const supportMobile = renderInventory(supportState, { inventoryTab: 'gear', gearSlot: 'accessory' });
check(supportDesktop.includes('每回合获得1剑气') && !supportDesktop.includes('data-action="gear/inscribe"')
  && supportMobile.includes('每回合获得1剑气') && !supportMobile.includes('data-command="gear/inscribe"') && !supportMobile.includes('铭刻 · 待报价'), '新版辅助入袋后直接显示主效果，冻结灵纹不出现旧二次铭刻入口');

const battle = { turn: 2, hand: [{ uid: 'one', id: 'one', name: '保留剑', cost: 1, retain: true }, { uid: 'two', id: 'two', name: '水剑', cost: 1 }] };
const ability = { id: 'artifact:redraw', name: '五行流转匣', targetMode: 'hand-many', targetMin: 1, targetMax: 5, playable: true, cost: 3 };
battle.artifacts = [ability];
const snapshot = artifactRedrawSnapshot(battle);
let selected = toggleArtifactRedraw(battle, ability, [], 'one');
selected = toggleArtifactRedraw(battle, ability, selected, 'two');
check(selected.length === 2 && artifactRedrawRequest(battle, ability, selected, snapshot)?.targets.length === 2, '五阶法器仅两张手牌也可弃二抽二，保留牌可主动选');
check(artifactRedrawRequest(battle, ability, [], snapshot) === null && artifactRedrawRequest(battle, ability, ['one', 'one'], snapshot) === null, '空选与重复UID不提交');
check(artifactRedrawRequest(battle, ability, ['missing'], snapshot) === null, '过期或非当前手牌UID不提交');
const changed = { ...battle, hand: [battle.hand[1]] };
check(!artifactRedrawIsCurrent(changed, snapshot) && artifactRedrawRequest(changed, ability, ['two'], snapshot) === null, '打开后手牌变化需重新确认整组选择');
check(artifactRedrawRequest({ ...battle, turn: 3 }, ability, ['one'], snapshot) === null, '跨回合的旧选择不能提交');
const body = artifactRedrawRequest(battle, ability, ['one'], snapshot);
check(body.expectedHand.join('|') === 'one|two' && body.expectedTurn === 2 && body.targets.join() === 'one', '提交开窗时全部UID和回合以便后端校验过期动作');
const redraw = renderArtifactRedraw(battle, ability, ['one'], { mode: 'mobile' });
check(redraw.includes('已选 1 / 2 张') && redraw.includes('确认弃 1 抽 1') && redraw.includes('可主动弃置的保留牌'), '多选反馈是实际可选上限和严格等量，不要求选满');
check(isArtifactRedraw(ability) && !isArtifactRedraw({ kind: 'exhaust', requiresTarget: true }), '批量换牌不改变现有移除牌单选机制');
check(renderBattleDetail({ battle }, 'artifacts').content.includes('artifact-redraw-open'), '掌中法宝进入多选后再提交');
const capRows = battleStatusRows({ swordQi: 9, water: 7, charge: 5, soul: 7, discardPower: .4, resourceCaps: { swordQi: 13, water: 9, charge: 7, soul: 9, discardPower: .55 } });
check(capRows.some(item => item.label === '剑气' && item.value === '9 / 13 层') && capRows.some(item => item.label === '水势' && item.value === '7 / 9 层')
  && capRows.some(item => item.label === '弃牌锐势' && item.value.includes('55%')), '实时状态显示后端冻结的法器上限与比例，不写死基础上限');

const source = readFileSync(new URL('../web/app.mjs', import.meta.url), 'utf8');
const start = source.indexOf('async function refreshForgePreview('), end = source.indexOf('\n// ------------------------------------------------------------', start);
const pending = [];
const context = vm.createContext({ hasNewForge, forgeRequest, forgeOpts: catalog, forgeSelection: forgeRequest({ shape: 'box', effect: 'redraw' }),
  forgePreview: null, forgePreviewSerial: 0, forgePreviewLoading: null, economyRevision: 1, render() {}, economyNeeds: () => ({ forge: true }),
  fetch: (url, options) => new Promise(resolve => pending.push({ url, body: JSON.parse(options.body), resolve })) });
vm.runInContext(source.slice(start, end) + '\nthis.refresh = refreshForgePreview;', context);
const first = context.refresh(false), duplicate = context.refresh(false);
check(pending.length === 1, '相同配方并发推演合并在途请求');
context.forgeSelection.inscription = 'azure';
const second = context.refresh(false);
pending[1].resolve({ ok: true, json: async () => ({ info: { valid: true, marker: 'new' } }) });
await second;
pending[0].resolve({ ok: true, json: async () => ({ info: { valid: true, marker: 'old' } }) });
await Promise.all([first, duplicate]);
check(context.forgePreview.info.marker === 'new', '快速换配方后旧预览不能覆盖当前配方');
context.forgeSelection.inscription = 'white';
const failure = context.refresh(false);
pending[2].resolve({ ok: false, json: async () => ({ error: '隔离网络失败' }) });
await failure;
check(context.forgePreview.info.enabled === false && context.forgePreview.info.reason.includes('隔离网络失败'), '失败预览禁用成器且能显示原因');
const recovery = context.refresh(false);
pending[3].resolve({ ok: true, json: async () => ({ info: { valid: true, marker: 'recovered' } }) });
await recovery;
check(context.forgePreview.info.marker === 'recovered', '预览失败后可以重试恢复，不残留请求锁');
if (process.argv.includes('--dom')) {
  const require = createRequire(import.meta.url);
  let playwright;
  try { playwright = require('playwright'); } catch { playwright = createRequire(process.execPath)('playwright'); }
  const browser = await playwright.chromium.launch({ executablePath: process.env.CHROMIUM_PATH || undefined, headless: true });
  try {
    const page = await browser.newPage({ viewport: { width: 1440, height: 900 } });
    await page.route('**/*', route => route.abort());
    const styles = readFileSync(new URL('../web/forge.css', import.meta.url), 'utf8');
    let fixtureSelection = normalizeForgeSelection({}, catalog), fixtureView = { slot: null, filters: { owned: true } }, fixtureMode = 'desktop';
    const fixtureHtml = () => renderForgeWorkshop(state, catalog, fixtureSelection, { info: { ...quote.info, requiredEnergy: 8, enabled: fixtureSelection.materials.filter(Boolean).length === 10 } }, { mode: fixtureMode, view: fixtureView });
    await page.setContent(`<!doctype html><meta charset="utf-8"><style>body{margin:0;padding:16px;font:14px Arial;background:#f3f4e9}#fixture{max-width:1100px;margin:auto}${styles}</style><main id="fixture">${fixtureHtml()}</main>`);
    await page.exposeFunction('forgeFixtureAction', async (action, value) => {
      if (action === 'slot') fixtureView.slot = Number(value);
      else if (action === 'picker-close') fixtureView.slot = null;
      else if (['add', 'fill', 'remove', 'clear'].includes(action)) {
        fixtureSelection = editForgeSelection(fixtureSelection, catalog, action, value, fixtureView.slot);
        if (['add', 'fill'].includes(action)) { const empty = fixtureSelection.materials.indexOf(''); fixtureView.slot = empty < 0 ? null : empty; }
      }
      await page.locator('#fixture').evaluate((element, html) => { element.innerHTML = html; }, fixtureHtml());
    });
    await page.evaluate(() => document.addEventListener('click', event => {
      const button = event.target.closest('[data-action],[data-ui]');
      if (button && !button.disabled) window.forgeFixtureAction((button.dataset.action || button.dataset.ui).replace('forge-', ''), button.dataset.id || button.dataset.value || '');
    }));
    const started = performance.now();
    await page.locator('[data-action="forge-slot"]').first().click();
    await page.locator('[data-action="forge-fill"][data-id="material-0"]').click();
    await page.waitForFunction(() => document.querySelectorAll('.forge-portion.filled').length === 10);
    check(await page.locator('[data-action="forge-run-v2"]').isEnabled(), '真实DOM点击选材并批量填满十格后可确认');
    check((await page.locator('.forge-workshop').boundingBox()).height < 760, '桌面十槽、五维、主效果、费用与确认同屏');
    const interactionMs = Math.round(performance.now() - started);
    await mkdir(new URL('../tmp/forge-ui-check/', import.meta.url), { recursive: true });
    await page.screenshot({ path: fileURLToPath(new URL('../tmp/forge-ui-check/desktop.png', import.meta.url)), fullPage: true });
    fixtureMode = 'mobile';
    await page.setViewportSize({ width: 390, height: 844 });
    await page.locator('#fixture').evaluate((element, html) => { element.innerHTML = html; }, fixtureHtml());
    const measurements = await page.evaluate(() => ({ overflow: document.documentElement.scrollWidth > innerWidth,
      rows: [...new Set([...document.querySelectorAll('.forge-portion')].map(element => Math.round(element.getBoundingClientRect().top)))].length,
      slots: document.querySelectorAll('.forge-portion').length, submit: document.querySelector('.forge-submit').getBoundingClientRect().height }));
    check(!measurements.overflow && measurements.rows === 2 && measurements.slots === 10 && measurements.submit >= 44, `390px手机十槽五列两行、无横溢且确认命中区足够：${JSON.stringify(measurements)}`);
    await page.screenshot({ path: fileURLToPath(new URL('../tmp/forge-ui-check/mobile.png', import.meta.url)), fullPage: true });
    await page.locator('[data-ui="forge-slot"]').first().click();
    await page.getByRole('button', { name: '取回这一份', exact: true }).click();
    await page.waitForFunction(() => document.querySelectorAll('.forge-portion.filled').length === 9);
    check(await page.locator('[data-ui="forge-run-v2"]').isDisabled(), '手机真实点击取回单份后禁用旧成器报价');
    console.log(JSON.stringify({ browser: 'Chromium', fixtureInteractionMs: interactionMs, mobileMeasurements: measurements }));
  } finally { await browser.close(); }
}
console.log(JSON.stringify({ suite: 'forge-web-check', passed: checks, failed: 0 }));
