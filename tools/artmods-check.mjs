// 打印 JS 版 artMods 的结果，用于和 C++ 版对照。
import { ensureArts, artMods, artGain, artHas } from '../../michangsheng-demo/arts.mjs';

// 造一个最小的 s / p，够 ensureArts 与 artMods 使用
const p = { stones: 100, herbs: 10, sect: null, alchemy: 10, arts: null };
const s = { player: p };
ensureArts(s);

const show = (label) => {
  const m = artMods(p);
  const pick = ['cult','attack','sword','heal','hp','shield','mana','brew','forge',
                'reduction','charge','water','poison','soul','drain','fire','steal',
                'roundShield','openingShield','openingMana','openingQi','quietHeal','combo'];
  const o = {};
  for (const k of pick) o[k] = Number((m[k] ?? 0).toFixed(6));
  console.log(label, JSON.stringify(o));
};

show('初始');

// 点几个节点：气道三层、体道一层、剑道一层
p.arts.nodes = ['8-1', '8-2', '8-3', '6-1', '6-3', '7-1'];
show('节点');

// 修习吐纳功到 5 层，加蛮牛护体功作主修
p.arts.manuals = { tuna: 5, bull: 3 };
p.arts.main = 'bull';
p.arts.aux = ['tuna'];
show('功法');

// 带一件法器
p.arts.gear = [{ id: 'g1', name: '试剑', slot: 'weapon', element: '金', polarity: '天阳', grade: 2, bonus: 0.15, inscribed: true, price: 100 }];
p.arts.equipped = { weapon: 'g1' };
show('法器');

// 丹毒削弱修炼
p.arts.poison = 60;
show('丹毒60');
