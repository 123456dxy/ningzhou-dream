// 多人版：道号登录
// 第一次打开时输入道号（昵称），服务器发放会话 cookie。
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
      <p>输入你的道号进入宁州。同一道号即同一份存档，换设备输入相同道号即可继续。</p>
      <input name="nickname" maxlength="16" placeholder="道号：1～16 个汉字、字母、数字或下划线" required>
      <div class="nz-login-error" role="alert"></div>
      <button type="submit">入 世</button>
      <small>无需密码：请勿使用他人的道号，也不要把重要内容写进存档。</small>
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
  const err = el.querySelector('.nz-login-error');
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
      form.querySelector('button').disabled = true;
      try {
        const r = await rawFetch('/api/login', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ nickname }),
        });
        const d = await r.json().catch(() => ({}));
        if (!r.ok || !d.ok) { err.textContent = d.error || '登录失败，请稍后再试。'; return; }
        current = d.nickname;
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
    b.innerHTML = '<b></b> <button class="subtle" type="button">切换道号</button>';
    b.querySelector('button').addEventListener('click', logout);
    bar.prepend(b);
  }
  b.querySelector('b').textContent = current ? `道号：${current}` : '';
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
