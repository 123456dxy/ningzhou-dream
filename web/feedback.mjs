// 反馈箱：顶栏「反馈」按钮 → 弹窗选类型 + 填文字，提交到 /api/feedback（需登录）。
// 服务器自动附上道号、时间、境界、游戏内日期等上下文；不提供公开读取。
const MAX = 2000;

function dialog() {
  let d = document.getElementById('feedback-modal');
  if (d) return d;
  d = document.createElement('dialog');
  d.id = 'feedback-modal';
  d.setAttribute('aria-label', '反馈');
  d.innerHTML = `<form class="modal-inner" method="dialog">
    <div class="modal-heading"><div><div class="eyebrow"><i></i> 宁州一梦 · 意见箱</div><h2>反馈</h2></div><button class="close-modal" type="button" aria-label="关闭">×</button></div>
    <div class="modal-body" style="line-height:1.8">
      <div class="nz-fb-types" role="radiogroup" style="display:flex;gap:8px;margin-bottom:12px">
        <label class="nz-fb-type"><input type="radio" name="type" value="bug" checked> 遇到 Bug</label>
        <label class="nz-fb-type"><input type="radio" name="type" value="suggest"> 建议</label>
        <label class="nz-fb-type"><input type="radio" name="type" value="other"> 其他</label>
      </div>
      <textarea name="text" maxlength="${MAX}" rows="7" placeholder="说说遇到了什么、在哪个页面、怎么复现；或者你希望加什么。" style="width:100%;border:1px solid #d3d9c4;border-radius:5px;padding:10px 12px;background:#fdfcf7;color:var(--ink);font:13px/1.7 var(--sans);resize:vertical"></textarea>
      <div style="display:flex;justify-content:space-between;font-size:11px"><span class="muted">会自动附上你的道号、境界和游戏内日期，方便排查。</span><span class="nz-fb-count muted">0 / ${MAX}</span></div>
      <p class="nz-fb-msg" style="color:var(--red);min-height:1.6em;margin:6px 0 0"></p>
    </div>
    <div class="modal-actions"><button type="button" class="nz-fb-cancel">取消</button><button type="submit" class="primary">提交反馈</button></div>
  </form>`;
  document.body.appendChild(d);
  const f = d.querySelector('form');
  const msg = d.querySelector('.nz-fb-msg');
  const count = d.querySelector('.nz-fb-count');
  d.querySelector('.close-modal').onclick = d.querySelector('.nz-fb-cancel').onclick = () => d.close();
  f.text.addEventListener('input', () => { count.textContent = `${[...f.text.value].length} / ${MAX}`; });
  f.onsubmit = async (e) => {
    e.preventDefault();
    const text = f.text.value.trim();
    if (!text) { msg.textContent = '请写点内容再提交。'; return; }
    if ([...text].length > MAX) { msg.textContent = `最多 ${MAX} 字。`; return; }
    const btn = f.querySelector('[type=submit]');
    btn.disabled = true; msg.textContent = '';
    try {
      const r = await fetch('/api/feedback', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ type: f.type.value, text }) });
      const j = await r.json().catch(() => ({}));
      if (!r.ok || !j.ok) { msg.textContent = j.error || '提交失败，请稍后再试。'; return; }
      f.reset(); count.textContent = `0 / ${MAX}`;
      d.close();
      const t = document.getElementById('toast');
      if (t) { t.textContent = '反馈已收到，多谢道友！'; t.className = 'toast'; t.hidden = false; setTimeout(() => { t.hidden = true; }, 2600); }
    } catch {
      msg.textContent = '网络错误，请稍后再试。';
    } finally { btn.disabled = false; }
  };
  return d;
}

function mount() {
  const bar = document.querySelector('.toolbar');
  if (!bar || document.getElementById('nz-fb-btn')) return;
  const b = document.createElement('button');
  b.id = 'nz-fb-btn'; b.className = 'subtle'; b.type = 'button'; b.textContent = '反馈';
  b.addEventListener('click', () => { const d = dialog(); d.querySelector('.nz-fb-msg').textContent = ''; d.showModal(); });
  bar.insertBefore(b, bar.querySelector('[data-action="settings"]'));
  const st = document.createElement('style');
  st.textContent = `.nz-fb-type{border:1px solid var(--line);border-radius:6px;padding:4px 12px;cursor:pointer;font-size:12px;background:var(--surface)}.nz-fb-type:has(input:checked){border-color:#729467;background:#e4ecd9;color:var(--green)}.nz-fb-type input{margin:0 4px 0 0;vertical-align:-1px}`;
  document.head.appendChild(st);
}
mount();
