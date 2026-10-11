// 多人版：道号登录
// 输入道号 + 密码登录，服务器发放会话 cookie。新道号（或尚未设密码的老道号）首次登录时所填密码即成为其密码。
// 同一道号对应同一份存档；任何接口返回 401 时重新弹出登录框。

let current = null;
let pending = null;

const NICK_RE = /^[A-Za-z0-9_\u3400-\u4DBF\u4E00-\u9FFF\uF900-\uFAFF]{1,16}$/u;

function overlay() {
  let el = document.getElementById('nz-login');
  if (el) return el;
  el = document.createElement('div');
  el.id = 'nz-login';
  el.innerHTML = `
    <form class="nz-login-card" autocomplete="off">
      <div class="nz-login-seal">觅</div>
      <h2>宁州一梦</h2>
      <p>输入你的道号与密码进入宁州。同一道号即同一份存档，换设备输入相同道号和密码即可继续。</p>
      <input name="nickname" aria-label="道号" autocomplete="username" maxlength="16" placeholder="道号：1～16 个汉字、字母、数字或下划线" required>
      <input name="password" aria-label="密码" type="password" maxlength="64" placeholder="密码：至少 4 位" autocomplete="current-password" required style="margin-top:10px">
      <div class="nz-login-hint" style="font-size:12px;color:#a08049;min-height:1em;margin-top:6px"></div>
      <div class="nz-login-error" role="alert"></div>
      <button type="submit">入 世</button>
      <small>新道号首次登录即以所填密码注册；请牢记密码，忘记后需联系管理员。</small>
    </form>`;
  document.body.appendChild(el);
  return el;
}

function showLogin(message) {
  if (pending) return pending;
  const el = overlay();
  el.hidden = false;
  const form = el.querySelector('form');
  const input = form.nickname;
  const pwd = form.password;
  const hint = el.querySelector('.nz-login-hint');
  const err = el.querySelector('.nz-login-error');
  input.onchange = input.onblur = async () => {
    const n = input.value.trim();
    hint.textContent = '';
    if (!NICK_RE.test(n)) return;
    try {
      const d = await (await rawFetch('/api/nickinfo?nickname=' + encodeURIComponent(n))).json();
      if (input.value.trim() !== n || !d.ok) return;
      hint.textContent = d.hasPassword ? '' : d.exists
        ? '此道号尚未设密码：首次设置密码，本次所填密码即成为它的密码。'
        : '新道号：首次设置密码，本次所填密码即成为它的密码。';
    } catch { /* 忽略 */ }
  };
  err.textContent = message || '';
  setTimeout(() => input.focus(), 0);
  pending = new Promise(resolve => {
    form.onsubmit = async e => {
      e.preventDefault();
      const nickname = input.value.trim();
      if (!NICK_RE.test(nickname)) {
        err.textContent = '道号需为 1～16 个汉字、英文字母、数字或下划线。';
        return;
      }
      const password = pwd.value;
      if ([...password].length < 4) { err.textContent = '密码至少 4 位。'; return; }
      form.querySelector('button').disabled = true;
      try {
        const r = await rawFetch('/api/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nickname, password }),
        });
        const d = await r.json().catch(() => ({}));
        if (!r.ok || !d.ok) { err.textContent = d.error || '登录失败，请稍后再试。'; return; }
        current = d.nickname;
        pwd.value = '';
        hint.textContent = '';
        el.hidden = true;
        pending = null;
        updateBadge();
        resolve(d);
      } catch {
        err.textContent = '网络错误，请稍后再试。';
      } finally {
        form.querySelector('button').disabled = false;
      }
    };
  });
  return pending;
}

function updateBadge() {
  const bar = document.querySelector('.toolbar');
  if (!bar) return;
  let b = document.getElementById('nz-user');
  if (!b) {
    b = document.createElement('span');
    b.id = 'nz-user';
    b.innerHTML = '<b></b> <button class="subtle" type="button" data-pw>修改密码</button><button class="subtle" type="button" data-out>切换道号</button>';
    b.querySelector('[data-out]').addEventListener('click', logout);
    b.querySelector('[data-pw]').addEventListener('click', changePassword);
    bar.prepend(b);
  }
  b.querySelector('b').textContent = current ? `道号：${current}` : '';
}

function changePassword() {
  let d = document.getElementById('nz-pw-modal');
  if (!d) {
    d = document.createElement('dialog');
    d.id = 'nz-pw-modal';
    d.innerHTML = `<form class="modal-inner" method="dialog"><div class="modal-heading"><div><div class="eyebrow"><i></i> 道号</div><h2>修改密码</h2></div><button class="close-modal" type="button" aria-label="关闭">×</button></div>
      <div class="field"><label>原密码</label><input name="old" type="password" autocomplete="current-password" required></div>
      <div class="field" style="margin-top:12px"><label>新密码（至少 4 位）</label><input name="new1" type="password" autocomplete="new-password" required></div>
      <div class="field" style="margin-top:12px"><label>再输一次新密码</label><input name="new2" type="password" autocomplete="new-password" required></div>
      <p class="nz-pw-msg small" style="color:var(--red);min-height:1.6em;margin-top:10px"></p>
      <div class="modal-actions"><button type="submit" class="primary">确认修改</button></div></form>`;
    document.body.appendChild(d);
    d.querySelector('.close-modal').onclick = () => d.close();
    const f = d.querySelector('form');
    f.onsubmit = async (e) => {
      e.preventDefault();
      const msg = d.querySelector('.nz-pw-msg');
      if (f.new1.value !== f.new2.value) { msg.textContent = '两次输入的新密码不一致。'; return; }
      if ([...f.new1.value].length < 4) { msg.textContent = '新密码至少 4 位。'; return; }
      const r = await fetch('/api/password', { method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ old: f.old.value, new: f.new1.value }) }).catch(() => null);
      const j = r ? await r.json().catch(() => ({})) : {};
      if (!r || !j.ok) { msg.textContent = j.error || '修改失败，请稍后再试。'; return; }
      f.reset(); msg.textContent = '';
      d.close();
      const t = document.getElementById('toast');
      if (t) { t.textContent = '密码已修改，其他设备需用新密码重新登录。'; t.hidden = false; setTimeout(() => { t.hidden = true; }, 3000); }
    };
  }
  d.querySelector('.nz-pw-msg').textContent = '';
  d.showModal();
}

async function logout() {
  await rawFetch('/api/logout', { method: 'POST' }).catch(() => {});
  location.reload();
}

const rawFetch = window.fetch.bind(window);

// 任何接口返回 401：弹出登录框，登录后自动重试一次
window.fetch = async (input, init) => {
  const res = await rawFetch(input, init);
  const url = typeof input === 'string' ? input : input?.url || '';
  if (res.status === 401 && url.includes('/api/') && !url.includes('/api/login')) {
    current = null;
    updateBadge();
    await showLogin('登录已失效，请重新输入道号。');
    return rawFetch(input, init);
  }
  return res;
};

// 启动时确保已登录；返回 { nickname, hasGame }
export async function ensureLogin() {
  try {
    const r = await rawFetch('/api/whoami');
    if (r.ok) {
      const d = await r.json();
      current = d.nickname;
      updateBadge();
      return d;
    }
  } catch { /* 走登录 */ }
  return showLogin();
}
