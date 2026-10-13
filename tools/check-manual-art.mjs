// 功法资源自检：只读取正式目录，核对逐本素材、体积、尺寸及回退；无需图片依赖或服务器。
import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { MANUAL_ART, MANUAL_ART_BY_ID } from '../web/assets/manuals/manifest.mjs';
import { manualIllustration, renderManualIllustration, handleManualIllustrationError } from '../web/arts.mjs';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const assetDirectory = resolve(root, 'web/assets/manuals');
let checkCount = 0;

// 用途：记录有意义的资源断言；流程：正式资源发布检查；用法：传条件与说明；返回：无，失败抛错。
function verify(condition, message) {
  assert.ok(condition, message);
  checkCount++;
}

// 用途：读取 WebP 的尺寸并核验容器完整性；流程：素材格式校验。
// 用法：传入文件 Buffer；返回：宽高对象，文件无有效图像头时抛错。
function webpDimensions(bytes) {
  assert.ok(bytes.length >= 30 && bytes.toString('ascii', 0, 4) === 'RIFF'
    && bytes.toString('ascii', 8, 12) === 'WEBP', '素材必须为真实 WebP');
  assert.equal(bytes.readUInt32LE(4) + 8, bytes.length, 'WebP 容器长度必须完整');
  for (let offset = 12; offset + 8 <= bytes.length;) {
    const kind = bytes.toString('ascii', offset, offset + 4);
    const size = bytes.readUInt32LE(offset + 4);
    const data = offset + 8;
    assert.ok(data + size <= bytes.length, 'WebP 数据块不能越界');
    if (kind === 'VP8X' && size >= 10) {
      return { width: bytes.readUIntLE(data + 4, 3) + 1, height: bytes.readUIntLE(data + 7, 3) + 1 };
    }
    if (kind === 'VP8 ' && size >= 10) {
      assert.ok(bytes[data + 3] === 0x9d && bytes[data + 4] === 1 && bytes[data + 5] === 0x2a, 'VP8 关键帧签名有效');
      return { width: bytes.readUInt16LE(data + 6) & 0x3fff, height: bytes.readUInt16LE(data + 8) & 0x3fff };
    }
    if (kind === 'VP8L' && size >= 5) {
      assert.equal(bytes[data], 0x2f, 'VP8L 签名有效');
      const bits = bytes.readUInt32LE(data + 1);
      return { width: (bits & 0x3fff) + 1, height: ((bits >>> 14) & 0x3fff) + 1 };
    }
    offset = data + size + (size & 1);
  }
  throw new Error('缺少 WebP 图像数据块');
}

const source = readFileSync(resolve(root, 'data/reference_tables.h'), 'utf8');
const baseManuals = source.slice(source.indexOf('inline const vector<BaseManualDef>& BaseManualDef_TABLE()'), source.indexOf('// 基础神通 12 种'));
const definitions = [...baseManuals.matchAll(/\{"([a-z]+)",\s*"([^"]+)",\s*"([^"]+)"/g)]
  .map(([, id, name, dao]) => ({ id, name, dao }));
verify(definitions.length === 40, '当前正式目录应包含 40 本功法');
verify(Object.keys(MANUAL_ART_BY_ID).length === definitions.length, '图片 ID 表应与正式功法目录数量一致');
verify(Object.keys(MANUAL_ART).length === definitions.length, '不存在未映射的旧共享素材定义');

const paths = new Set();
const hashes = new Set();
const report = [];
for (const manual of definitions) {
  const primary = manualIllustration(manual);
  const legacy = manualIllustration({ id: manual.id });
  verify(primary && legacy && primary.src === legacy.src, `${manual.id} 的当前与旧目录都使用同一本独立图`);
  verify(primary.src === `assets/manuals/${manual.id}-v1.webp`, `${manual.id} 使用按稳定 ID 命名的图片`);
  verify(!paths.has(primary.src), `${manual.id} 未复用其他正式功法的图片路径`);
  paths.add(primary.src);
  const bytes = readFileSync(resolve(root, 'web', primary.src));
  const dimensions = webpDimensions(bytes);
  verify(dimensions.width === 512 && dimensions.height === 512, `${manual.id} 是 512 × 512`);
  verify(bytes.length <= 90 * 1024, `${manual.id} 不超过 90 KiB`);
  const sha256 = createHash('sha256').update(bytes).digest('hex');
  verify(!hashes.has(sha256), `${manual.id} 的图片内容与所有其他正式图片不同`);
  hashes.add(sha256);
  report.push({ ...manual, path: primary.src, ...dimensions, bytes: bytes.length, sha256 });
}
verify(paths.size === 40 && hashes.size === 40, '40 个正式功法对应 40 个路径与 40 份不同内容');
verify(manualIllustration({ id: 'wood', dao: '木' }).src !== manualIllustration({ id: 'longlife', dao: '木' }).src,
  '同属木道的青木诀与长生诀仍有不同图片');
verify(manualIllustration({ id: 'gold', dao: '水' }).src.endsWith('/gold-v1.webp'), '正式功法 ID 不被同道共享回退覆盖');
verify(manualIllustration({ id: 'new-manual', daoId: 'metal' }).src.endsWith('/gold-v1.webp'), '未来未知 ID 可以按稳定大道显示默认图');
verify(manualIllustration({ id: 'unrecognised', name: '金虹诀', dao: 'unknown' }) === null, '不按可编辑名称推测素材');
verify(!renderManualIllustration({ id: 'unrecognised' }).includes('<img'), '完全未知条目保留文字回退');
verify(renderManualIllustration({ id: 'wood' }, { compact: true }).includes('manual-art--compact'), '来源列表支持紧凑插图');
const own = { hidden: false, matches: selector => selector === 'img[data-manual-art]' };
const unrelated = { hidden: false, matches: () => false };
handleManualIllustrationError({ target: own });
handleManualIllustrationError({ target: unrelated });
handleManualIllustrationError(null);
verify(own.hidden && !unrelated.hidden, '图片错误只降级功法图片，不影响人物立绘');
const generated = JSON.parse(readFileSync(resolve(assetDirectory, 'generation-spec.json'), 'utf8'));
verify(Object.keys(generated.assets).length === 40 && definitions.every(manual => generated.assets[manual.id]?.prompt), '每本正式功法均记录真实生成提示');
verify(new Set(Object.values(generated.assets).map(asset => asset.generatedSource)).size === 40, '40 本使用独立的图像生成输出');
verify(readdirSync(assetDirectory).filter(name => name.endsWith('.webp')).length === 40, '发布目录恰好只有 40 张正式成品');
verify(!readdirSync(assetDirectory).some(name => /\.png$/i.test(name)), '原始 PNG 未放入发布目录');
verify(readFileSync(resolve(root, 'web/index.html'), 'utf8').includes('href="manuals-art.css"'), '桌面入口加载功法专用样式');
const summary = { checks: checkCount, count: report.length, uniquePaths: paths.size, uniqueHashes: hashes.size,
  totalBytes: report.reduce((sum, asset) => sum + asset.bytes, 0), maxBytes: Math.max(...report.map(asset => asset.bytes)), assets: report };
if (process.argv.includes('--write-report')) writeFileSync(resolve(assetDirectory, 'asset-check.json'), JSON.stringify(summary, null, 2) + '\n');
console.log(`Manual artwork: ${checkCount} checks passed; 40 unique paths and SHA256; ${summary.totalBytes} bytes total, ${summary.maxBytes} bytes maximum.`);
