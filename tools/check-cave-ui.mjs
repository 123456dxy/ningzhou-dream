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
verify(html.includes('data-cave-plant-count') && html.includes('max="79"'), '播种数量控件采用后端剩余容量');
verify(html.includes('data-action="cave-garden-plant"') && html.includes('data-plot-id="plot-1"'), '播种动作标识与点位完整');
verify(html.includes('data-action="cave-garden-deposit"') && html.includes('data-action="cave-garden-withdraw"'), '储备操作有明确存取动作');
verify(!html.includes('cave-garden-accelerate') && !html.includes('加速一次'), '自动加速不再提供单次收费按钮');
verify(html.includes('45 灵石 / 天') && html.includes('80 灵石'), '单日费用与本格储备读取权威数据');
verify(html.includes('9 天') && html.includes('30 天') && html.includes('10 天'), '成熟时间展示后端预估，不重算成长');
verify(html.includes('成熟可收 15 份') && html.includes('5 份') && html.includes('15 份'), '成熟收获量与独立种植批次可见');
verify(html.includes('data-action="cave-garden-clear"'), '放弃种苗有独立动作供主入口确认');
verify(html.includes('data-action="cave-train"') && html.includes('data-id="arena"') && html.includes('10 灵石') && html.includes('2 天'), '训练场展示真实演武报价');
verify(JSON.stringify(fixture) === before, '纯展示不修改调用方状态或批次进度');

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

console.log(`洞府纯展示契约：${checks} 项通过。`);
