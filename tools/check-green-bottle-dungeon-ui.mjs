// 小绿瓶专属秘境展示契约；模拟后端冻结报价，不代替实际结算和真实浏览器冒烟。
import assert from 'node:assert/strict';
import { renderCavePage } from '../web/cave-panel.mjs';
import { renderDungeonDirectory, renderDungeonEntrances, renderDungeonRun } from '../web/dungeons.mjs';

let checks = 0;
// 用途：记录独立展示断言；流程：本工具的权威快照夹具；返回：无，失败立即抛错。
function check(condition, message) { assert.ok(condition, message); checks++; }

const state = { player: { location: 'wuling', health: 100 }, cave: {
  access: { atHome: true }, garden: { plots: [], plantOptions: [] },
  greenBottle: { hasTalent: true, currentGrade: 5, currentDailyBonus: 420, currentUnitsPerDay: 480,
    nextGrade: 6, nextDailyBonus: 700, nextUnitsPerDay: 760, upgrade: { enabled: false, reason: '缺少灵材' },
    dungeons: [
      { grade: 4, name: '青玄灵境', materialName: '青玄灵髓', stage: 6, realmName: '金丹初期', rewardCount: 50, enabled: true },
      { grade: 5, name: '太乙灵境', materialName: '太乙青晶', stage: 9, realmName: '元婴初期', rewardCount: 100, enabled: true },
      { grade: 6, name: '造化灵境', materialName: '造化碧玉', stage: 12, realmName: '化神', rewardCount: 200, enabled: false, reason: '需达到化神 <未满足>' },
    ] },
} };
const before = JSON.stringify(state);
const cave = renderCavePage(state);
for (const portal of state.cave.greenBottle.dungeons) {
  check(cave.includes(`通关获得 ${portal.materialName} ×${portal.rewardCount}`), `${portal.grade}阶洞府入口显示后端完整升阶材料数量`);
  check(cave.includes(`固定难度：${portal.realmName}`), `${portal.grade}阶洞府入口显示固定境界难度`);
}
check(/data-action="green-bottle-dungeon"[^>]*data-material-grade="6"[^>]*disabled/.test(cave)
  && cave.includes('需达到化神 &lt;未满足&gt;'), '低境仍可查看已解锁入口，资格和原因遵从后端并转义');

for (const portal of state.cave.greenBottle.dungeons) {
  const site = { id: `bottle-${portal.grade}`, type: 'bottle', name: portal.name, location: 'wuling', locationName: '武陵城',
    stage: portal.stage, realmName: portal.realmName, revealed: true, available: true, entryEnabled: portal.enabled,
    targetName: portal.materialName, targetKind: 'bottleMaterial', targetGrade: portal.grade, targetRewardCount: portal.rewardCount };
  const directoryState = { ...state, dungeons: { sites: [site] } };
  check(renderDungeonDirectory(directoryState).includes(`${portal.materialName} ×${portal.rewardCount}`), `${portal.grade}阶秘境目录读取冻结目标数量`);
  check(renderDungeonEntrances(directoryState, 'wuling').includes(`${portal.materialName} ×${portal.rewardCount}`), `${portal.grade}阶地图入口读取冻结目标数量`);
  const run = { ...site, siteId: site.id, siteName: site.name, phase: 'route', nodes: [], deck: [], relics: [],
    realmName: '化神', difficultyStage: portal.stage, difficultyRealmName: portal.realmName };
  const running = renderDungeonRun({ ...state, dungeons: { run } });
  check(running.includes(`dungeon-eyebrow">${portal.realmName} · 灵材秘境`)
    && running.includes(`${portal.materialName} ×${portal.rewardCount}`), `${portal.grade}阶探索展示固定难度与冻结保底，不使用高境玩家难度`);
  const complete = renderDungeonRun({ ...state, dungeons: { run: { ...run, phase: 'complete', cleared: true } } });
  check(complete.includes(`已取得${portal.materialName} ×${portal.rewardCount}。`), `${portal.grade}阶通关结算沿用冻结材料数`);
}

const legacyRun = { siteId: 'legacy', siteName: '旧灵境', type: 'bottle', phase: 'route', nodes: [], deck: [], relics: [],
  realmName: '筑基初期', targetName: '青玄灵髓', targetKind: 'bottleMaterial' };
const legacy = renderDungeonRun({ ...state, dungeons: { run: legacyRun } });
check(legacy.includes('青玄灵髓 ×10') && legacy.includes('dungeon-eyebrow">筑基初期 · 灵材秘境'), '旧响应缺新增字段保留十份和原境界显示');
const escaped = renderDungeonRun({ ...state, dungeons: { run: { ...legacyRun, targetRewardCount: '<50>', difficultyRealmName: '金丹<固定>' } } });
check(escaped.includes('×&lt;50&gt;') && escaped.includes('金丹&lt;固定&gt;') && !escaped.includes('×<50>'), '冻结数量和难度名称按纯文本转义');
check(!cave.includes('bottleMaterial'), '专属灵材不暴露内部物品类型');
check(JSON.stringify(state) === before, '所有展示均不修改调用方的报价或状态');
console.log(`小绿瓶秘境展示契约：${checks}/${checks} 通过。`);
