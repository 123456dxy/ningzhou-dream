import { WeatherMusicTracks, mfCreateWeatherMusic } from './weather-music.mjs';

let sharedControls = null;

const statusText = Object.freeze({
  off: '背景音乐已关闭',
  waiting: '等待播放，点击下方按钮即可开始。',
  loading: '正在载入音乐……',
  playing: '正在播放',
  paused: '播放已暂停，可点击继续。',
  error: '暂时无法播放，请重试。',
});

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, character => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  })[character]);
}

export function mfRenderMusicButton() {
  return '<button type="button" class="music-open-button" data-music-open aria-haspopup="dialog" aria-controls="music-dialog" aria-label="背景音乐设置"><span aria-hidden="true">♫</span><span>音乐</span><i data-music-indicator aria-hidden="true"></i></button>';
}

export function mfRenderMusicSettingsEntry() {
  return '<section class="music-settings-entry" aria-label="背景音乐"><div><strong>背景音乐</strong><p>随宁州天气切换，也可手动选曲。</p></div>' + mfRenderMusicButton() + '</section>';
}

// One page owns one controller. The dialog survives either client rebuilding its
// game workspace; media updates only patch this small, independent set of nodes.
export function mfCreateMusicControls() {
  if (sharedControls) return sharedControls;
  const controller = mfCreateWeatherMusic();
  const dialog = document.createElement('dialog');
  dialog.id = 'music-dialog';
  dialog.className = 'music-dialog';
  dialog.setAttribute('aria-labelledby', 'music-dialog-title');
  dialog.innerHTML = `<div class="music-dialog-inner">
    <header class="music-dialog-heading"><div><p class="music-eyebrow">宁州 · 天气五景</p><h2 id="music-dialog-title">背景音乐</h2></div><button type="button" class="music-close-button" data-music-close aria-label="关闭背景音乐设置" autofocus>关闭</button></header>
    <label class="music-toggle"><span><strong>背景音乐</strong><small>仅保存在当前浏览器</small></span><input type="checkbox" role="switch" data-music-enabled aria-label="开启背景音乐"></label>
    <section class="music-now-playing" aria-label="当前音乐"><p class="music-weather">宁州气候 · <span data-music-weather>尚未入世</span></p><strong data-music-title></strong><p class="music-selection" data-music-selection></p><p class="music-status" data-music-status role="status" aria-live="polite"></p><p class="music-error" data-music-error hidden></p><button type="button" class="music-retry-button" data-music-retry hidden>点击播放</button></section>
    <label class="music-field" for="music-mode"><span>选曲方式</span><select id="music-mode" data-music-mode><option value="auto">自动 · 随天气切换</option>${WeatherMusicTracks.map(track => `<option value="${escapeHtml(track.id)}">手动 · ${escapeHtml(track.title)}</option>`).join('')}</select></label>
    <label class="music-field" for="music-volume"><span class="music-volume-label">音量 <output for="music-volume" data-music-volume-label>25%</output></span><input id="music-volume" type="range" min="0" max="100" step="1" value="25" data-music-volume aria-label="背景音乐音量"></label>
    <p class="music-help">默认关闭。自动模式随天气柔和切换；手动选曲会保持所选音乐。声音偏轻，可按需调节音量。</p>
  </div>`;
  document.body.append(dialog);
  let disposed = false;
  let returnFocus = null;

  function refreshControls() {
    if (disposed) return;
    const snapshot = controller.getSnapshot();
    const volumePercent = Math.round(snapshot.volume * 100);
    // A failed switch can report an error while the previous track keeps playing.
    const errorText = snapshot.error ? String(snapshot.error) : '';
    const hasError = !!errorText || snapshot.status === 'error';
    const switchFailed = snapshot.status === 'playing' && !!errorText;
    dialog.querySelector('[data-music-enabled]').checked = snapshot.enabled;
    const modeSelect = dialog.querySelector('[data-music-mode]');
    if (modeSelect.value !== snapshot.mode) modeSelect.value = snapshot.mode;
    const volumeInput = dialog.querySelector('[data-music-volume]');
    // Preserve range dragging and keyboard focus rather than replacing controls.
    if (Number(volumeInput.value) !== volumePercent) volumeInput.value = String(volumePercent);
    dialog.querySelector('[data-music-volume-label]').value = `${volumePercent}%`;
    dialog.querySelector('[data-music-weather]').textContent = snapshot.weather || '尚未入世';
    const trackTitle = snapshot.title || '晴山 · 松间初照';
    dialog.querySelector('[data-music-title]').textContent = switchFailed ? `待切换 · ${trackTitle}` : trackTitle;
    dialog.querySelector('[data-music-selection]').textContent = snapshot.mode === 'auto' ? '自动选曲 · 随天气' : '手动选曲 · 保持此曲';
    const status = switchFailed ? '切换未完成，原曲继续播放。' : snapshot.volume === 0 && snapshot.enabled && snapshot.status === 'playing' ? '正在播放 · 音量为 0' : statusText[snapshot.status] || '背景音乐';
    dialog.querySelector('[data-music-status]').textContent = status;
    const error = dialog.querySelector('[data-music-error]');
    error.hidden = !hasError;
    error.textContent = hasError ? errorText || '音乐暂时未能载入，请检查网络后重试。' : '';
    const retry = dialog.querySelector('[data-music-retry]');
    retry.hidden = !snapshot.enabled || !(hasError || ['waiting', 'paused'].includes(snapshot.status));
    retry.textContent = switchFailed ? '重试切换' : hasError ? '重试播放' : '点击播放';
    document.querySelectorAll('[data-music-open]').forEach(button => {
      button.dataset.musicState = snapshot.enabled ? hasError ? 'error' : snapshot.status : 'off';
      button.setAttribute('aria-label', `背景音乐设置 · ${status}`);
      button.title = `背景音乐 · ${status}`;
    });
  }

  function closeDialog() {
    if (dialog.open) dialog.close();
  }
  function handleClose() {
    if (returnFocus?.isConnected) returnFocus.focus({ preventScroll: true });
    returnFocus = null;
  }
  function handleClick(event) {
    const target = event.target.closest?.('[data-music-open],[data-music-close],[data-music-retry]');
    if (!target || target.disabled) return;
    if (target.hasAttribute('data-music-open')) {
      returnFocus = target;
      refreshControls();
      if (!dialog.open) dialog.showModal();
    } else if (target.hasAttribute('data-music-close')) closeDialog();
    else void controller.setEnabled(true);
  }
  function handleChange(event) {
    if (event.target.matches('[data-music-enabled]')) void controller.setEnabled(event.target.checked);
    else if (event.target.matches('[data-music-mode]')) controller.setMode(event.target.value);
  }
  function handleInput(event) {
    if (event.target.matches('[data-music-volume]')) controller.setVolume(Number(event.target.value) / 100);
  }
  function handleBackdrop(event) {
    if (event.target !== dialog) return;
    const bounds = dialog.getBoundingClientRect();
    if (event.clientX < bounds.left || event.clientX > bounds.right || event.clientY < bounds.top || event.clientY > bounds.bottom) closeDialog();
  }
  document.addEventListener('click', handleClick);
  dialog.addEventListener('change', handleChange);
  dialog.addEventListener('input', handleInput);
  dialog.addEventListener('click', handleBackdrop);
  dialog.addEventListener('close', handleClose);
  const unsubscribe = controller.subscribe(refreshControls);
  refreshControls();
  sharedControls = Object.freeze({
    setWeather(weather) {
      if (disposed) return;
      controller.setWeather(weather || '');
      refreshControls();
    },
    refreshControls,
    dispose() {
      if (disposed) return;
      disposed = true;
      unsubscribe();
      controller.dispose();
      document.removeEventListener('click', handleClick);
      dialog.removeEventListener('change', handleChange);
      dialog.removeEventListener('input', handleInput);
      dialog.removeEventListener('click', handleBackdrop);
      dialog.removeEventListener('close', handleClose);
      dialog.remove();
      sharedControls = null;
    },
  });
  return sharedControls;
}
