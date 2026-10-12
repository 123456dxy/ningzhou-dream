export const destinations = [ ['home','修行','修'], ['map','山河','山'], ['inventory','行囊','囊'], ['arts','道法','道'], ['social','世间','世'] ];
const allowed = new Set(destinations.map(x => x[0]));
export function parseRoute(hash = '') {
  const [route, section] = hash.replace(/^#\/?/, '').split('/');
  return {route:allowed.has(route) ? route : 'home', section:section || ''};
}
export function createRouter(onChange, win = globalThis.window) {
  const update = () => onChange(parseRoute(win.location.hash));
  win.addEventListener('popstate', update);
  win.addEventListener('hashchange', update);
  return {
    start:update,
    go(route, section = '') { const next = `#/${allowed.has(route) ? route : 'home'}${section ? '/' + encodeURIComponent(section) : ''}`; if (next === win.location.hash) { update(); return; } win.history.pushState({},'',next); update(); },
    dispose() { win.removeEventListener('popstate',update);win.removeEventListener('hashchange',update); }
  };
}
