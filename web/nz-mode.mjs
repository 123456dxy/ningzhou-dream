// 本地测试版（服务端 --local）运行模式探测。
//
// 背景：index.html 是公开联机版与本地测试版共用的静态入口，
// 所以「关闭创建角色/登录/注册与联网模块」一律在运行期切换，不删静态入口。
// 用法：页面加载时 mode() 只发一次 /api/mode，结果写入 window.__NZ_LOCAL__。

let pending = null;

// 用途：读取当前运行模式。
// 流程：首次调用请求 /api/mode，之后复用同一个 Promise。
// 调用方式：各前端模块启动时 await mode()。
// 返回值：Promise<boolean>，true 表示本地测试版。
export function mode() {
  if (!pending) {
    pending = fetch('/api/mode', { cache: 'no-store' })
      .then(r => r.json())
      .then(d => {
        const local = !!(d && d.local);
        window.__NZ_LOCAL__ = local;
        if (d && d.nickname) window.__NZ_LOCAL_NICK = d.nickname;
        return local;
      })
      .catch(() => false);
  }
  return pending;
}

// 用途：同步读取已探测到的模式（尚未探测完时按公开联机版处理）。
// 返回值：true 表示本地测试版。
export function isLocal() { return window.__NZ_LOCAL__ === true; }

// 用途：隐藏本地测试版不该出现的联网入口。
// 流程：移除天机大比页签、天骄榜按钮与意见箱按钮；天机提醒宿主只隐藏不删除（渲染仍会写它）。
// 调用方式：探测到本地测试版后调用一次。
// 返回值：无返回值。
export function hideOnlineEntries() {
  document.querySelector('.main-tabs [data-tab="tianji"]')?.remove();
  document.getElementById('nz-lb-btn')?.remove();
  document.getElementById('nz-fb-btn')?.remove();
  const reminder = document.getElementById('tianji-reminder');
  if (reminder) { reminder.innerHTML = ''; reminder.hidden = true; }
  // 本地版没有云端，顶栏与页脚不再提联机
  const status = document.getElementById('save-status');
  if (status) status.textContent = '本地存档';
  const notes = document.querySelectorAll('.page-footer span');
  if (notes[1]) notes[1].textContent = '本地测试版 · 独立存档';
}
