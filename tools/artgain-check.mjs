import { ensureArts, artGain, artMods } from '../../michangsheng-demo/arts.mjs';

// artGain 的悟道点累积
const p = { stones: 100, herbs: 10, sect: null, alchemy: 10, arts: null };
const s = { player: p };
ensureArts(s);
artGain(p, '剑', 35);
console.log('after 35:', p.arts.exp['剑'], 'points', p.arts.points);
artGain(p, '剑', 80);
console.log('after 80:', p.arts.exp['剑'], 'points', p.arts.points);

// 主修/辅修的 cult 计算：单独验证
const q = { stones: 100, herbs: 10, sect: null, alchemy: 10, arts: null };
ensureArts({ player: q });
q.arts.nodes = ['8-1','8-2','8-3','6-1','6-3','7-1'];
q.arts.manuals = { tuna: 5, bull: 3 };
q.arts.main = 'bull';
q.arts.aux = ['tuna'];
console.log('cult:', artMods(q).cult.toFixed(6), 'hp:', artMods(q).hp.toFixed(6));
