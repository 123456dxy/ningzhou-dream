// 打印 JS 版俸禄账簿与身份的结果，供 C++ 版对照。
import { ensureArts } from '../../michangsheng-demo/arts.mjs';
import { ensureSociety, socStipend, socRank, socAccrue, socInitPayroll } from '../../michangsheng-demo/society.mjs';

function makeGame(sect, stage, days) {
  const p = {
    name: '测试', gender: '女', age: 20, lifespan: 100, stage, xp: 0,
    apt: 60, luck: 45, insight: 20, sword: 20, alchemy: 10, body: 2,
    health: 100, stones: 500, herbs: 20, pills: 0, cores: 0,
    location: 'wuling', relations: {}, renown: 0, highestStage: stage,
    achievements: [], sect, traits: ['sword'], build: { budget: 10 }, arts: null,
  };
  const s = {
    version: 5, rng: 1, seed: 1, year: Math.floor(days / 360), eventSeq: 0, npcSeq: 0,
    ended: false, pending: null, plan: 'cultivate', flags: {}, logs: [], npcs: [],
    clock: { days, ageBase: 16, carry: {}, job: null },
    world: { prosperity: 72, market: 1, mineOwner: 'jinhong', herbs: 3200, coreStock: 120, pillStock: 90, weather: '春和景明', sects: {} },
    player: p,
  };
  ensureArts(s);
  ensureSociety(s);
  return s;
}

console.log('=== 身份与俸禄 ===');
for (const stage of [0, 2, 3, 5, 6, 9, 12]) {
  const s = makeGame('jinhong', stage, 0);
  console.log(`stage=${stage} rank=${socRank(s)} stipend=${socStipend(s)}`);
}

console.log('=== 俸禄记账 ===');
{
  // 第 0 个月拜入金虹剑派（炼气初期），推进到第 5 个月
  const s = makeGame('jinhong', 0, 0);
  socInitPayroll(s);
  console.log('init balance=' + s.bonds.payroll.balance + ' credited=' + s.bonds.payroll.credited);
  s.clock.days = 150;   // 第 5 个月
  socAccrue(s);
  console.log('after 5 months balance=' + s.bonds.payroll.balance + ' credited=' + s.bonds.payroll.credited + ' lastMonth=' + s.bonds.payroll.lastMonth);
}
{
  // 散修不应记俸禄
  const s = makeGame(null, 0, 0);
  socInitPayroll(s);
  s.clock.days = 150;
  socAccrue(s);
  console.log('散修 balance=' + s.bonds.payroll.balance);
}
{
  // 恶名 40 以上暂停俸禄
  const s = makeGame('jinhong', 0, 0);
  socInitPayroll(s);
  s.bonds.notoriety = 50;
  s.clock.days = 150;
  socAccrue(s);
  console.log('恶名50 balance=' + s.bonds.payroll.balance + ' lastMonth=' + s.bonds.payroll.lastMonth);
}
{
  // 筑基初期（stage 3）俸禄应为 16/月
  const s = makeGame('jinhong', 3, 0);
  socInitPayroll(s);
  s.clock.days = 90;   // 第 3 个月
  socAccrue(s);
  console.log('筑基初期 3月 balance=' + s.bonds.payroll.balance);
}
