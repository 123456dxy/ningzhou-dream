import { esc } from './shell.mjs';

export function renderArtifactIllustration(item = {}, { compact = false, fallback = '✧' } = {}) {
  const path = item.illustration || '';
  if (!/^\/assets\/artifacts\/(sword|needle|seal|ruler|box|armor|robe|bell|pearl|ring|token)-v1\.png$/.test(path)) return '';
  return `<span class="artifact-illustration${compact ? ' compact' : ''}"><img data-artifact-illustration src="${esc(path)}" alt="${esc(item.name || item.shapeName || '法器')}" loading="lazy" decoding="async"><span class="artifact-illustration-fallback" hidden>${fallback}</span></span>`;
}

if (typeof document !== 'undefined') document.addEventListener('error', event => {
  const image = event.target;
  if (!image?.matches?.('img[data-artifact-illustration]')) return;
  image.hidden = true;
  const fallback = image.nextElementSibling;
  if (fallback?.classList.contains('artifact-illustration-fallback')) fallback.hidden = false;
}, true);
