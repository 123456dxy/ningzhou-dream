// 只启动本脚本自己的隔离服务和临时账号；绝不连接已有公网或本地玩家服务。
// node tools/ui-regressions-20261012.mjs --binary build-backend-desktop/ningzhou_web.exe
import assert from 'node:assert/strict';
import { spawn, spawnSync } from 'node:child_process';
import { once } from 'node:events';
import { createServer } from 'node:net';
import { existsSync, mkdirSync, mkdtempSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { brewRequirements } from '../web/economy.mjs';
import { inventoryItems, renderCharacterPanel } from '../web/inventory-panel.mjs';
import { renderAchievements } from '../web/sects.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const simulated = args.includes('--fixture');
const binary = resolve(root, args[args.indexOf('--binary') + 1] || 'build-backend-desktop/ningzhou_web.exe');
const output = join(root, 'build', 'ui-regressions');
mkdirSync(output, { recursive: true });
const temporary = mkdtempSync(join(output, 'isolated-'));
const shortPath = path => {
  const result = spawnSync('C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe', ['-NoProfile', '-File', join(root, 'tools/test-temp-path.ps1'), '-Path', path], { encoding: 'utf8', windowsHide: true });
  if (result.status !== 0) throw new Error('无法取得隔离工作区短路径');
  return result.stdout.trim();
};
const nativeTemporary = shortPath(temporary), nativeWeb = shortPath(join(root, 'web'));
const require = createRequire('C:/Users/24974/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/package.json');
const { chromium } = require('playwright');
const browserPath = ['C:/Program Files/Google/Chrome/Application/chrome.exe', 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe', 'C:/Program Files/Microsoft/Edge/Application/msedge.exe'].find(existsSync);
const results = [];
let server, browser, page;
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
function check(ok, name) { results.push({ passed: !!ok, name }); assert.ok(ok, name); console.log('PASS ' + name); }

try {
  const recipe = { id: '5001', enabled: true, stoneCost: 14, days: 4, durabilityCost: 1,
    ingredients: [{ id: 'silver', name: '银月花', required: 1, owned: 1 }] };
  const quoteState = { player: { stones: 1000 }, arts: { durability: 40 } };
  check(brewRequirements(quoteState, recipe, 1).enabled && !brewRequirements(quoteState, recipe, 10).enabled,
    '整批预检：1份材料可炼1炉，不能炼10炉');
  check(brewRequirements(quoteState, recipe, 10).rows[0].missing === 9, '整批缺口为9份');
  const achieved = renderAchievements({ achievements: [{ name: '丹术初成', target: 5, progress: 500, done: true }] });
  check(achieved.includes('已达成') && achieved.includes('width:100%') && !achieved.includes('500 / 5'), '已达成成就不溢出、不继续显示过量进度');

  const reservation = createServer(); reservation.listen(0, '127.0.0.1'); await once(reservation, 'listening');
  const port = reservation.address().port; await new Promise(resolve => reservation.close(resolve));
  const base = `http://127.0.0.1:${port}`;
  const log = [];
  server = simulated
    ? spawn(process.execPath, [join(root,'tools/ui-fixture-20261012.mjs'),'--port',String(port),'--root',root], {cwd:temporary,windowsHide:true,stdio:['ignore','pipe','pipe']})
    : spawn(binary, ['--host', '127.0.0.1', '--port', String(port), '--no-open', '--web', nativeWeb, '--data', join(nativeTemporary, 'data'), '--feedback', join(nativeTemporary, 'feedback')], { cwd: nativeTemporary, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  server.stdout.on('data', data => log.push(data.toString())); server.stderr.on('data', data => log.push(data.toString()));
  server.on('error', error => log.push(error.message));
  for (let i = 0; i < 200; i++) { try { await fetch(base + '/api/whoami'); break; } catch { await wait(50); } }
  browser = await chromium.launch({ executablePath: browserPath, headless: true });
  const context = await browser.newContext({ viewport: { width: 1440, height: 1080 } });
  page = await context.newPage();
  const errors = []; page.on('pageerror', error => errors.push(error.message));
  async function api(path, body = {}) {
    const response = await context.request.post(base + '/api/' + path, { data: body });
    const result = await response.json();
    assert.ok(response.ok() && !result.error, path + ': ' + (result.error || response.status()));
    return result;
  }
  await api('login', { nickname: 'DesktopUI121012', password: 'isolated-test-password' });
  await api('new', { seed: 121012, name: '桌面验收', gender: '女', traits: ['poor'], allocation: {} });
  const initial = JSON.parse((await api('export')).game);
  const fixture = structuredClone(initial);
  fixture.player.location = 'wuling'; fixture.player.stones = 5000; fixture.player.health = 80;
  fixture.player.age = fixture.clock.ageBase = 18;
  fixture.player.arts.herbs = { '银月花': 1, '流莹草': 1, '恒心草': 1 };
  fixture.player.herbs = 3; fixture.player.arts.materials = { iron: 4, jade: 3, wood: 3 };
  fixture.player.arts.potions = { '5001': 2 };
  fixture.npcs = []; fixture.pending = null; fixture.job = null;
  fixture.logs = [
    { year: 0, age: 18, category: 'self', text: '自身纪事验收', important: false },
    { year: 0, age: 18, category: 'world', text: '天下纪事验收', important: true },
    { year: 0, age: 18, category: 'npc', text: '同道纪事验收', important: false },
  ];
  await api('import', { game: JSON.stringify(fixture) });
  await page.goto(base + '/#chronicle'); await page.locator('[data-logfilter="all"]').waitFor();
  check(await page.locator('.innate-stats').innerText().then(text => text.includes('资质') && text.includes('悟性')), '侧栏人物下显示资质悟性');
  await page.locator('[data-logfilter="world"]').click();
  check(await page.locator('.timeline .log-entry').count() === 1 && await page.locator('.timeline').innerText().then(text => text.includes('天下纪事验收') && !text.includes('自身纪事验收')), '真实DOM点击天下分类生效');
  await page.locator('[data-logfilter="important"]').click();
  check(await page.locator('.timeline .log-entry').count() === 1, '真实DOM点击大事分类生效');
  await page.locator('[data-plan-select]').selectOption('rest');
  check((await api('state')).state.plan === 'rest', '紧凑单选计划真实提交且仅有一个当前计划');
  const size = await page.locator('[data-chronicle-resize]').boundingBox();
  await page.mouse.move(size.x + size.width - 5, size.y + size.height - 5); await page.mouse.down();
  await page.mouse.move(size.x + size.width - 5, size.y + size.height + 100, { steps: 10 }); await page.mouse.up();
  await wait(180);
  const draggedHeight = await page.locator('[data-chronicle-resize]').evaluate(node => node.offsetHeight);
  check(draggedHeight > 390, '纪事高度可以通过真实鼠标拖动增大');
  await page.reload(); await page.locator('[data-chronicle-resize]').waitFor();
  check(Math.abs(await page.locator('[data-chronicle-resize]').evaluate(node => node.offsetHeight) - draggedHeight) < 3, '纪事拖动高度重新加载后保留');
  await page.screenshot({ path: join(output, 'chronicle.png'), fullPage: true });

  await page.locator('.main-tabs [data-tab="economy"]').click();
  for (const category of ['city', 'sect', 'wild', 'story']) { await page.locator(`[data-subtab="economy"][data-sub="${category}"]`).click(); check(await page.locator(`[data-subtab="economy"][data-sub="${category}"].on`).count() === 1, `任务分类${category}真实可点击`); }
  const story = page.locator('[data-action="story"][data-id="letter"]'); await story.click();
  check(await page.locator('[data-story="letter"]').count() === 2, '支线真实DOM点击打开两项分支');
  const beforeStory = (await api('state')).state.clock.days;
  await page.locator('[data-story="letter"][data-choice="0"]').click();
  await page.waitForFunction(() => !document.querySelector('.story-overlay'));
  await wait(250);
  check((await api('state')).state.clock.days === beforeStory + 3, '支线分支真实点击结算并兑现3天耗时');

  await page.locator('.main-tabs [data-tab="market"]').click();
  check(await page.locator('[data-brew-batch],[data-forge],[data-action="furnace/repair"]').count() === 0, '坊市页面只显示交易入口');
  check(await page.locator('[data-shop-grade]').inputValue()==='1','坊市品阶默认一阶');
  await page.locator('[data-shop="potion"]').click();
  check(await page.locator('[data-pill-catalog-effect]').innerText().then(text=>text.includes('回复气血20点')&&!text.includes('回复气血0点')),'坊市丹药显示完整效果，忽略当前健康度截断描述');
  await page.locator('[data-shop-grade]').selectOption('2');
  await page.locator('[data-shop="herb"]').click();
  check(await page.locator('[data-shop-grade]').inputValue()==='1','坊市各类型分别记忆品阶');
  await page.locator('[data-shop="potion"]').click();
  check(await page.locator('[data-shop-grade]').inputValue()==='2','切回丹药保留上次二阶');
  await page.reload(); await page.locator('[data-shop-grade]').waitFor();
  check(await page.locator('[data-shop-grade]').inputValue()==='2'&&await page.locator('[data-shop="potion"].active').count()===1,'重新加载保留坊市类型与品阶');
  await page.locator('[data-shop="book"]').click();
  check(await page.locator('#workspace [data-action="trade-stock"]').count()===0&&await page.locator('[data-shop-grade]').count()===0,'书坊并入同一分类，功法不伪造物品品阶');
  await page.locator('[data-shop="stock"]').click();
  check(await page.locator('#workspace [data-action="trade-stock"]').count()===4,'灵核与凝元丹仅在突破物品分类展示');
  if (simulated) {
    const fullHealthFixture=structuredClone(fixture); fullHealthFixture.player.health=100;
    await api('import',{game:JSON.stringify(fullHealthFixture)}); await page.reload();
    await page.locator('[data-shop="potion"]').click(); await page.locator('[data-shop-grade]').selectOption('1');
    check(await page.locator('[data-pill-catalog-effect]').innerText().then(text=>text.includes('回复气血20点')&&!text.includes('回复气血0点')),'满血时坊市目录仍展示完整20点效果');
    await page.locator('.main-tabs [data-tab="inventory"]').click(); await page.locator('[data-inventory-kind="potion"]').click();
    await page.locator('[data-inventory-pill-details="5001"] summary').click();
    check(await page.locator('[data-inventory-pill-details="5001"]').innerText().then(text=>text.includes('当前回复气血0点')),'满血时具体服用详情准确展示当前实际回复0点');
    await api('import',{game:JSON.stringify(fixture)}); await page.reload();
  }
  await page.locator('.main-tabs [data-tab="inventory"]').click();
  await page.locator('[data-inventory-kind="potion"]').click();
  check(await page.locator('[data-inventory-item]').count() === 1 && await page.locator('[data-inventory-item="5001"]').innerText().then(text => text.includes('化淤丹')), '行囊类型筛选显示中文丹药');
  await page.locator('[data-inventory-grade]').selectOption('1');
  check(await page.locator('[data-inventory-item="5001"]').count() === 1, '品阶筛选保留一阶丹药');
  await page.locator('[data-inventory-pill-details="5001"] summary').click();
  check(await page.locator('[data-inventory-pill-details="5001"][open]').count()===1
    && await page.locator('[data-inventory-pill-details="5001"]').innerText().then(text=>text.includes('当前服用效果')&&text.includes('丹毒')&&text.includes('持有 2 枚')),
    '点击具体化淤丹详情展示真实效果、丹毒和持有量');
  const beforePill = (await api('state')).state.arts.potions['5001'];
  await page.locator('[data-inventory-item="5001"] [data-action="use-pill"][data-id="5001"]').click(); await wait(400);
  check((await api('state')).state.arts.potions['5001'] === beforePill - 1, '行囊服用按钮实际消耗一枚丹药');
  await page.locator('[data-subtab="inventory"][data-sub="character"]').click();
  check(await page.locator('#workspace [data-action="use-pill"]').count()===0
    && !await page.locator('#workspace button').allTextContents().then(labels=>labels.some(text=>text.includes('服用化淤丹')))
    && await page.locator('#toast').isHidden(), '人物面板没有任何固定服药入口，也不残留化淤丹浮动反馈');
  check(await page.locator('.talent-detail').innerText().then(text => text.includes('家境清寒') && !text.includes('poor')), '人物面板天赋显示中文名称');
  await page.screenshot({ path: join(output, 'inventory-character.png'), fullPage: true });

  await api('import', { game: JSON.stringify(fixture) }); await page.reload();
  await page.locator('.main-tabs [data-tab="workshop"]').click();
  check(await page.locator('[data-brew-grade]').inputValue()==='1','炼丹目录默认一阶');
  await page.locator('[data-brew-grade]').selectOption('2');
  await page.reload(); await page.locator('[data-brew-grade]').waitFor();
  check(await page.locator('[data-brew-grade]').inputValue()==='2'&&await page.locator('.craft-card').count()===1,'炼丹品阶筛选刷新后保留，目录只显示所选品阶');
  await page.locator('[data-brew-grade]').selectOption('1');
  await page.locator('[data-brew-search]').fill('化淤丹');
  await page.locator('[data-brew-batch]').selectOption('10');
  check(await page.locator('[data-action="craft"][data-id="5001"]').isDisabled(), '选择10炉即时禁用材料只够1炉的开炉按钮');
  check(await page.locator('[data-action="craft"][data-id="5001"]').innerText().then(text => text.includes('缺 9') && text.includes('140 灵石')), '整批展示精确缺口和预计投入');
  const brewSupply = {kind:'brew',id:'5001',batch:10};
  const brewQuote = (await api('craft/materials/quote',brewSupply)).quote;
  await page.locator('[data-action="craft-supply-quote"]').click();
  await page.locator('[data-action="craft-supply-buy"]').waitFor();
  check(await page.locator('.craft-supply').innerText().then(text=>text.includes(String(brewQuote.totalCost))&&text.includes('确认购买')), '一键炼丹补料先显示服务端整批缺口和报价');
  const supplyBefore = (await api('state')).state;
  await page.locator('[data-action="craft-supply-buy"]').click(); await wait(350);
  const supplyAfter = (await api('state')).state;
  check(supplyAfter.player.stones === supplyBefore.player.stones - brewQuote.totalCost
    && !await page.locator('[data-action="craft"][data-id="5001"]').isDisabled(), '确认补料只按报价扣灵石，补齐10炉后开炉即时可用');
  await page.locator('[data-brew-batch]').selectOption('1');
  check(!(await page.locator('[data-action="craft"][data-id="5001"]').isDisabled()), '切回1炉恢复可开炉');
  check(await page.locator('.craft-card[data-id="5001"] [data-pill-catalog-effect]').innerText().then(text => text.includes('丹药完整效果')&&text.includes('回复气血20点')), '开炉前显示服务端丹药完整效果');
  await page.locator('[data-subtab="workshop"][data-sub="forge"]').click();
  await page.locator('.forge-preview [data-numeric-preview]').first().waitFor();
  check(await page.locator('.forge-preview [data-numeric-preview]').count() > 0 && await page.locator('.forge-passive').count() === 1, '炼器开炉前同时展示权威装备被动和法宝神通数值');
  await page.screenshot({ path: join(output, 'workshop-forge.png'), fullPage: true });
  const emptyMaterialsFixture = structuredClone(fixture); emptyMaterialsFixture.player.arts.materials={iron:0,jade:0,wood:0};
  await api('import',{game:JSON.stringify(emptyMaterialsFixture)}); await page.reload();
  await page.locator('[data-subtab="workshop"][data-sub="forge"]').click(); await page.locator('.forge-preview-heading').waitFor();
  const forgeSupply={kind:'forge',main:'iron',aux:'jade',core:'wood',shape:'sword'};
  const forgeQuote=(await api('craft/materials/quote',forgeSupply)).quote;
  await page.locator('[data-action="craft-supply-quote"]').click(); await page.locator('[data-action="craft-supply-buy"]').waitFor();
  check(await page.locator('.craft-supply').innerText().then(text=>text.includes(String(forgeQuote.totalCost))&&text.includes('锻剑铁')),'炼器一键补料按当前三项配料显示真实报价');
  await page.locator('[data-action="craft-supply-buy"]').click(); await wait(350);
  const forgedSupplyState=(await api('state')).state;
  check(forgedSupplyState.arts.materials.iron===4&&forgedSupplyState.arts.materials.jade===3&&forgedSupplyState.arts.materials.wood===3,'炼器补料真实按钮按4/3/3配料只补不足');
  if (simulated) {
    const recoveryFixture = structuredClone(fixture);
    recoveryFixture.gears=[{id:'fixture-blade',name:'锻剑铁剑',slot:'weapon',grade:1,equipped:false,
      salvage:{enabled:true,costStones:5,estimated:true,sourceNote:'旧法宝来源信息不足，按品阶估算',materials:[{id:'iron',name:'锻剑铁',count:2}]}}];
    await api('import',{game:JSON.stringify(recoveryFixture)});
    await page.goto(base+'/#equipment'); await page.reload(); await page.locator('[data-action="gear-salvage-preview"]').waitFor();
    check(await page.locator('.main-tabs [data-tab="inventory"].active').count()===1,'旧装备hash映射到新行囊页面');
    await page.locator('[data-action="gear-salvage-preview"]').click();
    check(await page.locator('#modal-content').innerText().then(text=>text.includes('锻剑铁 ×2')&&text.includes('5 灵石')&&text.includes('估算')),'回收确认前显示返还材料、手续费和旧档估算来源');
    const recoveryBefore=(await api('state')).state;
    await page.locator('[data-action="gear-salvage-confirm"]').click(); await wait(350);
    const recoveryAfter=(await api('state')).state;
    check(recoveryAfter.gears.length===0&&recoveryAfter.arts.materials.iron===recoveryBefore.arts.materials.iron+2
      && recoveryAfter.player.stones===recoveryBefore.player.stones-5,'回收确认按钮仅执行一次材料返还与手续费动作');
    await page.goto(base+'/#forge'); await page.locator('.forge-preview-heading').waitFor();
    check(await page.locator('[data-subtab="workshop"][data-sub="forge"].on').count()===1,'旧炼器hash映射到工坊炼器子页');
    await page.goto(base+'/#market/brew'); await page.locator('[data-brew-batch]').waitFor();
    check(await page.locator('.main-tabs [data-tab="workshop"].active').count()===1,'旧坊市炼丹链接映射到工坊炼丹');
  }
  await page.locator('.main-tabs [data-tab="quests"]').click(); await page.locator('[data-subtab="quests"][data-sub="renown"]').click();
  check(await page.locator('#workspace').innerText().then(text => !text.includes('本世档案') && !text.includes('灵根资质') && !text.includes('出身天赋')), '成就页没有重复人物档案');
  if (simulated) {
    await page.setViewportSize({width:1920,height:1080}); await page.goto(base+'/#brew');
    await page.locator('.craft-card').first().waitFor();
    const compact = await page.locator('.craft-grid').evaluate(grid=>({columns:getComputedStyle(grid).gridTemplateColumns.split(' ').length,
      cards:[...grid.querySelectorAll('.craft-card')].map(card=>({width:card.getBoundingClientRect().width,topGap:card.querySelector('.craft-top').getBoundingClientRect().top-card.getBoundingClientRect().top,display:getComputedStyle(card).display}))}));
    check(compact.columns<=4&&compact.cards.every(card=>card.width>=270&&card.topGap<=24&&card.display==='flex'),'1920px炼丹目录不挤成五列，所有短卡内容顶对齐无巨大留白');
    await page.screenshot({path:join(output,'workshop-brew-compact.png'),fullPage:true});
    await page.locator('.main-tabs [data-tab="dungeon"]').click(); await page.locator('[data-dungeon-view="intel"]').click();
    check(await page.locator('[data-dungeon-intel-grade]').inputValue()==='3'&&await page.locator('.dungeon-intel-item').count()===2,'天机阁情报默认三阶，不混入四阶目录');
    check(await page.locator('[data-action="dungeon-intel"]').evaluateAll(buttons=>buttons.every(button=>button.disabled)),'不在天机阁时所有情报购买仍禁用');
    await page.locator('[data-dungeon-intel-grade]').selectOption('4');
    await page.locator('[data-dungeon-intel-kind="material"]').click();
    await page.locator('[data-dungeon-intel-search]').fill('仙露草');
    check(await page.locator('.dungeon-intel-item').count()===0,'四阶加炼器材料加药材关键词不产生错误交集');
    await page.locator('[data-dungeon-intel-search]').fill('寒晶');
    check(await page.locator('.dungeon-intel-item').count()===1&&await page.locator('.dungeon-intel-item h4').innerText()==='寒晶秘境','情报品阶、类型和关键词组合筛选生效');
    await page.reload(); await page.locator('[data-dungeon-view="intel"]').click();
    check(await page.locator('[data-dungeon-intel-grade]').inputValue()==='4','重新加载后记住上次四阶情报选择');
    const atIntelFixture=structuredClone(fixture); atIntelFixture.player.location='tianji';
    await api('import',{game:JSON.stringify(atIntelFixture)}); await page.reload(); await page.locator('[data-dungeon-view="intel"]').click();
    await page.locator('[data-dungeon-intel-kind="material"]').click(); await page.locator('[data-dungeon-intel-search]').fill('寒晶');
    check(await page.locator('.dungeon-intel-item').innerText().then(text=>text.includes('境界不足'))&&await page.locator('[data-action="dungeon-intel"]').isDisabled(),'抵达天机阁后境界不足条目继续禁用并说明原因');
    await page.screenshot({path:join(output,'intel-grade-filter.png'),fullPage:true});
  }
  check(errors.length === 0, '完整桌面流程无浏览器脚本异常');
  writeFileSync(join(output, 'server-test.log'), log.join(''), 'utf8');
} catch (error) {
  results.push({ passed: false, name: error.message }); console.error(error.stack); process.exitCode = 1;
  if (page) { try { await page.screenshot({ path: join(output, 'failure.png'), fullPage: true }); } catch {} }
} finally {
  if (browser) await browser.close();
  if (server && server.exitCode === null) server.kill();
  writeFileSync(join(output, simulated?'fixture-results.json':'results.json'), JSON.stringify({ simulatedAPI:simulated,checks: results.length, passed: results.filter(item => item.passed).length, results }, null, 2));
  console.log(`Desktop UI: ${results.filter(item => item.passed).length}/${results.length} passed.`);
}
