import fs from 'node:fs';
const raw = fs.readFileSync('build/full-test.log', 'utf8');
for (const match of raw.matchAll(/\[失败\] ([^\n]+) \(得到「(\{[\s\S]*?)」，期望「(\{[\s\S]*?)」\)/g)) {
  console.log(match[1]);
  const a = JSON.parse(match[2]), b = JSON.parse(match[3]);
  let count = 0;
  function diff(x,y,path='') {
    if (JSON.stringify(x) === JSON.stringify(y)) return;
    if (x && y && typeof x === 'object' && typeof y === 'object') { for (const k of new Set([...Object.keys(x),...Object.keys(y)])) diff(x[k],y[k],path+'.'+k); }
    else if (++count < 30) console.log(path,JSON.stringify(x),'!=',JSON.stringify(y));
  }
  diff(a,b); console.log('differences',count);
}
