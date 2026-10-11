// 打印 JS 版 LCG 的前几个值，用来和 C++ 版对照。
let rng = 20261004 >>> 0;
const f = () => { rng = (Math.imul(rng, 1664525) + 1013904223) >>> 0; return rng / 4294967296; };
const vals = [];
for (let i = 0; i < 5; i++) vals.push(f().toFixed(10));
console.log(vals.join(' '));
