// 天骄榜：多人排行榜（数据由服务器从各道号存档计算，按 境界 > 修为 > 声望 排序）
// 本地测试版（--local）不显示该入口，也不请求 /api/leaderboard。
import { mode } from './nz-mode.mjs';
const esc = (s) => String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));

function dialog() {
  let d = document.getElementById('leaderboard-modal');
  if (!d) {
    d = document.createElement('dialog');
    d.id = 'leaderboard-modal';
    d.setAttribute('aria-label', '天骄榜');
    d.addEventListener('click', (e) => { if (e.target === d || e.target.closest('.close-modal')) d.close(); });
    document.body.appendChild(d);
  }
  return d;
}

async function openBoard() {
  const d = dialog();
  d.innerHTML = `<div class="modal-inner"><div class="modal-heading"><div><div class="eyebrow"><i></i> 宁州众修 · 同参大道</div><h2>天骄榜</h2></div><button class="close-modal" aria-label="关闭">×</button></div><div class="modal-body nz-lb-body">推演中……</div></div>`;
  if (!d.open) d.showModal();
  const body = d.querySelector('.nz-lb-body');
  try {
    const r = await fetch('/api/leaderboard?limit=50');
    const data = await r.json();
    if (!data.ok) throw new Error(data.error || '读取失败');
    const me = document.querySelector('#nz-user b')?.textContent.replace(/^道号：/, '');
    const rows = data.list.map((x) => `<tr class="${x.nickname === me ? 'me' : ''} ${x.rank <= 3 ? 'top' + x.rank : ''}">
      <td class="rk">${x.rank}</td><td><b>${esc(x.nickname)}</b><small>${esc(x.name)}${x.ended ? ' · 已坐化' : ''}</small></td>
      <td>${esc(x.realm)}</td><td class="num">${x.xp}</td><td class="num">${x.renown}</td><td class="num">${x.age}</td></tr>`).join('');
    body.innerHTML = data.list.length
      ? `<table class="nz-lb"><thead><tr><th>名次</th><th>道号 / 角色</th><th>境界</th><th>修为</th><th>声望</th><th>年岁</th></tr></thead><tbody>${rows}</tbody></table>
         <p class="small muted">共 ${data.total} 位道友上榜 · 按境界、修为、声望排序 · 依各自云端存档推算，约半分钟更新一次</p>`
      : '<p>尚无道友留名。</p>';
  } catch (e) {
    body.textContent = '天骄榜暂时无法读取：' + e.message;
  }
}

async function mount() {
  if (await mode()) return;   // 本地测试版：没有天骄榜
  const bar = document.querySelector('.toolbar');
  if (!bar || document.getElementById('nz-lb-btn')) return;
  const b = document.createElement('button');
  b.id = 'nz-lb-btn';
  b.className = 'subtle';
  b.type = 'button';
  b.textContent = '天骄榜';
  b.addEventListener('click', openBoard);
  bar.insertBefore(b, bar.querySelector('[data-action="settings"]'));
  const st = document.createElement('style');
  st.textContent = `.nz-lb{width:100%;border-collapse:collapse;font-size:12px;color:var(--ink)}.nz-lb th{font-weight:normal;font-size:10px;color:#8d957e;text-align:left;border-bottom:1px solid var(--line);padding:6px 8px}.nz-lb td{padding:7px 8px;border-bottom:1px solid #e8ebdf;line-height:1.5}.nz-lb td small{display:block;font-size:10px;color:var(--muted)}.nz-lb .num{text-align:right;font-family:var(--serif)}.nz-lb th:nth-child(n+4){text-align:right}.nz-lb .rk{font:16px var(--serif);color:#8b9382;width:36px;text-align:center}.nz-lb .top1 .rk{color:#b8913f}.nz-lb .top2 .rk{color:#8f9aa0}.nz-lb .top3 .rk{color:#a8714a}.nz-lb tr.me{background:#e8eee1}#leaderboard-modal .modal-body{max-height:65vh;overflow:auto}`;
  document.head.appendChild(st);
}
mount();
