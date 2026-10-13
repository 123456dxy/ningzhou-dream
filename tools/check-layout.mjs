// 渲染结构检查：把前端各页面的 HTML 结构跑一遍，确认布局类用得对。
//
// 为什么需要这个：之前我把 .chronicle-top 当容器、往 1fr+254px 的 grid 里塞
// 三个子元素，视觉上就乱了。这类错误肉眼要打开浏览器才看得出来，
// 用脚本检查结构能立刻发现。
//
// 用法：node tools/check-layout.mjs

import { readFileSync } from 'node:fs';

// 页面和地图/景观模块共同构成实际布局，都要检查。
const src = ['web/app.mjs', 'web/shell.mjs', 'web/map.mjs', 'web/location-art.mjs', 'web/portraits.mjs', 'web/friends.mjs', 'web/dungeons.mjs', 'web/cave-ui.mjs']
  .map(f => readFileSync(f, 'utf8')).join('\n');

let problems = 0;
const fail = (msg) => { console.log(`  ✗ ${msg}`); problems++; };
const ok = (msg) => console.log(`  ✓ ${msg}`);

console.log('=== 布局类使用检查 ===\n');

// 1) .chronicle-top 只能作为面板内的标题栏，不能当外层容器
//    特征：它应该紧跟在 <section class="chronicle-panel"> 之后
const topUses = [...src.matchAll(/class="chronicle-top"/g)].length;
console.log(`1) .chronicle-top 出现 ${topUses} 次`);
if (topUses === 0) {
  fail('一处都没用？原版每页的面板标题栏都用它');
} else {
  // 检查是否都用在我自己的 panel() 辅助函数里
  const inHelper = src.includes('`<div class="chronicle-top">');
  if (inHelper) ok('由 panel() 统一生成，位置正确（面板内标题栏）');
  else fail('没有走 panel()，可能又被当成外层容器用了');
}

// 2) .chronicle-layout 是 1fr + 254px，只能有两个直接子元素
console.log('\n2) .chronicle-layout 的子元素数量');
const layouts = [...src.matchAll(/layout\(([\s\S]*?)\)\s*\n/g)];
console.log(`   找到 ${layouts.length} 处 layout() 调用`);
// 检查有没有把多个元素拼进第一个参数
const concatPattern = /layout\(\s*([A-Za-z]+)\s*\+\s*([A-Za-z]+)\s*,/;
if (concatPattern.test(src)) {
  fail('layout() 第一个参数用了 + 拼接 → 会产生 3 个子元素，第三個被挤进 254px');
} else {
  ok('没有把多个面板拼进第一个参数');
}

// 3) 每页都应该以 heading() 开头（对应原版 .section-head）
console.log('\n3) 每页是否都有 heading()');
const views = ['viewChronicle', 'viewMap', 'viewArts', 'viewSociety',
               'viewQuests', 'viewMarket'];
// 这些页面均为顶层函数；按下一条顶层函数声明截断，既覆盖长页面，
// 也不会把下一页的 heading() 算到缺标题的页面上。
const declarations = [...src.matchAll(/^(?:export\s+)?(?:async\s+)?function\s+([\w$]+)\s*\(/gm)];
for (const v of views) {
  const index = declarations.findIndex(match => match[1] === v);
  if (index < 0) { fail(`${v} 找不到`); continue; }
  const start = declarations[index].index;
  const end = declarations[index + 1]?.index ?? src.length;
  const seg = src.slice(start, end);
  // 标题可直接返回、先存变量，或接在兽潮等置顶横幅之后拼入返回值。
  const has = /return\s+heading\(/.test(seg) || /[=+]\s*heading\(/.test(seg);
  if (has) ok(`${v} 有 heading()`);
  else fail(`${v} 没有 heading()，页面会缺少标题块`);
}

// 4) 检查用到的类是否都有 CSS 定义
console.log('\n4) 用到的类是否有 CSS 定义');
const css = ['web/styles.css', 'web/enhance.css', 'web/build.css']
  .map(f => readFileSync(f, 'utf8')).join('\n');

// 收集各页面模块里 class="xxx" 用到的类名
const used = new Set();
const clean = c => c.replace(/[^a-zA-Z0-9_-]/g, '');
for (const m of src.matchAll(/class="([^"$]+)"/g)) {
  for (const c of m[1].split(/\s+/)) if (c) used.add(clean(c));
}
// 动态拼接的类名（模板里）也扫一下
for (const m of src.matchAll(/class="([^"]*\$\{[^"]*)"/g)) {
  for (const c of m[1].split(/\s+/)) {
    if (c && !c.includes('${')) used.add(clean(c));
  }
}

const missing = [];
for (const c of [...used].sort()) {
  // 只看布局相关的类，避免把状态类（如 active）误报
  if (!/^(chronicle|section|panel|plan|dao|log|entry|timeline|filters|card|shop|craft|pill|person|people|roster|story|map|atlas|route|travel|destination|location|journey|poison|build|talent|alloc|rel|feed|action|bag|detail|empty|choice|intro|hint|year|arts|duel|combat|battle|character|sidebar|friend|npc)/.test(c)) continue;
  if (!css.includes('.' + c)) missing.push(c);
}
if (missing.length === 0) ok(`${used.size} 个类名都有定义`);
else fail(`这些类没有 CSS 定义：${missing.join(', ')}`);

// 5) 确认没有用原版不存在的类当布局容器
console.log('\n5) 检查可疑的布局类');
for (const c of ['chronicle-list', 'panel-note', 'dao-node-row']) {
  if (src.includes(`class="${c}"`)) {
    if (css.includes('.' + c)) ok(`${c} 有定义`);
    else fail(`${c} 被使用但没有定义`);
  }
}

console.log(`\n=== ${problems === 0 ? '结构检查通过' : `发现 ${problems} 个问题`} ===`);
process.exit(problems === 0 ? 0 : 1);
