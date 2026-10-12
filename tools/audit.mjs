// 宁州一梦 · C++ 版 — 死代码与接线审计
//
// 这个脚本的存在理由：本项目已经反复出现同一类问题 ——
//   · 函数写好了却没人调用（breakthroughBonus / learnDays）
//   · 变量算好了却没插进返回值（hand / sectPanel）
//   · 后端下发了字段前端不读，或前端读了后端不发
//   · 局部变量定义后从未使用（sel）
// 前几轮都是玩家玩出来才发现的。这里把这几类做成自动检查。
//
// 用法：node tools/audit.mjs [--json]
// 退出码：0 = 没有发现，1 = 有发现（可用于 CI）

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const findings = [];
const add = (kind, where, detail) => findings.push({ kind, where, detail });

function walk(dir, ext, out = []) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    const st = statSync(p);
    if (st.isDirectory()) {
      if (name === 'build' || name === 'node_modules' || name === '.git') continue;
      walk(p, ext, out);
    } else if (ext.some(e => name.endsWith(e))) {
      out.push(p);
    }
  }
  return out;
}

const read = (p) => readFileSync(p, 'utf8');
const rel = (p) => relative(ROOT, p).replace(/\\/g, '/');

// API serializers are separate translation units. Scan the actual module tree
// instead of depending on the former monolithic api.cpp or local variable names.
const interfaceFiles = walk(join(ROOT, 'src/core/interface'), ['.cpp']);
const interfaceText = interfaceFiles.map(read).join('\n') + '\n' + read(join(ROOT, 'src/web_main.cpp'));
const playerViewText = read(join(ROOT, 'src/core/interface/player_views.cpp'));
const apiRouteText = read(join(ROOT, 'src/core/api.cpp')) + '\n' +
  read(join(ROOT, 'src/core/interface/routes.cpp')) + '\n' +
  read(join(ROOT, 'src/web_main.cpp')) + '\n' +
  read(join(ROOT, 'src/core/tianji.cpp'));

// ============================================================
// 1. C++：定义了但从未被调用的函数
// ============================================================
function auditCppDeadFunctions() {
  const cppFiles = walk(join(ROOT, 'src'), ['.cpp']);
  const allCpp = cppFiles.map(read);
  const headerFiles = walk(join(ROOT, 'src'), ['.h']);
  const testFiles = walk(join(ROOT, 'tests'), ['.cpp', '.h']);
  const testText = testFiles.map(read).join('\n');
  const cliText = walk(join(ROOT, 'src'), ['.cpp'])
    .filter(p => rel(p).includes('cli'))
    .map(read)
    .join('\n');

  // 已知的「故意不被调用」清单：入口、测试钩子、为对称性提供的重载
  const ALLOW = new Set([
    'main', 'handle', 'serve', 'game', 'started',
  ]);

  for (let i = 0; i < cppFiles.length; i++) {
    const file = cppFiles[i];
    const text = allCpp[i];
    // 匹配顶层函数定义：行首（无缩进）+ 返回类型 + 名字 + (
    const re = /^(?!\s)(?:static\s+)?(?:inline\s+)?[A-Za-z_][\w:<>,&*\s]*?\b(\w+)\s*\([^;]*?\)\s*\{/gm;
    for (const m of text.matchAll(re)) {
      const name = m[1];
      if (ALLOW.has(name)) continue;
      if (name === 'operator') continue;
      // 在其它文件里出现过吗（排除本文件）
      let used = false;
      for (let j = 0; j < allCpp.length && !used; j++) {
        if (j === i) continue;
        if (new RegExp(`\\b${name}\\s*[(<]`).test(allCpp[j])) used = true;
      }
      // 头文件里声明了（说明是对外接口）也算「有出口」
      if (!used) {
        for (const h of headerFiles) {
          if (new RegExp(`\\b${name}\\s*[(<]`).test(read(h))) { used = true; break; }
        }
      }
      // 测试或 CLI 里用了也算
      if (!used && new RegExp(`\\b${name}\\s*[(<]`).test(testText + cliText)) used = true;
      // 本文件内部调用也算。定义可能在调用点之后（例如 artsToJson 先调用
      // 后面才定义的 battleToJson），所以要把整篇都看一遍。
      if (!used) {
        const whole = text.slice(0, m.index) + text.slice(m.index + m[0].length);
        if (new RegExp(`\\b${name}\\s*[(<]`).test(whole)) used = true;
      }
      if (!used) {
        const line = text.slice(0, m.index).split('\n').length;
        add('C++ 死函数', `${rel(file)}:${line}`, `${name}() 从未被调用`);
      }
    }
  }
}

// ============================================================
// 2. JS：定义了但从未使用的局部 const
// ============================================================
function auditJsDeadLocals() {
  const jsFiles = walk(join(ROOT, 'web'), ['.mjs']);
  for (const file of jsFiles) {
    const src = read(file);
    for (const m of src.matchAll(/^(\s+)const (\w+) =/gm)) {
      const name = m[2];
      if (name === 'TABS') continue;   // 模块级常量，导出用
      const after = src.slice(m.index + m[0].length);
      // 展开运算符 ...name 也算使用，但它前面是点号，
      // 会被「排除属性访问」的 lookbehind 挡掉，所以单独放行。
      const re = new RegExp(`(\\.\\.\\.\\s*${name}\\b)|(?<![.\\w$])${name}\\b`);
      if (!re.test(after)) {
        const line = src.slice(0, m.index).split('\n').length;
        add('JS 死变量', `${rel(file)}:${line}`, `${name} 定义后从未使用`);
      }
    }
  }
}

// ============================================================
// 3. 后端下发的 state 字段 vs 前端读取的字段
// ============================================================
function auditStateContract() {
  const body = interfaceText;

  // 顶层 state 字段：buildState 里任何 s.set("xxx")
  const sent = new Set();
  for (const m of body.matchAll(/\bstate\.set\("(\w+)"/g)) sent.add(m[1]);

  // 玩家子对象字段：定位到 // ---- 玩家 ---- 那一块
  const playerBlock = playerViewText;
  const playerSent = new Set();
  for (const m of playerBlock.matchAll(/\bentry\.set\("(\w+)"/g)) playerSent.add(m[1]);

  // 前端读的字段
  const jsFiles = walk(join(ROOT, 'web'), ['.mjs']);
  const need = new Set();
  const needPlayer = new Set();
  for (const f of jsFiles) {
    const src = read(f);
    for (const m of src.matchAll(/\bS\.([A-Za-z_$][\w$]*)/g)) need.add(m[1]);
    for (const m of src.matchAll(/\bS\.player\.([A-Za-z_$][\w$]*)/g)) needPlayer.add(m[1]);
    for (const m of src.matchAll(/\bstate\.player\.([A-Za-z_$][\w$]*)/g)) needPlayer.add(m[1]);
    for (const m of src.matchAll(/\bS\?\.([A-Za-z_$][\w$]*)/g)) need.add(m[1]);
  }

  // 条件出现的字段：缺了正常
  const OPTIONAL = new Set([
    'battle', 'job', 'pending', 'ending', 'notorietyNote',
    'report', 'text', 'actions', 'slots', 'innate', 'state', 'error',
  ]);
  for (const n of need) {
    if (sent.has(n) || OPTIONAL.has(n)) continue;
    add('契约缺字段', 'api.cpp buildState', `前端读 S.${n}，后端顶层未下发`);
  }
  for (const n of needPlayer) {
    if (playerSent.has(n)) continue;
    add('契约缺字段', 'api.cpp buildState/player', `前端读 S.player.${n}，后端未下发`);
  }
}

// ============================================================
// 4. 后端下发了但前端从不读的字段
// ============================================================
function auditUnusedStateFields() {
  const body = interfaceText;
  const jsFiles = walk(join(ROOT, 'web'), ['.mjs']);
  const allJs = jsFiles.map(read).join('\n');

  const sent = new Set();
  for (const m of body.matchAll(/\bstate\.set\("(\w+)"/g)) sent.add(m[1]);
  for (const m of playerViewText.matchAll(/\bentry\.set\("(\w+)"/g)) sent.add(m[1]);

  for (const name of sent) {
    if (new RegExp(`\\b${name}\\b`).test(allJs)) continue;
    add('未读字段', 'api.cpp buildState', `后端下发 ${name}，前端从未读取`);
  }
}

// ============================================================
// 5. 前端发起的 API 路径 vs 后端注册的路由
// ============================================================
function auditRoutes() {
  const api = apiRouteText;
  const registered = new Set();
  for (const m of api.matchAll(/RouteId::\w+,\s*"(\/api\/[^"]+)"/g)) registered.add(m[1]);
  for (const m of api.matchAll(/path == "(\/api\/[^"]+)"/g)) registered.add(m[1]);
  // 前缀匹配的写法：path.rfind("/api/xxx/", 0) == 0
  for (const m of api.matchAll(/rfind\("(\/api\/[^"]+)"/g)) registered.add(m[1]);

  const jsFiles = walk(join(ROOT, 'web'), ['.mjs']);
  for (const f of jsFiles) {
    const src = read(f);
    for (const m of src.matchAll(/call\(\s*'([^']+)'/g)) {
      const p = '/api/' + m[1];
      // 允许 /api/gear/unequip 这类前缀
      let ok = registered.has(p);
      if (!ok) {
        for (const r of registered) {
          if (r.endsWith('/') && p.startsWith(r)) { ok = true; break; }
        }
      }
      if (!ok) {
        const line = src.slice(0, m.index).split('\n').length;
        add('路由不存在', `${rel(f)}:${line}`, `前端调用 ${p}，后端未注册`);
      }
    }
    for (const m of src.matchAll(/fetch\('(\/api\/[^']+)'/g)) {
      const p = m[1].split(/[?#]/, 1)[0];
      if (!registered.has(p)) {
        const line = src.slice(0, m.index).split('\n').length;
        add('路由不存在', `${rel(f)}:${line}`, `前端 fetch ${p}，后端未注册`);
      }
    }
  }
}

// ============================================================
// 6. JS：调用了未定义的函数
// ============================================================
function auditUndefinedCalls() {
  const jsFiles = walk(join(ROOT, 'web'), ['.mjs']);
  for (const file of jsFiles) {
    const src = read(file);
    const known = new Set([
      'console', 'Math', 'JSON', 'Object', 'Array', 'String', 'Number',
      'Boolean', 'Date', 'Set', 'Map', 'Promise', 'Error', 'parseInt',
      'parseFloat', 'isNaN', 'fetch', 'document', 'window', 'URL', 'Blob',
      'setTimeout', 'clearTimeout', 'setInterval', 'clearInterval',
      'encodeURIComponent', 'decodeURIComponent', 'RegExp', 'Symbol',
      'WeakMap', 'Infinity', 'NaN', 'undefined', 'null', 'true', 'false',
      'if', 'for', 'while', 'switch', 'catch', 'return', 'typeof',
      'function', 'await', 'new', 'delete', 'of', 'in', 'do', 'else',
      'try', 'finally', 'throw', 'case', 'break', 'continue', 'class',
      'extends', 'prompt', 'alert', 'confirm', 'requestAnimationFrame',
      'async', 'yield', 'super', 'this', 'void', 'instanceof',
    ]);
    for (const m of src.matchAll(/(?:^|\n)\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_$][\w$]*)/g)) known.add(m[1]);
    for (const m of src.matchAll(/(?:^|\n)\s*(?:export\s+)?(?:let|const|var)\s+([A-Za-z_$][\w$]*)/g)) known.add(m[1]);
    for (const m of src.matchAll(/import\s*\{([^}]*)\}/g)) {
      for (const p of m[1].split(',')) {
        const n = p.trim().split(/\s+as\s+/).pop().trim();
        if (n) known.add(n);
      }
    }
    for (const m of src.matchAll(/function\s*[\w$]*\s*\(([^)]*)\)/g)) {
      for (const p of m[1].split(',')) {
        const n = p.trim().replace(/[{}[\]]/g, '').split(':').pop().split('=')[0].trim();
        if (/^[A-Za-z_$][\w$]*$/.test(n)) known.add(n);
      }
    }
    for (const m of src.matchAll(/\(([^)]*)\)\s*=>/g)) {
      for (const p of m[1].split(',')) {
        const n = p.trim().replace(/[{}[\]]/g, '').split(':').pop().split('=')[0].trim();
        if (/^[A-Za-z_$][\w$]*$/.test(n)) known.add(n);
      }
    }
    for (const m of src.matchAll(/for\s*\(\s*(?:const|let|var)\s+([^)]*?)\s+of\s/g)) {
      for (const p of m[1].replace(/[{}[\]]/g, '').split(',')) {
        if (/^[A-Za-z_$][\w$]*$/.test(p.trim())) known.add(p.trim());
      }
    }
    for (const m of src.matchAll(/catch\s*\(\s*([A-Za-z_$][\w$]*)/g)) known.add(m[1]);

    for (const m of src.matchAll(/(?<![.\w$])([A-Za-z_$][\w$]*)\s*\(/g)) {
      const n = m[1];
      if (known.has(n)) continue;
      // HTML 模板内 style="...calc(...)" 是 CSS 函数；不要把它当成 JS 调用。
      // 只在当前词仍处于 style 属性中时跳过，真正写在 JS 的 calc() 仍会报告。
      if (n === 'calc') {
        const attributeStart = src.lastIndexOf('style="', m.index);
        if (attributeStart >= 0 && !src.slice(attributeStart + 7, m.index).includes('"')) continue;
      }
      const line = src.slice(0, m.index).split('\n').length;
      // IIFE 之类的自引用跳过
      if (src.slice(m.index, m.index + 40).includes('function')) continue;
      // 函数表达式自调用：名字前面是 async / function
      const pre = src.slice(Math.max(0, m.index - 12), m.index);
      if (/\b(async|function)\s+$/.test(pre)) continue;
      // 对象字面量的方法：名字前面是 { 或 ,，或者整行只有缩进 + 名字
      const lineStart = src.lastIndexOf('\n', m.index - 1) + 1;
      const before = src.slice(lineStart, m.index);
      if (/^\s*(async\s+)?$/.test(before)) continue;
      add('未定义调用', `${rel(file)}:${line}`, `${n}() 未定义`);
    }
  }
}

auditCppDeadFunctions();
auditJsDeadLocals();
auditStateContract();
auditUnusedStateFields();
auditRoutes();
auditUndefinedCalls();

// ---- 输出 ----
const byKind = new Map();
for (const f of findings) {
  if (!byKind.has(f.kind)) byKind.set(f.kind, []);
  byKind.get(f.kind).push(f);
}

if (process.argv.includes('--json')) {
  console.log(JSON.stringify(findings, null, 2));
} else {
  if (!findings.length) {
    console.log('=== 审计通过：没有发现死代码或接线问题 ===');
  } else {
    for (const [kind, list] of byKind) {
      console.log(`\n=== ${kind}（${list.length}）===`);
      for (const f of list) console.log(`  ${f.where}\n      ${f.detail}`);
    }
    console.log(`\n合计 ${findings.length} 项`);
  }
}
process.exit(findings.length ? 1 : 0);
