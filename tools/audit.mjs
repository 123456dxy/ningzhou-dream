// 宁州一梦 · C++ 版 — 死代码与接线审计
//
// 这个脚本的存在理由：本项目已经反复出现同一类问题 ——
//   · 函数写好了却没人调用（breakthroughBonus / learnDays）
//   · 变量算好了却没插进返回值（hand / sectPanel）
//   · 后端下发了字段前端不读，或前端读了后端不发
//   · 局部变量定义后从未使用（sel）
// 前几轮都是玩家玩出来才发现的。这里把这几类做成自动检查。
//
// 用法：node tools/audit.mjs [--json] [--self-test]
// 退出码：0 = 无错误，1 = 有错误；API 未读字段只输出使用提示，不阻断 CI。

import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join, relative, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';
import assert from 'node:assert/strict';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
const findings = [];
const add = (kind, where, detail, severity = 'error') => findings.push({ kind, where, detail, severity });

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
// Only the entry serialized into state.player belongs to the player contract.
const playerEntryText = playerViewText.slice(0, playerViewText.indexOf('state.set("player"'));
const apiRouteText = read(join(ROOT, 'src/core/api.cpp')) + '\n' +
  read(join(ROOT, 'src/core/interface/routes.cpp')) + '\n' +
  read(join(ROOT, 'src/web_main.cpp')) + '\n' +
  read(join(ROOT, 'src/core/tianji.cpp'));

// ============================================================
// 1. C++：定义了但从未被调用的函数
// ============================================================
function stripCppTrivia(source) {
  return source.replace(/R"([^\s()\\]{0,16})\([\s\S]*?\)\1"|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g,
    text => text.replace(/[^\r\n]/g, ' '));
}
function auditCppDeadFunctions() {
  const cppFiles = walk(join(ROOT, 'src'), ['.cpp']);
  const allCpp = cppFiles.map(file => stripCppTrivia(read(file)));
  const headerFiles = walk(join(ROOT, 'src'), ['.h']);
  const testFiles = walk(join(ROOT, 'tests'), ['.cpp', '.h']);
  const testText = testFiles.map(file => stripCppTrivia(read(file))).join('\n');
  const cliText = walk(join(ROOT, 'src'), ['.cpp'])
    .filter(p => rel(p).includes('cli'))
    .map(read)
    .join('\n');

  // 已知的「故意不被调用」清单：入口、测试钩子、为对称性提供的重载
  const ALLOW = new Set([
    'main', 'wmain', 'handle', 'serve', 'game', 'started',
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
      // 函数也可作为signal/thread等回调传入：识别代码中的标识符引用，不要求紧跟括号。
      let used = false;
      for (let j = 0; j < allCpp.length && !used; j++) {
        if (j === i) continue;
        if (new RegExp(`\\b${name}\\b`).test(allCpp[j])) used = true;
      }
      // 头文件里声明了（说明是对外接口）也算「有出口」
      if (!used) {
        for (const h of headerFiles) {
          if (new RegExp(`\\b${name}\\s*[(<]`).test(stripCppTrivia(read(h)))) { used = true; break; }
        }
      }
      // 测试或 CLI 里用了也算
      if (!used && new RegExp(`\\b${name}\\b`).test(testText + stripCppTrivia(cliText))) used = true;
      // 本文件内部调用也算。定义可能在调用点之后（例如 artsToJson 先调用
      // 后面才定义的 battleToJson），所以要把整篇都看一遍。
      if (!used) {
        const whole = text.slice(0, m.index) + text.slice(m.index + m[0].length);
        if (new RegExp(`\\b${name}\\b`).test(whole)) used = true;
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
// Parse source without executing it. Prefer an installed Acorn; official Node
// also bundles it for its REPL. The bundled path is private, so absence fails
// explicitly instead of silently falling back to a weaker regex audit.
function loadJsParser() {
  const require = createRequire(import.meta.url);
  try { return require('acorn'); } catch (error) {
    if (error.code !== 'MODULE_NOT_FOUND') throw error;
  }
  const source = process.binding('natives')['internal/deps/acorn/acorn/dist/acorn'];
  if (!source) throw new Error('JavaScript 审计需要 Acorn；请安装 acorn 或使用包含该解析器的官方 Node.js。');
  const module = { exports: {} };
  new Function('exports', 'module', source)(module.exports, module);
  return module.exports;
}
const jsParser = loadJsParser();
const JS_GLOBALS = new Set(`
  Array ArrayBuffer BigInt BigInt64Array BigUint64Array Boolean DataView Date
  Error AggregateError EvalError RangeError ReferenceError SyntaxError TypeError URIError
  Float32Array Float64Array Function Int8Array Int16Array Int32Array Map Number Object
  Promise Proxy RegExp Set SharedArrayBuffer String Symbol Uint8Array Uint8ClampedArray
  Uint16Array Uint32Array WeakMap WeakRef WeakSet FinalizationRegistry
  decodeURI decodeURIComponent encodeURI encodeURIComponent escape unescape eval
  isFinite isNaN parseFloat parseInt structuredClone
  AbortController AbortSignal Blob BroadcastChannel CustomEvent DOMException Event
  File FileReader FormData Headers Image IntersectionObserver MessageChannel MutationObserver
  Option PerformanceObserver Request ResizeObserver Response URL URLSearchParams WebSocket Worker
  alert atob btoa cancelAnimationFrame cancelIdleCallback clearInterval clearTimeout confirm
  fetch getComputedStyle matchMedia prompt queueMicrotask requestAnimationFrame requestIdleCallback
  setInterval setTimeout
`.trim().split(/\s+/));

// A per-module lexical binding graph: declarations are collected before references
// resolve, so hoisted functions work while parameters/locals cannot leak into other
// functions or files. This checks bare calls/constructors, not object method names,
// dynamic eval, whether a bound value is callable, or temporal-dead-zone execution.
function analyzeJavaScript(source) {
  const ast = jsParser.parse(source, { ecmaVersion: 'latest', sourceType: 'module', locations: true });
  const bindings = [], references = [];
  const scope = (parent = null, type = 'block') => ({ parent, type, names: new Map() });
  const declare = (id, where, kind, exported = false) => {
    if (kind === 'var') while (where.parent && !['function', 'module'].includes(where.type)) where = where.parent;
    let binding = where.names.get(id.name);
    if (!binding) {
      binding = { name: id.name, line: id.loc.start.line, kind, used: exported, scope: where };
      where.names.set(id.name, binding); bindings.push(binding);
    } else if (exported) binding.used = true;
  };
  function pattern(node, where, kind, exported = false) {
    if (!node) return;
    if (node.type === 'Identifier') declare(node, where, kind, exported);
    else if (node.type === 'RestElement') pattern(node.argument, where, kind, exported);
    else if (node.type === 'AssignmentPattern') { pattern(node.left, where, kind, exported); visit(node.right, where); }
    else if (node.type === 'ArrayPattern') node.elements.forEach(item => pattern(item, where, kind, exported));
    else if (node.type === 'ObjectPattern') {
      // { discarded, ...rest } intentionally excludes keys from a copied object.
      const omitsKeys = node.properties.some(item => item.type === 'RestElement');
      node.properties.forEach(item => {
        if (item.type === 'RestElement') pattern(item.argument, where, kind, exported);
        else { if (item.computed) visit(item.key, where); pattern(item.value, where, kind, exported || omitsKeys); }
      });
    }
  }
  function visit(node, where, exported = false) {
    if (!node) return;
    switch (node.type) {
      case 'Identifier': references.push({ name: node.name, line: node.loc.start.line, scope: where }); return;
      case 'PrivateIdentifier': case 'Literal': case 'TemplateElement': case 'EmptyStatement': return;
      case 'Program': node.body.forEach(child => visit(child, where)); return;
      case 'BlockStatement': case 'StaticBlock': {
        const childScope = scope(where); node.body.forEach(child => visit(child, childScope)); return;
      }
      case 'VariableDeclaration':
        node.declarations.forEach(item => { pattern(item.id, where, node.kind, exported); visit(item.init, where); }); return;
      case 'FunctionDeclaration': case 'FunctionExpression': case 'ArrowFunctionExpression': {
        if (node.type === 'FunctionDeclaration' && node.id) declare(node.id, where, 'function', exported);
        const childScope = scope(where, 'function');
        if (node.type === 'FunctionExpression' && node.id) declare(node.id, childScope, 'function');
        node.params.forEach(param => pattern(param, childScope, 'parameter'));
        visit(node.body, childScope); return;
      }
      case 'ClassDeclaration': case 'ClassExpression': {
        if (node.type === 'ClassDeclaration' && node.id) declare(node.id, where, 'class', exported);
        const childScope = scope(where, 'class');
        if (node.id) declare(node.id, childScope, 'class');
        visit(node.superClass, where); visit(node.body, childScope); return;
      }
      case 'Property': case 'MethodDefinition': case 'PropertyDefinition':
        if (node.computed) visit(node.key, where); visit(node.value, where); return;
      case 'MemberExpression':
        visit(node.object, where); if (node.computed) visit(node.property, where); return;
      case 'CallExpression': case 'NewExpression':
        if (node.callee.type === 'Identifier') references.push({ name: node.callee.name, line: node.callee.loc.start.line, scope: where, call: true });
        else visit(node.callee, where);
        node.arguments.forEach(argument => visit(argument, where)); return;
      case 'TaggedTemplateExpression':
        if (node.tag.type === 'Identifier') references.push({ name: node.tag.name, line: node.tag.loc.start.line, scope: where, call: true });
        else visit(node.tag, where);
        visit(node.quasi, where); return;
      case 'ImportDeclaration': node.specifiers.forEach(item => declare(item.local, where, 'import')); return;
      case 'ExportNamedDeclaration': case 'ExportDefaultDeclaration':
        visit(node.declaration, where, true);
        if (!node.source) node.specifiers?.forEach(item => visit(item.local, where)); return;
      case 'ExportAllDeclaration': return;
      case 'CatchClause': {
        const childScope = scope(where); pattern(node.param, childScope, 'catch'); visit(node.body, childScope); return;
      }
      case 'ForStatement': case 'ForInStatement': case 'ForOfStatement': {
        const childScope = scope(where);
        for (const key of ['init', 'test', 'update', 'left', 'right', 'body']) visit(node[key], childScope);
        return;
      }
      case 'SwitchStatement': {
        visit(node.discriminant, where); const childScope = scope(where);
        node.cases.forEach(child => visit(child, childScope)); return;
      }
      case 'LabeledStatement': visit(node.body, where); return;
      case 'BreakStatement': case 'ContinueStatement': return;
    }
    for (const value of Object.values(node)) {
      if (Array.isArray(value)) value.forEach(child => { if (child?.type) visit(child, where); });
      else if (value?.type) visit(value, where);
    }
  }
  visit(ast, scope(null, 'module'));
  const undefinedCalls = [];
  for (const reference of references) {
    let current = reference.scope, binding;
    while (current && !(binding = current.names.get(reference.name))) current = current.parent;
    if (binding) binding.used = true;
    else if (reference.call && !JS_GLOBALS.has(reference.name)) undefinedCalls.push(reference);
  }
  return { unused: bindings.filter(binding => binding.kind === 'const' && !binding.used), undefinedCalls };
}
const jsAnalysis = new Map();
function analyzeFile(file) {
  if (!jsAnalysis.has(file)) jsAnalysis.set(file, analyzeJavaScript(read(file)));
  return jsAnalysis.get(file);
}
function auditJsDeadLocals() {
  for (const file of walk(join(ROOT, 'web'), ['.mjs']))
    for (const binding of analyzeFile(file).unused)
      add('JS 死变量', `${rel(file)}:${binding.line}`, `${binding.name} 定义后从未使用`);
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
  const playerBlock = playerEntryText;
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
  for (const m of playerEntryText.matchAll(/\bentry\.set\("(\w+)"/g)) sent.add(m[1]);

  for (const name of sent) {
    if (new RegExp(`\\b${name}\\b`).test(allJs)) continue;
    add('API 使用提示', 'api.cpp buildState', `后端下发 ${name}，当前前端未读取；可能供兼容接口或其他客户端使用`, 'information');
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
  for (const file of walk(join(ROOT, 'web'), ['.mjs']))
    for (const call of analyzeFile(file).undefinedCalls)
      add('未定义调用', `${rel(file)}:${call.line}`, `${call.name}() 未定义`);
}

// Run before every repository audit. No source fixture is evaluated as JavaScript.
function verifyJsAnalysis() {
  const valid = analyzeJavaScript(`
    import defaultFn, { original as alias } from './dependency.mjs';
    import * as namespace from './other.mjs';
    const invoke = fn => fn();
    const { first, nested: { callback: second }, ...rest } = source;
    function use({ fetcher = () => null, onBusy }, [done], ...callbacks) {
      fetcher(); onBusy(); done(); callbacks.forEach(fn => fn());
      const action = () => {}, attrs = () => {}; action(); attrs();
      return { method(fn) { fn(); }, [first()]: second(), ...rest };
    }
    class Custom extends Error { method(fn) { return fn(); } }
    new Custom(); new FormData(); new AbortController(); new ResizeObserver(() => {});
    defaultFn(); alias(); namespace.method(); invoke(() => {}); use({}, []);
    const text = 'fakeCall()'; const css = \`var(--color) :has(button) calc(1px) \${first()}\`;
    export { text, css };
  `);
  assert.deepEqual(valid.undefinedCalls.map(item => item.name), []);
  assert.deepEqual(valid.unused.map(item => item.name), []);
  assert.deepEqual(analyzeJavaScript('const { omitted, ...rest } = object; consume(rest);').unused, []);
  assert.deepEqual(analyzeJavaScript('const { unused, used } = object; consume(used);').unused.map(item => item.name), ['unused']);
  assert.ok(!stripCppTrivia('// ghost()\nconst char* s = \"ghost()\";').includes('ghost'));
  assert.ok(stripCppTrivia('std::signal(SIGINT, callback);').includes('callback'));
  assert.deepEqual(analyzeJavaScript(`
    function one(callback) { callback(); const local = () => {}; local(); }
    function two() { callback(); local(); }
    { const blockOnly = () => {}; blockOnly(); } blockOnly();
    missingAtColumnZero();
    const html = \`CSS var(--x) \${missingInTemplate()}\`;
    export { html };
    new MissingConstructor();
  `).undefinedCalls.map(item => item.name),
  ['callback', 'local', 'blockOnly', 'missingAtColumnZero', 'missingInTemplate', 'MissingConstructor']);
  assert.deepEqual(analyzeJavaScript('function declaredElsewhere() {}').undefinedCalls, []);
  assert.deepEqual(analyzeJavaScript('declaredElsewhere();').undefinedCalls.map(item => item.name), ['declaredElsewhere']);
  assert.deepEqual(analyzeJavaScript(`
    function one() { const unused = 1; }
    function two() { const unused = 2; return unused; }
    export const publicConstant = 3;
  `).unused.map(item => item.name), ['unused']);
}
verifyJsAnalysis();
if (process.argv.includes('--self-test')) {
  console.log('JavaScript 审计自测通过：globals、参数、解构、箭头、导入/类、模板插值与词法作用域。');
  process.exit(0);
}


auditCppDeadFunctions();
auditJsDeadLocals();
auditStateContract();
auditUnusedStateFields();
auditRoutes();
auditUndefinedCalls();

// ---- 输出 ----
const errors = findings.filter(finding => finding.severity === 'error');
const information = findings.length - errors.length;
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
    console.log(`\n${errors.length ? '审计未通过' : '审计通过'}：${errors.length} 项错误，${information} 条 API 使用提示（提示不作为错误）。`);
  }
}
process.exit(errors.length ? 1 : 0);
