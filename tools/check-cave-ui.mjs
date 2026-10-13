// 洞府独立纯展示契约检查；不启动服务器，不读取玩家存档，不代替真实浏览器测试。
import assert from 'node:assert/strict';
import { renderCavePage } from '../web/cave-panel.mjs';

let checks = 0;

// 用途：记录一个独立展示契约断言。
// 流程：洞府展示检查入口。
// 使用方式：传布尔结果与中文说明；失败抛出断言错误。
// 返回值：无；通过时增加检查计数。
function verify(condition, description) {
  assert.ok(condition, description);
  checks += 1;
}

const fixture = {
  player: { stones: 1234, caveName: '青石灵脉', locationName: '青石灵脉' },
  cave: {
    access: { atHome: true, homeLocation: 'qingshi', homeName: '青石灵脉', reason: '', canReturn: false,
      settle: { enabled: false, reason: '洞府已经在这里了。', costStones: 40 } },
    training: [{ id: 'arena', name: '演武切磋', desc: '增进剑道与斗法心得', cost: 10, days: 2,
      enabled: true, reason: '' }],
    garden: {
      plotCountUnlocked: 3, plantsPerPlotLimit: 99, harvestMultiplier: 3, hasGreenBottle: true,
      accessReason: '', growthCostRule: '费用由未成熟种苗、品阶与加速方案决定。',
      plantOptions: [{ herbId: 'herb-a', name: '青灵草', grade: 1, ownedCount: 12, baseGrowthDays: 30 },
        { herbId: 'herb-b', name: '云香花', grade: 2, ownedCount: 5, baseGrowthDays: 60 }],
      plots: [{ plotId: 'plot-1', herbId: '', name: '', grade: 0, seedCount: 0, growingCount: 0,
        readyCount: 0, remainingCapacity: 99, reserveStones: 0, dailyCostStones: 0,
        plantEnabled: true, plantReason: '', deposit: { enabled: true, reason: '' },
        withdraw: { enabled: false, reason: '此格没有储备灵石。' }, clear: { enabled: false, reason: '没有种苗。' },
        harvest: { enabled: false, reason: '尚未种植。', harvestCount: 0 }, cohorts: [] },
      { plotId: 'plot-2', herbId: 'herb-a', name: '青灵草', grade: 1, seedCount: 20, growingCount: 15,
        readyCount: 5, remainingCapacity: 79, reserveStones: 80, requiredUnits: 3000,
        naturalUnitsPerDay: 100, spiritUnitsPerDay: 160, bottleUnitsPerDay: 300,
        fullyAcceleratedUnitsPerDay: 360, effectiveUnitsPerDay: 360,
        spiritDailyCostStones: 30, bottleDailyCostStones: 15, dailyCostStones: 45,
        estimatedNaturalDays: 30, estimatedSpiritDays: 19, estimatedBottleDays: 10,
        estimatedFullyAcceleratedDays: 9, estimatedEffectiveDays: 9, accelerationFunded: true,
        plantEnabled: true, plantReason: '', deposit: { enabled: true, reason: '' },
        withdraw: { enabled: true, reason: '' }, clear: { enabled: true, reason: '' },
        harvest: { enabled: true, reason: '', harvestCount: 15 },
        cohorts: [{ count: 5, progressUnits: 3000, ready: true }, { count: 15, progressUnits: 0, ready: false }] }],
    },
  },
};

const before = JSON.stringify(fixture);
const html = renderCavePage(fixture, { supplyHtml: '<section data-test-supply>服务端配给区域</section>' });
verify(html.includes('洞府') && html.includes('青石灵脉'), '洞府页显示归属地点');
verify(html.includes('在府') && !html.includes('cave-enter') && !html.includes('cave-leave'), '沿用真实地点，不引入进入离开旗标');
verify(html.includes('服务端配给区域'), '配给区域由根页面现有权威渲染传入');
verify(!html.includes('炼丹') && !html.includes('炼器') && !html.includes('goto-workshop'), '洞府不重复嵌入工坊制作');
verify(html.includes('data-cave-plant-count') && html.includes('max="12"'), '播种数量上限合并后端剩余容量与所选药材库存');
verify(html.includes('data-action="cave-garden-plant"') && html.includes('data-plot-id="plot-1"'), '播种动作标识与点位完整');
verify(html.includes('data-action="cave-garden-deposit"') && html.includes('data-action="cave-garden-withdraw"'), '储备操作有明确存取动作');
verify(!html.includes('cave-garden-accelerate') && !html.includes('加速一次'), '自动加速不再提供单次收费按钮');
verify(html.includes('45 灵石 / 天') && html.includes('80 灵石'), '单日费用与本格储备读取权威数据');
verify(html.includes('9 天') && html.includes('30 天') && html.includes('10 天'), '成熟时间展示后端预估，不重算成长');
verify(html.includes('成熟可收 15 份') && html.includes('5 份') && html.includes('15 份'), '成熟收获量与独立种植批次可见');
verify(html.includes('data-action="cave-garden-clear"'), '放弃种苗有独立动作供主入口确认');
verify(html.includes('data-action="cave-train"') && html.includes('data-id="arena"') && html.includes('10 灵石') && html.includes('2 天'), '训练场展示真实演武报价');
verify(JSON.stringify(fixture) === before, '纯展示不修改调用方状态或批次进度');

const noBottle = structuredClone(fixture);
noBottle.cave.garden.hasGreenBottle = false;
const noBottleHtml = renderCavePage(noBottle);
verify(!noBottleHtml.includes('data-cave-reserve-amount') && !noBottleHtml.includes('cave-garden-deposit')
  && !noBottleHtml.includes('cave-garden-withdraw'), '无瓶完全隐藏储备存入取出控件');
verify(!noBottleHtml.includes('自动加速') && !noBottleHtml.includes('全部加速')
  && noBottleHtml.includes('按当前成长预计全部成熟约'), '无瓶隐藏付费加速方案但保留真实成长时间');
const lowCapacity = structuredClone(fixture);
lowCapacity.cave.garden.plots[1].remainingCapacity = 1;
const lowCapacityHtml = renderCavePage(lowCapacity, { plantSelections: { 'plot-2': { count: 99 } } });
verify(/min="1" max="1" step="1" value="1"/.test(lowCapacityHtml), '本格剩余一份容量时草稿收束到一份');

const outside = structuredClone(fixture);
outside.cave.access.atHome = false;
outside.cave.access.canReturn = true;
outside.cave.access.reason = '请先回到洞府所在地。';
outside.cave.training[0].enabled = false;
outside.cave.training[0].reason = '请先回到洞府所在地。';
outside.cave.garden.accessReason = '请先回到洞府所在地。';
for (const plot of outside.cave.garden.plots) {
  plot.plantEnabled = false;
  plot.plantReason = '请先回到洞府所在地。';
  for (const action of ['harvest', 'deposit', 'withdraw', 'clear']) {
    plot[action].enabled = false;
    plot[action].reason = '请先回到洞府所在地。';
  }
}
const outsideHtml = renderCavePage(outside);
verify(outsideHtml.includes('外出') && outsideHtml.includes('data-action="cave-return"'), '外出可以查看并提供真实返府旅行入口');
verify(/data-action="cave-train"[^>]*disabled/.test(outsideHtml), '训练可用性来自后端拒绝资格');
verify(/data-action="cave-garden-harvest"[^>]*disabled/.test(outsideHtml), '外出成熟药材仍显示但禁止收获');
verify(outsideHtml.includes('45 灵石 / 天') && outsideHtml.includes('自动加速'), '外出时后台成长与格内付费状态仍然可见');

const missingHtml = renderCavePage({ player: {} });
verify(!missingHtml.includes('data-action="cave-garden-') && !missingHtml.includes('data-action="cave-train"'), '缺少权威状态不伪造操作资格');
verify(missingHtml.includes('正在读取'), '缺少灵田状态明确显示读取状态');

const escaped = structuredClone(fixture);
escaped.cave.access.homeName = '<img src=x onerror=alert(1)>';
escaped.cave.garden.plots[1].name = '<script>alert(1)</script>';
escaped.cave.training[0].reason = '" onclick="alert(1)';
const escapedHtml = renderCavePage(escaped);
verify(!escapedHtml.includes('<script>') && !escapedHtml.includes('<img src=x'), '地点和药材名称严格HTML转义');
verify(escapedHtml.includes('&lt;script&gt;') && escapedHtml.includes('&quot; onclick=&quot;'), '动态标题与拒绝原因不会变成代码或属性');

// 用途：从纯展示 HTML 读取指定灵田的控件范围。
// 流程：药材筛选和已种田锁定行为检查。
// 使用方式：传页面 HTML 和测试夹具的稳定 plotId，不执行 DOM 或服务器动作。
// 返回值：该灵田 article 的 HTML；找不到返回空文本。
function plotHtml(pageHtml, plotId) {
  return pageHtml.match(new RegExp(`<article[^>]*data-cave-plot="${plotId}"[^>]*>([\\s\\S]*?)</article>`))?.[1] || '';
}

const catalogFixture = structuredClone(fixture);
catalogFixture.cave.garden.plantOptions.push(
  { herbId: 'herb-d', name: '灵香芝', grade: 3, ownedCount: 6, baseGrowthDays: 120 },
  { herbId: 'herb-e', name: '星香芝', grade: 6, ownedCount: 1, baseGrowthDays: 960 },
);
const catalogBefore = JSON.stringify(catalogFixture);
const defaultFilterHtml = renderCavePage(catalogFixture);
const defaultEmptyPlot = plotHtml(defaultFilterHtml, 'plot-1');
verify(defaultFilterHtml.includes('data-cave-plant-grade="1"') && defaultFilterHtml.includes('data-cave-plant-grade="all"')
  && defaultFilterHtml.includes('data-cave-plant-grade="6"') && !defaultFilterHtml.includes('data-cave-plant-grade="5"'),
  '品阶由可播种目录提取并提供全部选项，不伪造不存在的品阶');
verify(/data-cave-plant-grade="1"[^>]*aria-pressed="true"/.test(defaultFilterHtml)
  && defaultFilterHtml.includes('仅列行囊已有药材') && !defaultFilterHtml.includes('data-cave-plant-owned'),
  '默认选一阶并说明权威目录仅列已有药材，不提供无效库存开关');
verify(defaultEmptyPlot.includes('value="herb-a"') && !defaultEmptyPlot.includes('value="herb-b"')
  && !defaultEmptyPlot.includes('value="herb-d"'), '空灵田默认只显示一阶有库存药材');

const gradeHtml = renderCavePage(catalogFixture, { plantFilters: { grade: '3', search: '' },
  plantSelections: { 'plot-1': { herbId: 'herb-a', count: 7 } } });
const gradeEmptyPlot = plotHtml(gradeHtml, 'plot-1');
verify(gradeEmptyPlot.includes('value="herb-d" selected') && !gradeEmptyPlot.includes('value="herb-a"')
  && gradeEmptyPlot.includes('value="6"'), '切换品阶舍弃不可见的旧药材草稿，份数收束到新药材库存');
const lockedPlot = plotHtml(gradeHtml, 'plot-2');
verify(lockedPlot.includes('value="herb-a" selected') && !lockedPlot.includes('value="herb-d"')
  && /data-cave-plant-herb[^>]*disabled/.test(lockedPlot)
  && !/data-action="cave-garden-plant"[^>]*disabled/.test(lockedPlot), '已种田忽略全局筛选，锁定同种并允许补种');

const searchHtml = renderCavePage(catalogFixture, { plantFilters: { grade: 'all', search: '香芝' } });
const searchedPlot = plotHtml(searchHtml, 'plot-1');
verify(searchedPlot.includes('value="herb-d"') && searchedPlot.includes('value="herb-e"')
  && !searchedPlot.includes('value="herb-a"') && !searchedPlot.includes('value="herb-b"'), '名称搜索可跨品阶匹配当前目录');

const emptySearchHtml = renderCavePage(catalogFixture, { plantFilters: { grade: 'all', search: '不存在' },
  plantSelections: { 'plot-1': { herbId: 'herb-a', count: 12 } } });
const emptySearchPlot = plotHtml(emptySearchHtml, 'plot-1');
verify(emptySearchPlot.includes('<option value="">没有符合筛选的药材</option>')
  && /data-action="cave-garden-plant"[^>]*disabled/.test(emptySearchPlot)
  && !emptySearchPlot.includes('value="herb-a"'), '筛选无匹配时空选择并禁用播种，不提交隐藏的旧草稿');
verify(plotHtml(emptySearchHtml, 'plot-2').includes('value="herb-a" selected'), '无匹配搜索不隐藏已种田的同种补种');

const lockedEmpty = structuredClone(catalogFixture);
lockedEmpty.cave.garden.plantOptions = lockedEmpty.cave.garden.plantOptions.filter(option => option.herbId !== 'herb-a');
const lockedEmptyHtml = renderCavePage(lockedEmpty);
verify(/data-cave-plant-grade="1"[^>]*aria-pressed="true"/.test(lockedEmptyHtml),
  '选中品阶库存用光时仍保留当前筛选标识，避免空目录却看不出筛选条件');
verify(plotHtml(lockedEmptyHtml, 'plot-2').includes('value="herb-a" selected')
  && plotHtml(lockedEmptyHtml, 'plot-2').includes('青灵草 · 1 阶 · 行囊 0')
  && /data-action="cave-garden-plant"[^>]*disabled/.test(plotHtml(lockedEmptyHtml, 'plot-2'))
  && plotHtml(lockedEmptyHtml, 'plot-2').includes('行囊中没有此药材'),
  '已种药材用光并离开可播种目录时，依据灵田快照保留固定条目并禁止补种');
verify(!plotHtml(lockedEmptyHtml, 'plot-2').includes('自然成长'), '库存用光后缺少基础天数时不伪造药材自然成长时间');

const invalidGradeHtml = renderCavePage(catalogFixture, { plantFilters: { grade: '<script>', search: '' } });
verify(/data-cave-plant-grade="1"[^>]*aria-pressed="true"/.test(invalidGradeHtml)
  && !invalidGradeHtml.includes('<script>'), '非法本地品阶回退一阶，不插入动态代码');
const escapedSearchHtml = renderCavePage(catalogFixture, { plantFilters: { grade: 'all', search: '\" onfocus=\"alert(1)' } });
verify(escapedSearchHtml.includes('value="&quot; onfocus=&quot;alert(1)"')
  && !escapedSearchHtml.includes('value="\" onfocus="'), '搜索输入值严格转义，不成为事件属性');
verify(JSON.stringify(catalogFixture) === catalogBefore, '筛选不改变材料目录、行囊或灵田状态');

console.log(`洞府纯展示契约：${checks} 项通过。`);
