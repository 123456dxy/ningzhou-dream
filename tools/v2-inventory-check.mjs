// Inventory/arts integration checks against a disposable real HTTP server.
// Node 18+: node tools/v2-inventory-check.mjs
// Optional: NZ_REPO_ROOT=/repo NZ_WEB_BINARY=/path/to/ningzhou_web
// Creates its own OS-selected port, data directory and account, then removes them.
// Never connects to an existing server and never logs credentials or cookies.
// Render tests are not browser/visual acceptance tests.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { once } from 'node:events';
import { access, mkdtemp, rm } from 'node:fs/promises';
import { createServer } from 'node:net';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(process.env.NZ_REPO_ROOT || join(dirname(fileURLToPath(import.meta.url)), '..'));
const binary = resolve(process.env.NZ_WEB_BINARY || join(root, 'build-v2', process.platform === 'win32' ? 'ningzhou_web.exe' : 'ningzhou_web'));
const { renderInventory } = await import(pathToFileURL(join(root, 'web/v2/views/inventory.mjs')));
const { renderArts } = await import(pathToFileURL(join(root, 'web/v2/views/arts.mjs')));
const { esc } = await import(pathToFileURL(join(root, 'web/v2/ui.mjs')));
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
const temporary = await mkdtemp(join(tmpdir(), 'ningzhou-v2-inventory-'));
let server, serverError, interrupted, origin, cookie = '', renderCases = 0, requests = 0, lastRequestAt = 0;

function interrupt(signal) {
  interrupted = signal;
  server?.kill('SIGTERM');
}
const onInterrupt = () => interrupt('SIGINT');
const onTerminate = () => interrupt('SIGTERM');
process.on('SIGINT', onInterrupt);
process.on('SIGTERM', onTerminate);

async function api(path, body = {}) {
  assert.ok(!interrupted, 'test interrupted');
  await delay(Math.max(0, 75 - (Date.now() - lastRequestAt)));
  lastRequestAt = Date.now();
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), 5000);
  try {
    const response = await fetch(origin + '/api/' + path, {
      method: 'POST', signal: controller.signal,
      headers: { 'content-type': 'application/json', ...(cookie ? { cookie } : {}) },
      body: JSON.stringify(body),
    });
    requests++;
    if (response.headers.has('set-cookie')) cookie = response.headers.get('set-cookie').split(';')[0];
    const result = await response.json();
    // Do not print request bodies, credentials, session IDs or full game states.
    assert.ok(response.ok && result.ok !== false && !result.error, `${path}: ${result.error || response.status}`);
    return result;
  } finally { clearTimeout(timer); }
}

function freeze(value) {
  if (value && typeof value === 'object') {
    Object.values(value).forEach(freeze);
    Object.freeze(value);
  }
  return value;
}
function valid(html, label) {
  assert.ok(!/undefined|NaN/.test(html), `${label}: missing value leaked`);
  renderCases++;
  return html;
}
function button(html, attribute, value) {
  return [...html.matchAll(/<button\b[^>]*>/g)].map(([tag]) => tag)
    .find(tag => tag.includes(`${attribute}="${value}"`));
}
function disabled(tag) {
  assert.ok(tag, 'expected button exists');
  return /\sdisabled(?:\s|>|=)/.test(tag);
}

try {
  await access(binary);
  const reservation = createServer();
  reservation.listen(0, '127.0.0.1');
  await once(reservation, 'listening');
  const port = reservation.address().port;
  await new Promise(resolve => reservation.close(resolve));
  origin = `http://127.0.0.1:${port}`;
  server = spawn(binary, [
    '--host', '127.0.0.1', '--port', String(port), '--web', join(root, 'web'),
    '--data', join(temporary, 'data'), '--feedback', join(temporary, 'feedback'), '--no-open',
  ], { cwd: temporary, stdio: 'ignore' });
  server.on('error', error => { serverError = error; });
  const startupDeadline = Date.now() + 15000;
  let ready = false;
  while (!ready && Date.now() < startupDeadline && !interrupted) {
    if (serverError) throw serverError;
    assert.equal(server.exitCode, null, 'disposable server must remain running');
    try {
      const response = await fetch(origin + '/api/whoami', { signal: AbortSignal.timeout(500) });
      ready = response.ok || response.status === 401;
    } catch { /* A new process can take a moment to bind. */ }
    if (!ready) await delay(100);
  }
  assert.ok(ready, 'disposable server starts within 15 seconds');
  await api('login', { nickname: 'v2viewtest', password: randomBytes(18).toString('base64url') });
  const state = (await api('new', { name: '视图测试', seed: 12122026 })).state;
  const data = {
    nodes: await api('arts/nodes'), spells: await api('arts/spells'),
    economy: await api('eco-options'), forge: await api('forge-options'),
  };
  const forge = { main: 'iron', aux: 'jade', core: 'wood', shape: data.forge.shapes[0].id };
  data.forgePreview = await api('forge-preview', forge);
  assert.equal(state.pillCatalog?.length, state.brewOptions.length, 'new binary exposes every pill');
  assert.ok(state.furnaceRepair, 'new binary exposes authoritative furnace quote');
  freeze(state);
  freeze(data);

  // Forty combinations, followed below by two real acquired-item states.
  for (const inventoryTab of ['bag', 'gear', 'market', 'workshop', 'jobs', 'potions', 'herbs', 'items', 'brew', 'forge', 'furnace']) {
    valid(renderInventory(state, { inventoryTab, forge }, data), inventoryTab);
  }
  for (const workshopTab of ['brew', 'forge', 'furnace']) {
    valid(renderInventory(state, { inventoryTab: 'workshop', workshopTab, forge }, data), workshopTab);
  }
  for (const inventoryKind of ['all', 'potions', 'herbs', 'materials', 'breakthrough']) {
    valid(renderInventory(state, { inventoryTab: 'bag', inventoryKind }, data), inventoryKind);
  }
  for (const artsTab of ['loadout', 'manuals', 'spells', 'daos', 'cave', 'builds']) {
    valid(renderArts(state, { artsTab }, data), artsTab);
  }
  for (const manualFilter of ['learned', 'books', 'sources']) {
    valid(renderArts(state, { artsTab: 'manuals', manualFilter }, data), manualFilter);
  }
  for (const spellFilter of ['known', 'carried', 'learnable', 'all']) {
    valid(renderArts(state, { artsTab: 'spells', spellFilter }, data), spellFilter);
  }
  for (const marketKind of ['potion', 'herb', 'material', 'gear']) {
    for (const marketAvailability of ['all', 'affordable']) {
      valid(renderInventory(state, { inventoryTab: 'market', marketKind, marketAvailability }, data), marketKind);
    }
  }

  const bag = renderInventory(state, { inventoryTab: 'bag' }, data);
  assert.ok(bag.includes('data-ui="inventory-item"') && bag.includes('data-selected-item='));
  assert.ok(bag.includes('data-input="inventory-search"'));
  assert.ok(renderInventory(state, { inventoryTab: 'bag', inventorySearch: '不存在' }, data).includes('没有符合搜索'));
  assert.ok(renderArts(state, { artsTab: 'manuals', manualSearch: '不存在' }, data).includes('没有符合搜索'));
  const dao = renderArts(state, { artsTab: 'daos' }, data);
  assert.equal((dao.match(/data-command="arts\/node"/g) || []).length, 1, 'only the selected node has an action');
  const spells = renderArts(state, { artsTab: 'spells' }, data);
  assert.ok((spells.match(/data-command="arts\/spell"/g) || []).length < 10, 'default spell view is learned-only');
  assert.ok(spells.includes('data-ui="spell-dao"'));
  for (const [render, ui, attr] of [
    [renderInventory, { inventoryTab: 'market', marketAvailability: 'all' }, 'data-selected-market'],
    [renderInventory, { inventoryTab: 'workshop' }, 'data-selected-recipe'],
    [renderArts, { artsTab: 'manuals' }, 'data-selected-manual'],
    [renderArts, { artsTab: 'spells' }, 'data-selected-spell'],
    [renderArts, { artsTab: 'daos' }, 'data-selected-node'],
  ]) {
    const html = valid(render(state, ui, data), 'single selection workspace');
    assert.equal((html.match(new RegExp(attr + '=', 'g')) || []).length, 1, 'exactly one selected detail');
    assert.equal((html.match(/class="player-result/g) || []).length, 1, 'receipt area exists before any operation');
  }
  const initialRecipe = state.brewOptions[0];
  const changedEligibility = { ...state, brewOptions: state.brewOptions.map((item,index) => ({ ...item, enabled: index === 1 })) };
  const stableRecipe = valid(renderInventory(changedEligibility, { inventoryTab: 'workshop' }, data), 'recipe eligibility stability');
  assert.ok(stableRecipe.includes(`data-selected-recipe="${initialRecipe.id}"`));
  const initialNode = (dao.match(/data-selected-node="([^"]+)/) || [])[1];
  const learnedNodeState = { ...state, arts: { ...state.arts, nodes: [...(state.arts.nodes || []), initialNode] } };
  assert.ok(valid(renderArts(learnedNodeState, { artsTab: 'daos' }, data), 'node eligibility stability').includes(`data-selected-node="${initialNode}"`));
  const defaultManual = state.arts.manualList[0];
  if (defaultManual) {
    const learning = valid(renderArts({ ...state, job: { name: '修习功法', remaining: 5 } }, { artsTab: 'manuals', manualId: defaultManual.id }, data), 'local learning continuation');
    assert.ok(learning.includes('继续 5 天至完成') && !learning.includes('class="notice"'));
  }
  const theory = renderArts(state, { artsTab: 'cave' }, data);
  assert.ok(theory.includes('洞府加成') && theory.includes('理论合计'));

  // Realm eligibility alone is not permission to learn from an actual source.
  const unknown = data.spells.spells.find(item => !item.known && item.learnable);
  assert.ok(unknown);
  const noSource = { ...state, librarySpells: [], nearby: [] };
  assert.ok(!renderArts(noSource, { artsTab: 'spells', spellFilter: 'learnable' }, data).includes(unknown.name));
  const withSource = { ...noSource, librarySpells: [{ ...unknown, known: false, enabled: true, points: 10 }] };
  assert.ok(renderArts(withSource, { artsTab: 'spells', spellFilter: 'learnable' }, data).includes('data-command="library/spell"'));

  // Buy a real pill in the disposable game and use authoritative current-player data.
  const potion = state.market.find(item => item.kind === 'potion' && item.price <= state.player.stones);
  assert.ok(potion);
  const potionState = (await api('buy', { kind: 'potion', id: potion.id, count: 1 })).state;
  const potionHTML = valid(renderInventory(potionState, { inventoryTab: 'bag', inventoryKind: 'potions' }, data), 'purchased potion');
  const actualPill = potionState.pillCatalog.find(item => item.id === potion.id);
  assert.ok(potionHTML.includes(potion.name) && potionHTML.includes(esc(actualPill.effect)));
  assert.ok(!potionHTML.includes('药效说明尚未提供'));
  assert.ok(potionHTML.includes(`服后丹毒 ${actualPill.poisonAfter}`));
  assert.ok(potionHTML.indexOf('服后丹毒') < potionHTML.indexOf('<details>'), 'poison result is visible before details');
  assert.ok(potionHTML.includes('即使实际增益为 0'), 'zero-benefit consumption warning remains visible');
  assert.equal(disabled(button(potionHTML, 'data-command', 'use-pill')), !actualPill.enabled);
  assert.ok(potionHTML.indexOf('服后丹毒') < potionHTML.indexOf('data-command="use-pill"'), 'risk stays immediately before primary use action');
  const depletedState = (await api('use-pill', { id: potion.id })).state;
  const pillReceipt = { text: '丹药已服用，持有数量已更新。', path: 'use-pill', itemId: potion.id };
  const depletedHTML = valid(renderInventory(depletedState, { inventoryTab: 'bag', inventoryKind: 'potions', inventoryItem: 'potions:' + potion.id, playerActionResult: pillReceipt }, data), 'depleted selected potion');
  assert.ok(depletedHTML.includes(`data-selected-item="potions:${potion.id}"`) && depletedHTML.includes(pillReceipt.text));
  assert.ok(disabled(button(depletedHTML, 'data-command', 'use-pill')), 'last consumed item stays visible at zero quantity');
  const marketKey = 'potion:' + potion.id;
  const unaffordable = { ...potionState, player: { ...potionState.player, stones: 0 } };
  const retainedMarket = valid(renderInventory(unaffordable, { inventoryTab: 'market', marketItem: marketKey, playerActionResult: { text: '买入完成', path: 'buy', itemId: potion.id } }, data), 'market purchase retention');
  assert.ok(retainedMarket.includes(`data-selected-market="${marketKey}"`) && retainedMarket.includes('灵石不足，还差'));
  assert.ok(disabled(button(retainedMarket, 'data-command', 'buy')));
  const wrongReceipt = valid(renderInventory(potionState, { inventoryTab: 'bag', inventoryKind: 'potions', playerActionResult: { text: '错误物品回执', itemId: 'other', path: 'use-pill' } }, data), 'receipt isolation');
  assert.ok(!wrongReceipt.includes('错误物品回执'));
  const caveCandidate = valid(renderArts(state, { artsTab: 'cave', caveSupply: 'stone:1' }, data), 'unquoted supply recovery');
  assert.ok(caveCandidate.includes('月费待查') && caveCandidate.includes('href="index.html"'));
  assert.ok(disabled(button(caveCandidate, 'data-command', 'supply')));

  // A synthetic distinction complements HTTP coverage: brew eligibility != use eligibility.
  const enriched = {
    ...potionState,
    pillCatalog: [{ id: potion.id, name: potion.name, owned: 1, effect: '服务端真实药效', usage: '服务端用法', enabled: true, used: 2, toleranceRemaining: 3, poisonAfter: 4 }],
    brewOptions: [{ id: potion.id, name: potion.name, enabled: false, reason: '缺材料' }],
  };
  let enrichedHTML = renderInventory(enriched, { inventoryTab: 'bag', inventoryKind: 'potions' }, data);
  assert.ok(enrichedHTML.includes('服务端真实药效') && enrichedHTML.includes('耐药剩余 3 次'));
  assert.ok(!disabled(button(enrichedHTML, 'data-command', 'use-pill')));
  enriched.pillCatalog[0].enabled = false;
  enriched.pillCatalog[0].reason = '服务端禁止服用';
  enrichedHTML = renderInventory(enriched, { inventoryTab: 'bag', inventoryKind: 'potions' }, data);
  assert.ok(disabled(button(enrichedHTML, 'data-command', 'use-pill')) && enrichedHTML.includes('服务端禁止服用'));

  // Reset only this disposable game, then buy and equip real gear.
  let gearState = (await api('new', { name: '装备测试', seed: 12122026 })).state;
  const gear = gearState.market.find(item => item.kind === 'gear' && item.price <= gearState.player.stones);
  assert.ok(gear);
  gearState = (await api('buy', { kind: 'gear', id: gear.id, count: 1 })).state;
  const gearHTML = valid(renderInventory(gearState, { inventoryTab: 'gear' }, data), 'purchased gear');
  const quotes = gearState.gears[0].actions;
  assert.ok(quotes, 'new binary exposes authoritative gear quotes');
  assert.ok(!gearHTML.includes('data-command="gear/salvage"'), 'destructive action requires confirmation UI');
  assert.ok(!gearHTML.includes('收回材料'), 'salvage returns stones, not materials');
  assert.ok(gearHTML.includes(`铭刻 ${quotes.inscribe.costStones} 灵石`));
  assert.ok(gearHTML.includes(`分解 ${quotes.salvage.returnStones} 灵石`));
  assert.ok(gearHTML.includes(`出售 ${quotes.sell.returnStones} 灵石`));
  assert.equal(disabled(button(gearHTML, 'data-ui', 'gear-confirm-salvage')), !quotes.salvage.enabled);
  assert.equal(disabled(button(gearHTML, 'data-ui', 'gear-confirm-sell')), !quotes.sell.enabled);
  assert.equal(disabled(button(gearHTML, 'data-command', 'gear/inscribe')), !quotes.inscribe.enabled);
  assert.ok(gearHTML.includes(esc(quotes.inscribe.reason)));
  const repair = gearState.furnaceRepair;
  const furnace = renderInventory(gearState, { inventoryTab: 'workshop', workshopTab: 'furnace' }, data);
  assert.ok(furnace.includes(`修缮 · ${repair.costStones} 灵石`));
  assert.equal(disabled(button(furnace, 'data-command', 'furnace/repair')), !repair.enabled);
  const equippedState = (await api('equip', { id: gearState.gears[0].id })).state;
  assert.ok(renderInventory(equippedState, { inventoryTab: 'gear' }, data).includes('data-command="gear/unequip"'));

  // Regression: equipping the first accessory must not switch to an empty weapon slot.
  let accessoryState = (await api('new', { name: '配饰测试', seed: 12122026 })).state;
  const accessoryShape = data.forge.shapes.find(item => item.slot === 'accessory');
  assert.ok(accessoryShape, 'server catalog includes an accessory shape');
  const accessoryOffer = accessoryState.market.find(item => item.kind === 'gear'
    && item.id.startsWith(accessoryShape.id + '-') && item.price <= accessoryState.player.stones);
  assert.ok(accessoryOffer, 'disposable opening game can buy an accessory');
  accessoryState = (await api('buy', { kind: 'gear', id: accessoryOffer.id, count: 1 })).state;
  const accessoryId = accessoryState.gears[0].id;
  const beforeEquip = renderInventory(accessoryState, { inventoryTab: 'gear' }, data);
  accessoryState = (await api('equip', { id: accessoryId })).state;
  const afterEquip = renderInventory(accessoryState, { inventoryTab: 'gear' }, data);
  for (const html of [beforeEquip, afterEquip]) {
    assert.ok(html.includes('value="accessory" selected'), 'default slot stays accessory');
    assert.ok(html.includes(`data-disclosure-key="gear-${esc(accessoryId)}"`), 'same item keeps stable disclosure identity');
  }

  for (const blocker of [{ job: { name: '当前安排', remaining: 1 } }, { dungeons: { run: {} } }]) {
    const busy = { ...gearState, ...blocker };
    assert.ok(blocker.job ? button(renderInventory(busy, { inventoryTab: 'gear' }, data), 'data-command', 'advance') : disabled(button(renderInventory(busy, { inventoryTab: 'gear' }, data), 'data-command', 'equip')));
    const artsHTML = renderArts({ ...state, ...blocker }, { artsTab: 'spells' }, data);
    const spellButtons = [...artsHTML.matchAll(/<button\b[^>]*data-command="arts\/spell"[^>]*>/g)];
    assert.ok(blocker.job ? button(artsHTML, 'data-command', 'advance') : spellButtons.length && spellButtons.every(([tag]) => disabled(tag)));
  }
  const hostile = '"><img src=x onerror=alert(1)>';
  assert.ok(!renderInventory(state, { inventoryTab: 'bag', inventorySearch: hostile }, data).includes('<img'));
  assert.ok(!renderArts(state, { artsTab: 'spells', spellSearch: hostile }, data).includes('<img'));
  assert.equal(renderCases, 54);
  console.log(JSON.stringify({
    suite: 'v2-inventory-check', passed: true, renderCases, httpRequests: requests,
    coverage: ['owned item grouping', 'search and filters', 'focused Dao', 'learned spell default', 'source-based learning', 'authoritative pills and quotes', 'gear confirmation boundary', 'stable equipped item selection', 'job/dungeon locks', 'HTML escaping', 'pure frozen-state rendering', 'single selected detail', 'stable eligibility changes', 'local job continuation', 'depleted item retention', 'market affordability retention', 'receipt isolation', 'unquoted supply recovery'],
    isolation: 'fresh disposable server/account; no existing server or browser used',
  }));
} catch (error) {
  console.error(JSON.stringify({ suite: 'v2-inventory-check', passed: false, renderCases, error: error.message }));
  process.exitCode = interrupted === 'SIGINT' ? 130 : interrupted ? 143 : 1;
} finally {
  process.off('SIGINT', onInterrupt);
  process.off('SIGTERM', onTerminate);
  if (server && server.exitCode === null && server.signalCode === null && !serverError) {
    const exited = once(server, 'exit');
    server.kill('SIGTERM');
    const timer = setTimeout(() => server.kill('SIGKILL'), 5000);
    await exited;
    clearTimeout(timer);
  }
  await rm(temporary, { recursive: true, force: true });
}
