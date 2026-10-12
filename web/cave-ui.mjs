import { esc } from './shell.mjs';

// 用途：读取洞府供应档位权限。
// 流程：按kind与level查找权威档位，给出可用性和原因。
// 调用方式：配给按钮传入state、kind和level。
// 返回值：含enabled与reason的对象。
export function caveSupplyAccess(state, kind, level) {
  if (kind !== 'qi' || level === 0) return { enabled: true, reason: '' };
  if (!state.player?.cave) return { enabled: false, reason: '尚未安府，需先在此安府。' };
  const cave = state.cave || {}, supply = state.player?.supply || {};
  const rows = cave.supplies || cave.supplyRows || supply.qiRows || supply.qiOptions || [];
  const row = rows.find(row => Number(row.level ?? row.id) === level);
  if (row?.locked || row?.enabled === false) return { enabled: false, reason: row.reason || `需${row.tierName || '升级洞府'}。` };
  if (cave.maxSupply != null && level > cave.maxSupply)
    return { enabled: false, reason: `当前${cave.tierName || '洞府'}最多启用 ${cave.maxSupply} 档灵气配给。` };
  return { enabled: true, reason: '' };
}

// 用途：显示洞府升级条件。
// 流程：读取cave权威等级与报价，生成升级说明和按钮。
// 调用方式：洞府页面传入state。
// 返回值：升级面板HTML字符串。
export function renderCaveUpgrade(state) {
  const cave = state.cave;
  if (!cave) return '';
  const upgrade = cave.upgrade;
  const current = `<section class="cave-tier-card" data-cave-tier="${cave.tier}"><span>洞府档位</span><h4>${esc(cave.tierName)}</h4>`
    + `<p>最高可启用 ${esc(cave.maxSupply)} 档灵气配给。</p>`;
  if (!upgrade) return current + `<small>已达到最高档洞府。</small></section>`;
  return current + `<div class="cave-upgrade-target"><strong>下一档：${esc(upgrade.name)}</strong><p>${esc(upgrade.desc)}</p>`
    + `<small>声望达到 ${esc(upgrade.minRenown)} · ${esc(upgrade.cost)} 灵石</small>`
    + `${upgrade.reason ? `<p class="cave-upgrade-reason">${esc(upgrade.reason)}</p>` : ''}`
    + `<button class="wide-btn" data-action="cave-upgrade" ${upgrade.enabled ? '' : 'disabled'}>升档至${esc(upgrade.name)}</button></div></section>`;
}

// 用途：解释供应解锁限制。
// 流程：遍历权威供应档位，显示境界、声望及权限原因。
// 调用方式：洞府页面传入state。
// 返回值：限制说明HTML字符串。
export function renderCaveSupplyLimits(state) {
  const cave = state.cave || {}, supply = state.player?.supply || {};
  const rows = cave.supplies || cave.supplyRows || supply.qiRows || supply.qiOptions || [];
  if (!rows.length) return cave.maxSupply != null ? `<p class="cave-supply-note">${esc(cave.tierName)}支持最高 ${esc(cave.maxSupply)} 档灵气；更高方案需声望足够后升档。</p>` : '';
  return `<div class="cave-supply-limits">${rows.map(row => {
    const level = Number(row.level ?? row.id);
    const access = caveSupplyAccess(state, 'qi', level);
    return `<div class="cave-supply-limit${access.enabled ? '' : ' locked'}"><strong>${level} 档 · ${esc(row.name)}</strong>`
      + `<span>${esc(row.desc || '')}</span><small>${esc(access.reason || `${row.tierName || cave.tierName || '洞府'}可启用`)}${row.minRenown ? ` · 声望 ${row.minRenown}` : ''}</small></div>`;
  }).join('')}</div>`;
}
