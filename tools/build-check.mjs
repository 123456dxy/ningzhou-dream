// 打印 JS 版 buildCharacter 的结果，用于和 C++ 版对照。
import { buildCharacter } from '../../michangsheng-demo/sim.mjs';

const cases = [
  { traits: ['sword'], allocation: { apt: 6 }, budget: 10 },
  { traits: ['root', 'steady'], allocation: { apt: 0 }, budget: 10 },
  { traits: ['herb', 'merchant'], allocation: { wealth: 3 }, budget: 10 },
  { traits: ['luck', 'body'], allocation: { luck: 1 }, budget: 10 },
];

for (const c of cases) {
  const r = buildCharacter(c);
  console.log(JSON.stringify({
    traits: r.traits, apt: r.apt, luck: r.luck, insight: r.insight,
    sword: r.sword, alchemy: r.alchemy, body: r.body,
    lifeBonus: r.lifeBonus, stones: r.stones, herbs: r.herbs, spent: r.spent,
  }));
}
