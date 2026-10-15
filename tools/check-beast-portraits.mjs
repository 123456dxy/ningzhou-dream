// 只读核对六主题五境图路径与C++目录；--assets另检查已交付WebP，不启动服务器或接触玩家存档。
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { resolve, dirname } from 'node:path';
import { BEAST_PORTRAIT_THEMES, beastPortraitSource, isBeastPortraitPath } from '../web/beast-portraits.mjs';
import { portraitSource } from '../web/portraits.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
let checks = 0;

// 用途：统计有意义的资源断言；流程：定向资源验收；用法：传真假与说明；返回：无，失败即抛出。
function verify(condition, message) {
  assert.ok(condition, message);
  checks++;
}

const cpp = readFileSync(resolve(root, 'src/core/content/beast_portraits.cpp'), 'utf8');
const definitions = [...cpp.matchAll(/\{BeastPortraitTheme::(\w+),\s*"([a-z-]+)",\s*content::DaoId::(\w+)\}/g)]
  .map(([, kind, id, dao]) => ({ kind, id, dao }));
const daoSource = readFileSync(resolve(root, 'src/core/content/game_ids.cpp'), 'utf8');
const daos = new Map([...daoSource.matchAll(/Entry\{DaoId::(\w+),\s*"([a-z]+)",\s*"([^"]+)"\}/g)]
  .map(([, id, stable, legacy]) => [id, [stable, legacy]]));
verify(definitions.length === 6 && BEAST_PORTRAIT_THEMES.length === 6, 'C++与前端都注册六种真实主题');
verify(new Set(definitions.map(item => item.id)).size === 6, '后端主题ID唯一');

const paths = new Set();
for (const definition of definitions) {
  const frontend = BEAST_PORTRAIT_THEMES.find(theme => theme.id === definition.id);
  verify(!!frontend, `${definition.id} 前后端都有同一稳定ID`);
  const aliases = [...daos.get(definition.dao)];
  if (definition.dao === 'Earth') aliases.push(...daos.get('Water'));
  if (definition.dao === 'Metal') aliases.push(...daos.get('Sword'));
  verify(frontend.styles.length === aliases.length && aliases.every(alias => frontend.styles.includes(alias)),
    `${definition.id} 大道白名单与后端别名一致`);
  for (let major = 0; major < 5; major++) {
    const expected = `assets/portraits/beast-${definition.id}-${major}-v1.webp`;
    verify(!paths.has(expected), '不同主题和境界使用独立文件'); paths.add(expected);
    verify(isBeastPortraitPath(expected) && isBeastPortraitPath(`/${expected}`), '后端绝对路径与网页相对路径都属于白名单');
    for (const alias of [definition.id, ...aliases]) {
      const person = { style: alias, stage: major * 3 };
      verify(beastPortraitSource(person) === expected, `${definition.id}/${major} 按实际风格取图`);
      verify(portraitSource(person, 'beast') === expected, '正式前端与共享妖兽fallback一致');
    }
    if (major < 4) verify(beastPortraitSource({ style: definition.id, stage: major * 3 + 2 }) === expected,
      '同大境界小境界合理共享，不跨大境界冒用');
    verify(beastPortraitSource({ portrait: `/${expected}`, style: 'unknown', stage: 0 }) === `/${expected}`,
      '服务端白名单路径优先，不被前端猜测覆盖');
  }
}
verify(paths.size === 30, '六主题五境界共30条独立路径');
for (const invalid of ['', 'unknown', '气', '../flame-beast', '岩甲守卫', 'https://example.com/beast.webp']) {
  const person = { style: invalid, stage: 6 };
  verify(beastPortraitSource(person) === 'assets/portraits/beast-2.webp', '未知风格、显示名与路径不能绕过主题白名单');
}
for (const portrait of ['/assets/portraits/rogue.webp', '/assets/portraits/beast-flame-beast-5-v1.webp',
  '/assets/portraits/beast-flame-beast-2-v1.webp?path=other', '../assets/portraits/beast-2.webp', 'https://example.com/a.webp']) {
  verify(!isBeastPortraitPath(portrait), '人物图、越界档位、查询串、相对穿越和外部URL不能充作妖兽权威图');
  verify(beastPortraitSource({ portrait, style: 'fire', stage: 6 }) === 'assets/portraits/beast-flame-beast-2-v1.webp',
    '非法权威路径仍按合法主题安全回退');
}
verify(beastPortraitSource({ enemyDao: '火', style: '土', stage: 6 }) === 'assets/portraits/beast-flame-beast-2-v1.webp',
  '真实NPC本命大道优先于历史style，与后端一致');
verify(beastPortraitSource({ enemyDao: 'unknown', style: '土', stage: 6 }) === 'assets/portraits/beast-2.webp',
  '非空未知本命大道不偷偷转成其他风格');
verify(beastPortraitSource({ style: '火', stage: -100 }) === 'assets/portraits/beast-flame-beast-0-v1.webp', '越下界安全限制');
verify(beastPortraitSource({ style: '火', stage: 100 }) === 'assets/portraits/beast-flame-beast-4-v1.webp', '越上界安全限制');
verify(beastPortraitSource({ style: '火', stage: Number.NaN }) === 'assets/portraits/beast-flame-beast-0-v1.webp', '非有限境界安全回退');
verify(readFileSync(resolve(root, 'web/battle-lab.mjs'), 'utf8').includes("import { beastPortraitSource } from './beast-portraits.mjs'"),
  '独立试炼使用相同fallback模块');
verify(readFileSync(resolve(root, 'web/app.mjs'), 'utf8').includes('enemyDao: b.enemyDao, style: b.style'),
  '正式战斗fallback收到真实大道和兼容style');

// 用途：只读提取WebP容器尺寸及透明标记；流程：生成资产交付后验收；用法：传文件字节；返回：结构元信息。
function webpMetadata(bytes) {
  assert.ok(bytes.length >= 30 && bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP');
  assert.equal(bytes.readUInt32LE(4) + 8, bytes.length);
  let width, height, alpha = false;
  for (let offset = 12; offset + 8 <= bytes.length;) {
    const kind = bytes.toString('ascii', offset, offset + 4), size = bytes.readUInt32LE(offset + 4), data = offset + 8;
    assert.ok(data + size <= bytes.length);
    if (kind === 'VP8X' && size >= 10) {
      width = bytes.readUIntLE(data + 4, 3) + 1; height = bytes.readUIntLE(data + 7, 3) + 1;
      alpha ||= Boolean(bytes[data] & 0x10);
    } else if (kind === 'VP8 ' && size >= 10) {
      width ??= bytes.readUInt16LE(data + 6) & 0x3fff; height ??= bytes.readUInt16LE(data + 8) & 0x3fff;
    } else if (kind === 'VP8L' && size >= 5) {
      const bits = bytes.readUInt32LE(data + 1);
      width ??= (bits & 0x3fff) + 1; height ??= ((bits >>> 14) & 0x3fff) + 1; alpha ||= Boolean(bits & (1 << 28));
    }
    if (kind === 'ALPH') alpha = true;
    offset = data + size + (size & 1);
  }
  assert.ok(Number.isInteger(width) && Number.isInteger(height));
  return { width, height, alpha };
}

let totalBytes = 0;
if (process.argv.includes('--assets')) {
  const hashes = new Set();
  for (const path of paths) {
    const bytes = readFileSync(resolve(root, 'web', path));
    const info = webpMetadata(bytes), hash = createHash('sha256').update(bytes).digest('hex');
    verify(info.width >= 512 && info.width * 3 === info.height * 2, `${path} 为清晰2:3立绘`);
    verify(info.alpha, `${path} 保留透明通道`);
    verify(bytes.length <= 400 * 1024, `${path} 已压缩至400KiB内`);
    verify(!hashes.has(hash), `${path} 不是复制同一图片充数`); hashes.add(hash); totalBytes += bytes.length;
  }
}
console.log(`Beast portraits: ${checks} checks passed; ${paths.size} themed paths${process.argv.includes('--assets') ? `; ${totalBytes} asset bytes` : '; asset files not checked'}.`);
